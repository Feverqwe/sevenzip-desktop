// Adapted from 7-Zip CPP/7zip/UI/Agent/ArchiveFolderOut.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native transaction and CopyFromFile/Delete/CreateFolder/Rename branches;
// other update command branches remain mandatory.
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/LimitedStreams.h"
#include "7zip/Compress/CopyCoder.h"
#include "Common/ComTry.h"
#include "archive/agent/Agent.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivedeleteinputs.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveupdatepublication.h"
#include "archive/resources/qtarchivewritelease.h"
#include "fileops/native/FileDir.h"
#include "platform/temp/WorkDir.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QCoreApplication>
#include <QScopeGuard>

using namespace NWindows;
using namespace NFile;
using namespace NDir;

void CAgentFolder::GetPathParts(UStringVector &pathParts, bool &isAltStreamFolder) {
    if (_closedArchivePath) {
        pathParts = _closedArchivePath->Parts;
        isAltStreamFolder = _closedArchivePath->IsAltStreamFolder;
        return;
    }
    bool isChangedPath = false;
    if (_proxy2)
        _proxy2->GetDirPathParts(_proxyDirIndex, pathParts, isAltStreamFolder);
    else
        _proxy->GetDirPathParts_isChanged(_proxyDirIndex, pathParts, isChangedPath);
}

void CAgentFolder::RestorePath(const UStringVector &pathParts, bool isAltStreamFolder) {
    if (_proxy) {
        FOR_VECTOR(i, pathParts) {
            const int next = _proxy->FindSubDir(_proxyDirIndex, pathParts[i]);
            if (next == -1)
                break;
            _proxyDirIndex = (unsigned)next;
        }
    }
    if (_proxy2) {
        if (pathParts.IsEmpty() && isAltStreamFolder)
            _proxyDirIndex = k_Proxy2_AltRootDirIndex;
        else
            FOR_VECTOR(i, pathParts) {
                const bool dirOnly = (i + 1 < pathParts.Size() || !isAltStreamFolder);
                const int index = _proxy2->FindItem(_proxyDirIndex, pathParts[i], dirOnly);
                if (index == -1)
                    break;
                const CProxyFile2 &file =
                    _proxy2->Files[_proxy2->Dirs[_proxyDirIndex].Items[index]];
                if (dirOnly)
                    _proxyDirIndex = (unsigned)file.DirIndex;
                else {
                    if (file.AltDirIndex != -1)
                        _proxyDirIndex = (unsigned)file.AltDirIndex;
                    break;
                }
            }
        _isAltStreamFolder = _proxy2->IsAltDir(_proxyDirIndex);
    }
}

static bool Delete_EmptyFolder_And_EmptySubFolders(const FString &path) {
    {
        const FString pathPrefix = path + FCHAR_PATH_SEPARATOR;
        CObjectVector<FString> names;
        {
            NFind::CDirEntry fileInfo;
            NFind::CEnumerator enumerator;
            enumerator.SetDirPrefix(pathPrefix);
            for (;;) {
                bool found;
                if (!enumerator.Next(fileInfo, found))
                    return false;
                if (!found)
                    break;
#ifdef _WIN32
                if (fileInfo.IsDir())
#else
                if (enumerator.DirEntry_IsDir(fileInfo, false))
#endif
                    names.Add(fileInfo.Name);
            }
        }
        bool res = true;
        FOR_VECTOR(i, names) {
            if (!Delete_EmptyFolder_And_EmptySubFolders(pathPrefix + names[i]))
                res = false;
        }
        if (!res)
            return false;
    }
    // we clear read-only attrib to remove read-only dir
#ifdef _WIN32
    if (!SetFileAttrib(path, 0))
        return false;
#endif
    return RemoveDir(path);
}

struct C_CopyFileProgress_to_FolderCallback_MoveArc Z7_final : public ICopyFileProgress {
    IFolderArchiveUpdateCallback_MoveArc *Callback;
    HRESULT CallbackResult;

    virtual DWORD CopyFileProgress(UInt64 total, UInt64 current) Z7_override {
        HRESULT res = Callback->MoveArc_Progress(total, current);
        CallbackResult = res;
        // Atomic publication keeps the original on an interrupted copy, so the
        // source no longer needs to suppress cancellation after deleting it.
        return res == S_OK ? PROGRESS_CONTINUE : PROGRESS_CANCEL;
    }

    C_CopyFileProgress_to_FolderCallback_MoveArc(
        IFolderArchiveUpdateCallback_MoveArc *callback Z7_lifetimebound)
        : Callback(callback), CallbackResult(S_OK) {}
};

HRESULT CAgentFolder::CommonUpdateOperation(AGENT_OP operation, bool moveMode,
                                            const wchar_t *newItemName,
                                            const NUpdateArchive::CActionSet *actionSet,
                                            const UInt32 *indices, UInt32 numItems,
                                            IProgress *progress) {
    auto *publication = dynamic_cast<QtArchiveUpdatePublication *>(progress);
    bool publishedLocally = false;
    bool *published = publication ? &publication->Published.emplace(false) : &publishedLocally;
    if (!IsBound())
        return E_FAIL;
    if (moveMode && _agentSpec->_isHashHandler)
        return E_NOTIMPL;

    if (!_agentSpec->CanUpdate())
        return E_NOTIMPL;

    CMyComPtr<IFolderArchiveUpdateCallback> updateCallback100;
    if (progress)
        progress->QueryInterface(IID_IFolderArchiveUpdateCallback, (void **)&updateCallback100);

    auto writeLease =
        QtArchiveWriteLease::Acquire({QString::fromWCharArray(_agentSpec->_archiveFilePath)});
    QString error = writeLease ? QString{} : writeLease.error();
    if (writeLease)
        if (auto *resource = dynamic_cast<QtArchiveFileResource *>(_agentSpec->GetEngineResource()))
            if (const auto reader = resource->NativeReaderLease();
                reader && !reader->sourceIsCurrent())
                error = QCoreApplication::translate(
                    "QtOperation", "The archive changed on disk. Refresh it before editing.");
    if (!writeLease || !error.isEmpty()) {
        if (updateCallback100)
            updateCallback100->UpdateErrorMessage(error.toStdWString().c_str());
        return E_FAIL;
    }

    try {
        CMyComPtr<IUnknown> resourceOwner = _agentSpec->GetEngineResource();
        auto *resource = dynamic_cast<QtArchiveFileResource *>(resourceOwner.Interface());
        if (resource)
            resource->DiscardDeleteInputs();
        const auto discardDeleteInputs = qScopeGuard([&] {
            if (resource)
                resource->DiscardDeleteInputs();
        });

        RINOK(_agentSpec->SetFolder(this))

        // ---------- Save FolderItem ----------

        UStringVector pathParts;
        bool isAltStreamFolder = false;
        GetPathParts(pathParts, isAltStreamFolder);

        FStringVector requestedPaths;
        FStringVector processedPaths;

        CWorkDirTempFile tempFile;
        RINOK(tempFile.CreateTempFile(us2fs(_agentSpec->_archiveFilePath))) {
            CMyComPtr<IOutStream> tailStream;
            const CArc &arc = *_agentSpec->_archiveLink.GetArc();

            if (arc.ArcStreamOffset == 0)
                tailStream = tempFile.OutStream;
            else {
                if (arc.Offset < 0)
                    return E_NOTIMPL;
                RINOK(arc.InStream->Seek(0, STREAM_SEEK_SET, NULL))
                RINOK(NCompress::CopyStream_ExactSize(arc.InStream, tempFile.OutStream,
                                                      arc.ArcStreamOffset, NULL))
                CTailOutStream *tailStreamSpec = new CTailOutStream;
                tailStream = tailStreamSpec;
                tailStreamSpec->Stream = tempFile.OutStream;
                tailStreamSpec->Offset = arc.ArcStreamOffset;
                tailStreamSpec->Init();
            }

            HRESULT result;

            switch ((int)operation) {
            case AGENT_OP_Delete:
                result = _agentSpec->DeleteItems(tailStream, indices, numItems, updateCallback100);
                break;
            case AGENT_OP_CreateFolder:
                result = _agentSpec->CreateFolder(tailStream, newItemName, updateCallback100);
                break;
            case AGENT_OP_Rename:
                result = _agentSpec->RenameItem(tailStream, indices, numItems, newItemName,
                                                updateCallback100);
                break;
            case AGENT_OP_CopyFromFile:
                result = _agentSpec->UpdateOneFile(tailStream, indices, numItems, newItemName,
                                                   updateCallback100);
                break;
            case AGENT_OP_Uni: {
                Byte actionSetByte[NUpdateArchive::NPairState::kNumValues];
                for (unsigned i = 0; i < NUpdateArchive::NPairState::kNumValues; i++)
                    actionSetByte[i] = (Byte)actionSet->StateActions[i];
                result =
                    _agentSpec->DoOperation(&requestedPaths, &processedPaths, NULL, -1, tailStream,
                                            actionSetByte, NULL, updateCallback100);
                break;
            }
            default:
                return E_FAIL;
            }

            RINOK(result)
            if (resource) {
                RINOK(resource->ApplyOutputMTime(tempFile))
            }
        }

        _agentSpec->KeepModeForNextOpen();
        _agentSpec->Close();

        // before 9.26: if there was error for MoveToOriginal archive was closed.
        // now: we reopen archive after close

        // m_FolderItem = NULL;
        _items.Clear();
        _proxyDirIndex = k_Proxy_RootDirIndex;

        CMyComPtr<IFolderArchiveUpdateCallback_MoveArc> updateCallback_MoveArc;
        if (progress)
            progress->QueryInterface(IID_IFolderArchiveUpdateCallback_MoveArc,
                                     (void **)&updateCallback_MoveArc);

        HRESULT res;
        HRESULT ownedTempResult = S_OK;
        const auto adoptPublishedTemp = [&] {
            if (resource)
                ownedTempResult = resource->AdoptPublishedOwnedTemp(tempFile);
        };
        if (updateCallback_MoveArc) {
            const FString &tempFilePath = tempFile.Get_TempFilePath();
            UInt64 totalSize = 0;
            {
                NFind::CFileInfo fi;
                if (fi.Find(tempFilePath))
                    totalSize = fi.Size;
            }
            RINOK(updateCallback_MoveArc->MoveArc_Start(
                fs2us(tempFilePath), fs2us(tempFile.Get_OriginalFilePath()), totalSize,
                1)) // updateMode

            C_CopyFileProgress_to_FolderCallback_MoveArc prox(updateCallback_MoveArc);
            res = tempFile.MoveToOriginal(true, // deleteOriginal
                                          &prox, published);
            if (res == S_OK) {
                adoptPublishedTemp();
                res = updateCallback_MoveArc->MoveArc_Finish();
                // we don't return after E_ABORT here, because
                // we want to reopen new archive still.
            } else if (prox.CallbackResult != S_OK)
                res = prox.CallbackResult;

            // if updating callback returned E_ABORT,
            // then openCallback still can return E_ABORT also.
            // So ReOpen() will return with E_ABORT.
            // But we want to open archive still.
            // And Before_ArcReopen() call will clear user break status in that case.
            RINOK(updateCallback_MoveArc->Before_ArcReopen())
        } else {
            res = tempFile.MoveToOriginal(true, nullptr, published); // deleteOriginal
            if (res == S_OK)
                adoptPublishedTemp();
        }
        if (res != S_OK && res != E_ABORT && !*published && updateCallback100) {
            UString message = QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot replace the archive"));
            message.Add_LF();
            message += fs2us(tempFile.Get_OriginalFilePath());
            updateCallback100->UpdateErrorMessage(message);
        }
        // Preserve native Finish/Before_ArcReopen and reopening even when the
        // ownership adapter fails after a successful publication.
        if (res == S_OK && ownedTempResult != S_OK)
            res = ownedTempResult;

        // RINOK(res);
        if (res == S_OK) {
            if (resource) {
                QtUpdateDeletionCallback callback;
                callback.Progress = [updateCallback100](const FString &path, bool) {
                    return updateCallback100 ? updateCallback100->DeleteOperation(fs2us(path))
                                             : S_OK;
                };
                res = resource->DeleteInputsAfterPublication(callback);
            }
            if (moveMode && res == S_OK) {
                unsigned i;
                for (i = 0; i < processedPaths.Size(); i++) {
                    DeleteFileAlways(processedPaths[i]);
                }
                for (i = 0; i < requestedPaths.Size(); i++) {
                    const FString &fs = requestedPaths[i];
                    if (NFind::DoesDirExist(fs))
                        Delete_EmptyFolder_And_EmptySubFolders(fs);
                }
            }
        }

        {
            CMyComPtr<IArchiveOpenCallback> openCallback;
            if (updateCallback100)
                updateCallback100->QueryInterface(IID_IArchiveOpenCallback, (void **)&openCallback);
            RINOK(_agentSpec->ReOpen(openCallback))
        }

        // CAgent::ReOpen() deletes _proxy and _proxy2
        // _items.Clear();
        _proxy = NULL;
        _proxy2 = NULL;
        // _proxyDirIndex = k_Proxy_RootDirIndex;
        _isAltStreamFolder = false;

        // ---------- Restore FolderItem ----------

        CMyComPtr<IFolderFolder> archiveFolder;
        RINOK(_agentSpec->BindToRootFolder(&archiveFolder))

        // CAgent::BindToRootFolder() changes _proxy and _proxy2
        _proxy = _agentSpec->_proxy;
        _proxy2 = _agentSpec->_proxy2;
        _proxyGeneration = _agentSpec->_proxyGeneration.load();

        RestorePath(pathParts, isAltStreamFolder);

        return res;

    } catch (const UString &s) {
        if (updateCallback100) {
            UString s2 = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error: "));
            s2 += s;
            RINOK(updateCallback100->UpdateErrorMessage(s2))
            return E_FAIL;
        }
        throw;
    }
}

// clang-format off: preserve 7-Zip's COM_TRY/RINOK macro statement boundaries.
HRESULT CAgentFolder::CopyFrom(Int32 moveMode, const wchar_t *fromFolderPath,
                               const wchar_t *const *itemsPaths, UInt32 numItems,
                               IProgress *progress) throw() {
    COM_TRY_BEGIN
    if (!IsBound())
        return E_FAIL;
    {
        RINOK(_agentSpec->SetFiles(fromFolderPath, itemsPaths, numItems))
        return CommonUpdateOperation(AGENT_OP_Uni, (moveMode != 0), NULL,
                                     &NUpdateArchive::k_ActionSet_Add, NULL, 0, progress);
    }
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::UpdateFrom(const wchar_t *fromFolderPath,
                                     const wchar_t *const *itemsPaths, UInt32 numItems,
                                     const NUpdateArchive::CActionSet &actionSet,
                                     IProgress *progress)) {
    COM_TRY_BEGIN
    if (!IsBound())
        return E_FAIL;
    RINOK(_agentSpec->SetFiles(fromFolderPath, itemsPaths, numItems))
    return CommonUpdateOperation(AGENT_OP_Uni, false, NULL, &actionSet, NULL, 0, progress);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::UpdateFromPaths(const wchar_t *const *itemsPaths, UInt32 numItems,
                                          const NUpdateArchive::CActionSet &actionSet,
                                          NWildcard::ECensorPathMode pathMode,
                                          IProgress *progress)) {
    COM_TRY_BEGIN
    if (!IsBound())
        return E_FAIL;
    RINOK(_agentSpec->SetFiles(L"", itemsPaths, numItems))
    const auto previous = _agentSpec->GuiPathMode;
    _agentSpec->GuiPathMode = pathMode;
    const auto restore = qScopeGuard([&] { _agentSpec->GuiPathMode = previous; });
    return CommonUpdateOperation(AGENT_OP_Uni, false, NULL, &actionSet, NULL, 0, progress);
    COM_TRY_END
}
// clang-format on

Z7_COM7F_IMF(CAgentFolder::CopyFromFile(UInt32 destIndex, const wchar_t *itemPath,
                                        IProgress *progress)) {
    COM_TRY_BEGIN
    return CommonUpdateOperation(AGENT_OP_CopyFromFile, false, itemPath,
                                 &NUpdateArchive::k_ActionSet_Add, &destIndex, 1, progress);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::Delete(const UInt32 *indices, UInt32 numItems, IProgress *progress)) {
    COM_TRY_BEGIN
    return CommonUpdateOperation(AGENT_OP_Delete, false, NULL, &NUpdateArchive::k_ActionSet_Delete,
                                 indices, numItems, progress);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::CreateFolder(const wchar_t *name, IProgress *progress)) {
    COM_TRY_BEGIN
    if (!IsBound())
        return E_FAIL;
    if (_isAltStreamFolder)
        return E_NOTIMPL;
    if (_proxy2) {
        if (_proxy2->IsThere_SubDir(_proxyDirIndex, name))
            return ERROR_ALREADY_EXISTS;
    } else {
        if (_proxy->FindSubDir(_proxyDirIndex, name) != -1)
            return ERROR_ALREADY_EXISTS;
    }
    return CommonUpdateOperation(AGENT_OP_CreateFolder, false, name, NULL, NULL, 0, progress);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::Rename(UInt32 index, const wchar_t *newName, IProgress *progress)) {
    COM_TRY_BEGIN
    return CommonUpdateOperation(AGENT_OP_Rename, false, newName, NULL, &index, 1, progress);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::SetProperty(UInt32, PROPID, const PROPVARIANT *, IProgress *)) {
    return E_NOTIMPL;
}
