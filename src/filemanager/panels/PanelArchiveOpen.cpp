// Archive opening adapted from 7-Zip UI/FileManager/PanelItemOpen.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/Common/StreamObjects.h"
#include "7zip/UI/Common/ExtractMode.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariantConv.h"
#include "archive/operations/ExtractingFilePath.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/qtarchiveitemopen.h"
#include "archive/resources/virtualextractfilesystem.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMessageBox>
#include <memory>

using namespace NWindows;
using namespace NFile;
using namespace NDir;
static const FChar *const kTempDirPrefix = FTEXT("7zO");

HRESULT CPanel::OpenAsArc(IInStream *inStream, const CTempFileInfo &tempFileInfo,
                          const UString &virtualFilePath, const UString &arcFormat,
                          COpenResult &openRes, bool materializeStream) {
    openRes.Encrypted = false;
    CFolderLink folderLink;
    (CTempFileInfo &)folderLink = tempFileInfo;
    if (inStream)
        folderLink.IsVirtual = true;
    else {
        if (!folderLink.FileInfo.Find(folderLink.FilePath))
            return GetLastError_noZero_HRESULT();
        if (folderLink.FileInfo.IsDir())
            return S_FALSE;
        folderLink.IsVirtual = false;
    }
    folderLink.VirtualPath = virtualFilePath;
    CFfpOpen ffp(_folderManager);
    ffp.SettingsPath = _settingsPath;
    ffp.OwnedStreamFile = inStream && tempFileInfo.NeedDelete && !tempFileInfo.FilePath.IsEmpty();
    ffp.MaterializeStreamFile = materializeStream;
    const HRESULT res = ffp.OpenFileFolderPlugin(
        inStream, folderLink.FilePath.IsEmpty() ? us2fs(virtualFilePath) : folderLink.FilePath,
        arcFormat, GetParent());
    openRes.Encrypted = ffp.Encrypted;
    openRes.ErrorMessage = ffp.ErrorMessage;
    RINOK(res)
    CMyComPtr<IArchiveLayerFolder> child;
    if (ffp.Folder.QueryInterface(IID_IArchiveLayerFolder, &child) == S_OK && child) {
        UInt64 parentDepth = 0;
        CMyComPtr<IArchiveLayerFolder> parent;
        if (_folder && _folder.QueryInterface(IID_IArchiveLayerFolder, &parent) == S_OK && parent) {
            const HRESULT contextResult = parent->GetContainerDepth(&parentDepth);
            if (FAILED(contextResult))
                return contextResult;
        }
        const HRESULT contextResult = child->SetParentContainerDepth(parentDepth);
        if (FAILED(contextResult))
            return contextResult;
    }
    if (ffp.MaterializedStreamFile) {
        if (!folderLink.FileInfo.Find(folderLink.FilePath))
            return GetLastError_noZero_HRESULT();
        folderLink.IsVirtual = false;
    }
    folderLink.Password = ffp.Password;
    folderLink.UsePassword = ffp.Encrypted;
    if (_folder)
        folderLink.ParentFolderPath = GetFolderPath(_folder);
    else
        folderLink.ParentFolderPath = _currentFolderPrefix;
    if (!_parentFolders.IsEmpty())
        folderLink.ParentFolder = _folder;
    _parentFolders.Add(folderLink);
    _parentFolders.Back().Library.Attach(_library.Detach());
    ReleaseFolder();
    _library.Free();
    SetNewFolder(ffp.Folder);
    _library.Attach(ffp.Library.Detach());
    _flatMode = _flatModeForArc;
    _thereAreDeletedItems = false;
    if (!openRes.ErrorMessage.IsEmpty())
        MessageBox_Error(openRes.ErrorMessage);
    return S_OK;
}

HRESULT CPanel::OpenAsArc_Msg(IInStream *inStream, const CTempFileInfo &tempFileInfo,
                              const UString &virtualFilePath, const UString &arcFormat,
                              bool materializeStream) {
    COpenResult opRes;
    HRESULT res =
        OpenAsArc(inStream, tempFileInfo, virtualFilePath, arcFormat, opRes, materializeStream);
    if (res == S_OK)
        return res;
    if (res == E_ABORT)
        return res;
    if (opRes.Encrypted || res != S_FALSE) {
        UString message;
        if (res == S_FALSE)
            message = MyFormatNew(opRes.Encrypted ? IDS_CANT_OPEN_ENCRYPTED_ARCHIVE
                                                  : IDS_CANT_OPEN_ARCHIVE,
                                  virtualFilePath);
        else
            message = HResultToMessage(res);
        MessageBox_Error(message);
    }
    return res;
}

HRESULT CPanel::OpenAsArc_Name(const UString &relPath, const UString &arcFormat) {
    CTempFileInfo tfi;
    tfi.RelPath = relPath;
    tfi.FolderPath = us2fs(GetFsPath());
    const UString fullPath = GetFsPath() + relPath;
    tfi.FilePath = us2fs(fullPath);
    return OpenAsArc_Msg(NULL, tfi, fullPath, arcFormat);
}

HRESULT CPanel::OpenParentArchiveFolder() {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    if (_parentFolders.Size() < 2)
        return S_OK;
    const CFolderLink &folderLinkPrev = _parentFolders[_parentFolders.Size() - 2];
    const CFolderLink &folderLink = _parentFolders.Back();
    NFind::CFileInfo newFileInfo;
    if (newFileInfo.Find(folderLink.FilePath)) {
        if (folderLink.WasChanged_from_FolderLink(newFileInfo)) {
            const UString message = MyFormatNew(IDS_WANT_UPDATE_MODIFIED_FILE, folderLink.RelPath);
            QMessageBox box(QMessageBox::Question, QStringLiteral("7-Zip"),
                            QString::fromWCharArray(message.Ptr()),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, GetParent());
            box.setObjectName("modifiedArchiveQuestion");
            box.setTextFormat(Qt::PlainText);
            box.setDefaultButton(QMessageBox::Yes);
            box.setEscapeButton(QMessageBox::Cancel);
            if (box.exec() == QMessageBox::Yes) {
                if (OnOpenItemChanged(folderLink.FileIndex, fs2us(folderLink.FilePath),
                                      folderLinkPrev.UsePassword,
                                      folderLinkPrev.Password) != S_OK) {
                    MessageBox_Error(
                        MyFormatNew(IDS_CANNOT_UPDATE_FILE, fs2us(folderLink.FilePath)));
                    return S_OK;
                }
            }
        }
    }
    folderLink.DeleteDirAndFile();
    return S_OK;
}

HRESULT CPanel::OpenAsArc_Index(unsigned index, const wchar_t *type) {
    HRESULT res;
    {
        CDisableTimerProcessing disableTimerProcessing1(*this);
        CDisableNotify disableNotify(*this);
        res = OpenAsArc_Name(GetItemRelPath2(index), type ? type : L"");
    }
    if (res != S_OK) {
        RefreshTitle(true); // Restore the changed 7zFM title after an open error.
        return res;
    }
    // Refresh owns its own guards, retained by Qt when publication is deferred.
    // Do not let those guards capture the disabled flags of the open call.
    RefreshListCtrl();
    return S_OK;
}

void CPanel::OpenItemInArchive(unsigned index, bool tryInternal, bool tryExternal, bool editMode,
                               bool useEditor, const wchar_t *type) {
    std::optional<CDisableTimerProcessing> disableTimerProcessing(std::in_place, *this);
    // we don't want to change hash data here
    if (IsHashFolder())
        return;

    const UString name = GetItemName(index);
    const UString relPath = GetItemRelPath(index);

    if (tryExternal)
        if (IsVirus_Message(name))
            return;

    if (!_folderOperations) {
        MessageBox_Error_UnsupportOperation();
        return;
    }

    bool tryAsArchive = tryInternal && (!tryExternal || !DoItemAlwaysStart(name));

    const UString fullVirtPath = _currentFolderPrefix + relPath;

    CTempDir tempDirectory;
    if (!tempDirectory.Create(kTempDirPrefix)) {
        MessageBox_LastError();
        return;
    }

    FString tempDir = tempDirectory.GetPath();
    FString tempDirNorm = tempDir;
    NName::NormalizeDirPathPrefix(tempDirNorm);
    const FString tempFilePath = tempDirNorm + us2fs(Get_Correct_FsFile_Name(name));

    CTempFileInfo tempFileInfo;
    tempFileInfo.OwnedTempLease = tempDirectory.OwnedLease();
    tempFileInfo.FileIndex = index;
    tempFileInfo.RelPath = relPath;
    tempFileInfo.FolderPath = tempDir;
    tempFileInfo.FilePath = tempFilePath;
    tempFileInfo.NeedDelete = true;

    if (tryAsArchive) {
        CMyComPtr<IInArchiveGetStream> getStream;
        _folder.QueryInterface(IID_IInArchiveGetStream, &getStream);
        if (getStream) {
            CMyComPtr<ISequentialInStream> subSeqStream;
            bool defined = !_parentFolders.IsEmpty() && _parentFolders.Back().UsePassword;
            UString password = defined ? _parentFolders.Back().Password : UString();
            const HRESULT acquired = GetArchiveItemStream(
                _folder, index, &subSeqStream, GetParent(), _settingsPath, defined, password);
            if (!_parentFolders.IsEmpty()) {
                _parentFolders.Back().UsePassword = defined;
                _parentFolders.Back().Password = password;
            }
            if (acquired == E_ABORT)
                return;
            if (acquired != S_OK && acquired != S_FALSE && acquired != E_NOTIMPL) {
                MessageBox_Error_HRESULT(acquired);
                return;
            }
            if (subSeqStream) {
                CMyComPtr<IInStream> subStream;
                subSeqStream.QueryInterface(IID_IInStream, &subStream);
                if (subStream) {
                    HRESULT res =
                        OpenAsArc_Msg(subStream, tempFileInfo, fullVirtPath, type ? type : L""
                                      // , true // showErrorMessage
                        );
                    if (res == S_OK) {
                        tempDirectory.DisableDeleting();
                        disableTimerProcessing.reset();
                        RefreshListCtrl();
                        return;
                    }
                    if (res == E_ABORT || res != S_FALSE)
                        return;
                    if (!tryExternal)
                        return;
                    tryAsArchive = false;
                }
            }
        }
    }

    CRecordVector<UInt32> indices;
    indices.Add(index);

    UStringVector messages;

    bool usePassword = false;
    UString password;
    if (!_parentFolders.IsEmpty()) {
        const CFolderLink &fl = _parentFolders.Back();
        usePassword = fl.UsePassword;
        password = fl.Password;
    }

    /*
    #if defined(_WIN32) && !defined(UNDER_CE)
    CByteBuffer zoneBuf;
    #ifndef _UNICODE
    if (g_IsNT)
    #endif
    if (!_parentFolders.IsEmpty())
    {
      const CFolderLink &fl = _parentFolders.Front();
      if (!fl.IsVirtual && !fl.FilePath.IsEmpty())
        ReadZoneFile(fl.FilePath + k_ZoneId_StreamName, zoneBuf);
    }
    #endif
    */

    CVirtFileSystem *virtFileSystemSpec = NULL;
    CMyComPtr<ISequentialOutStream> virtFileSystem;

    const bool isAltStream = IsItem_AltStream(index);

    CCopyToOptions options;
    options.includeAltStreams = true;
    options.replaceAltStreamChars = isAltStream;
    {
        // CContextMenuInfo ci;
        // ci.Load();
        // if (ci.WriteZone != (UInt32)(Int32)-1)
        // we use kAll when we unpack just one file.
        options.ZoneIdMode = NExtract::NZoneIdMode::kAll;
        options.NeedRegistryZone = false;
    }

    const auto &ram = NQtFileManager::GetRamInfo();
    const size_t g_RAM_Size = ram.Size;
    const bool g_RAM_Size_Defined = ram.Defined;
    if (tryAsArchive) {
        // actually we want to get sum: size of main file plus sizes of altStreams.
        // but now there is no interface to get altStreams sizes.
        NCOM::CPropVariant prop;
        _folder->GetProperty(index, kpidSize, &prop);
        const size_t fileLimit = g_RAM_Size_Defined
                                     ? (MyMax(_parentFolders.Size() + 1, 8u) < sizeof(size_t) * 8
                                            ? g_RAM_Size >> MyMax(_parentFolders.Size() + 1, 8u)
                                            : 0)
                                     : 1u << 22;
        UInt64 fileSize = 0;
        if (!ConvertPropVariantToUInt64(prop, fileSize))
            fileSize = fileLimit;
#if 0 // 1 : for debug
    fileLimit = 1;
#endif

        if (fileSize <= fileLimit) {
            options.streamMode = true;
            virtFileSystemSpec = new CVirtFileSystem;
            virtFileSystem = virtFileSystemSpec;
            virtFileSystemSpec->FileName = name;
            virtFileSystemSpec->IsAltStreamFile = isAltStream;

#if defined(_WIN32) && !defined(UNDER_CE)
#ifndef _UNICODE
            if (g_IsNT)
#endif
            {
                Get_ZoneId_Stream_from_ParentFolders(virtFileSystemSpec->ZoneBuf);
                options.ZoneBuf = virtFileSystemSpec->ZoneBuf;
            }
#endif

            virtFileSystemSpec->MaxTotalAllocSize =
                (size_t)fileSize +
                (1 << 16); // we allow additional total size for small alt streams.
            virtFileSystemSpec->DirPrefix = tempDirNorm;
            // options.VirtFileSystem = virtFileSystem;
            options.VirtFileSystemSpec = virtFileSystemSpec;
        }
    }

    options.folder = fs2us(tempDirNorm);
    options.showErrorMessages = true;

    const HRESULT result = CopyTo(options, indices, &messages, usePassword, password);

    if (!_parentFolders.IsEmpty()) {
        CFolderLink &fl = _parentFolders.Back();
        fl.UsePassword = usePassword;
        fl.Password = password;
    }

    if (!messages.IsEmpty())
        return;
    if (result != S_OK) {
        if (result != E_ABORT)
            MessageBox_Error_HRESULT(result);
        return;
    }

    if (virtFileSystemSpec && !virtFileSystemSpec->WasStreamFlushedToFS()) {
        int index_in_Files = virtFileSystemSpec->Index_of_MainExtractedFile_in_Files;
        if (index_in_Files < 0) {
            if (virtFileSystemSpec->Files.Size() != 1) {
                MessageBox_Error_HRESULT(E_FAIL);
                return;
            }
            // it's not expected case that index was not set, but we support that case
            index_in_Files = 0;
        }
        {
            const CVirtFile &file = virtFileSystemSpec->Files[index_in_Files];
            CMyComPtr2_Create<IInStream, CBufInStream> bufInStream;
            bufInStream->Init(file.Data, file.WrittenSize, virtFileSystem);
            const HRESULT res =
                OpenAsArc_Msg(bufInStream, tempFileInfo, fullVirtPath, type ? type : L"", true);
            if (res == S_OK) {
                if (virtFileSystemSpec->Index_of_ZoneBuf_AltStream_in_Files >= 0 &&
                    !_parentFolders.IsEmpty()) {
                    const CVirtFile &fileZone =
                        virtFileSystemSpec
                            ->Files[virtFileSystemSpec->Index_of_ZoneBuf_AltStream_in_Files];
                    _parentFolders.Back().ZoneBuf.CopyFrom(fileZone.Data, fileZone.WrittenSize);
                }

                tempDirectory.DisableDeleting();
                disableTimerProcessing.reset();
                RefreshListCtrl();
                return;
            }
            if (res == E_ABORT || res != S_FALSE)
                return;
            if (!tryExternal)
                return;
            tryAsArchive = false;
            if (virtFileSystemSpec->FlushToDisk(true) != S_OK)
                return;
        }
    }

    /*
    #if defined(_WIN32) && !defined(UNDER_CE)
    if (zoneBuf.Size() != 0)
    {
      if (NFind::DoesFileExist_Raw(tempFilePath))
      {
        WriteZoneFile(tempFilePath + k_ZoneId_StreamName, zoneBuf);
      }
    }
    #endif
    */

    if (tryAsArchive) {
        const HRESULT res = OpenAsArc_Msg(NULL, tempFileInfo, fullVirtPath, type ? type : L""
                                          // , encrypted
                                          // , true // showErrorMessage
        );
        if (res == S_OK) {
            tempDirectory.DisableDeleting();
            disableTimerProcessing.reset();
            RefreshListCtrl();
            return;
        }
        if (res == E_ABORT || res != S_FALSE)
            return;
    }

    if (!tryExternal)
        return;

    if (!_externalFileServices) {
        MessageBox_Error_UnsupportOperation();
        return;
    }
    if (tryInternal && !editMode &&
        !_externalFileServices->ConfirmArchiveFallback(fs2us(tempFilePath), GetParent(),
                                                       _settingsPath))
        return;
    StartExternalFileSession(tempDirectory, tempFileInfo, tempDirNorm, usePassword, password,
                             editMode, useEditor);
}
