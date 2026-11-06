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
#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchiveitemopen.h"
#include "archive/resources/virtualextractfilesystem.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include "ui/progress/ProgressDialog2.h"
#include <memory>

using namespace NWindows;
using namespace NFile;
using namespace NDir;
static const FChar *const kTempDirPrefix = FTEXT("7zO");

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
    (void)OpenItemInArchiveImpl(index, tryInternal, tryExternal, editMode, useEditor, type);
}

HRESULT CPanel::OpenItemInArchiveInternal(unsigned index, const wchar_t *type,
                                          bool materializeStream) {
    return OpenItemInArchiveImpl(index, true, false, false, false, type, materializeStream);
}

HRESULT CPanel::OpenItemInArchiveImpl(unsigned index, bool tryInternal, bool tryExternal,
                                      bool editMode, bool useEditor, const wchar_t *type,
                                      bool materializeStream) {
    std::optional<CDisableTimerProcessing> disableTimerProcessing(std::in_place, *this);
    // we don't want to change hash data here
    if (IsHashFolder())
        return S_FALSE;

    const UString name = GetItemName(index);
    const UString relPath = GetItemRelPath(index);

    if (tryExternal)
        if (IsVirus_Message(name))
            return E_ABORT;

    if (!_folderOperations) {
        MessageBox_Error_UnsupportOperation();
        return E_NOTIMPL;
    }

    bool tryAsArchive = tryInternal && (!tryExternal || !DoItemAlwaysStart(name));

    const UString fullVirtPath = _currentFolderPrefix + relPath;

    CTempDir tempDirectory;
    if (!tempDirectory.Create(kTempDirPrefix)) {
        const HRESULT result = GetLastError_noZero_HRESULT();
        MessageBox_LastError();
        return result;
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
            NativePassword password =
                defined ? _parentFolders.Back().Password.native() : NativePassword();
            const HRESULT acquired =
                GetArchiveItemStream(_folder, index, &subSeqStream, GetParent(), _settingsPath,
                                     defined, password, !_archiveOpenDialogs);
            if (!_parentFolders.IsEmpty()) {
                _parentFolders.Back().UsePassword = defined;
                _parentFolders.Back().Password = ArchivePassword(password);
            }
            if (acquired == E_ABORT)
                return acquired;
            if (acquired != S_OK && acquired != S_FALSE && acquired != E_NOTIMPL) {
                MessageBox_Error_HRESULT(acquired);
                return acquired;
            }
            if (subSeqStream) {
                CMyComPtr<IInStream> subStream;
                subSeqStream.QueryInterface(IID_IInStream, &subStream);
                if (subStream) {
                    HRESULT res = OpenAsArc_Msg(subStream, tempFileInfo, fullVirtPath,
                                                type ? type : L"", materializeStream);
                    if (res == S_OK) {
                        tempDirectory.DisableDeleting();
                        disableTimerProcessing.reset();
                        return RefreshListCtrl();
                    }
                    if (res == E_ABORT || res != S_FALSE)
                        return res;
                    if (!tryExternal)
                        return res;
                    tryAsArchive = false;
                }
            }
        }
    }

    CRecordVector<UInt32> indices;
    indices.Add(index);

    UStringVector messages;

    bool usePassword = false;
    NativePassword password;
    if (!_parentFolders.IsEmpty()) {
        const CFolderLink &fl = _parentFolders.Back();
        usePassword = fl.UsePassword;
        password = fl.Password.native();
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

    CMyComPtr2<ISequentialOutStream, CVirtFileSystem> virtFileSystem;

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
            virtFileSystem.Create_if_Empty();
            virtFileSystem->FileName = name;
            virtFileSystem->IsAltStreamFile = isAltStream;

#if defined(_WIN32) && !defined(UNDER_CE)
#ifndef _UNICODE
            if (g_IsNT)
#endif
            {
                Get_ZoneId_Stream_from_ParentFolders(virtFileSystem->ZoneBuf);
                options.ZoneBuf = virtFileSystem->ZoneBuf;
            }
#endif

            virtFileSystem->MaxTotalAllocSize =
                (size_t)fileSize +
                (1 << 16); // we allow additional total size for small alt streams.
            virtFileSystem->DirPrefix = tempDirNorm;
            // options.VirtFileSystem = virtFileSystem;
            options.VirtFileSystemSpec = virtFileSystem.ClsPtr();
        }
    }

    options.folder = fs2us(tempDirNorm);
    options.showErrorMessages = _archiveOpenDialogs;
    options.DisableUserQuestions = !_archiveOpenDialogs;

    const HRESULT result = CopyTo(options, indices, &messages, usePassword, password);

    if (!_parentFolders.IsEmpty()) {
        CFolderLink &fl = _parentFolders.Back();
        fl.UsePassword = usePassword;
        fl.Password = ArchivePassword(password);
    }

    if (!messages.IsEmpty())
        return result == S_OK ? E_FAIL : result;
    if (result != S_OK) {
        if (result != E_ABORT)
            MessageBox_Error_HRESULT(result);
        return result;
    }

    if (virtFileSystem && !virtFileSystem->WasStreamFlushedToFS()) {
        int index_in_Files = virtFileSystem->Index_of_MainExtractedFile_in_Files;
        if (index_in_Files < 0) {
            if (virtFileSystem->Files.Size() != 1) {
                MessageBox_Error_HRESULT(E_FAIL);
                return E_FAIL;
            }
            // it's not expected case that index was not set, but we support that case
            index_in_Files = 0;
        }
        {
            const CVirtFile &file = virtFileSystem->Files[index_in_Files];
            CMyComPtr2_Create<IInStream, CBufInStream> bufInStream;
            bufInStream->Init(file.Data, file.WrittenSize, virtFileSystem);
            const HRESULT res =
                OpenAsArc_Msg(bufInStream, tempFileInfo, fullVirtPath, type ? type : L"", true);
            if (res == S_OK) {
                if (virtFileSystem->Index_of_ZoneBuf_AltStream_in_Files >= 0 &&
                    !_parentFolders.IsEmpty()) {
                    const CVirtFile &fileZone =
                        virtFileSystem->Files[virtFileSystem->Index_of_ZoneBuf_AltStream_in_Files];
                    _parentFolders.Back().ZoneBuf.CopyFrom(fileZone.Data, fileZone.WrittenSize);
                }

                tempDirectory.DisableDeleting();
                disableTimerProcessing.reset();
                return RefreshListCtrl();
            }
            if (res == E_ABORT || res != S_FALSE)
                return res;
            if (!tryExternal)
                return res;
            tryAsArchive = false;
            const HRESULT flushed = virtFileSystem->FlushToDisk(true);
            if (flushed != S_OK)
                return flushed;
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
            return RefreshListCtrl();
        }
        if (res == E_ABORT || res != S_FALSE)
            return res;
    }

    if (!tryExternal)
        return S_FALSE;

    if (!_externalFileServices) {
        MessageBox_Error_UnsupportOperation();
        return E_NOTIMPL;
    }
    if (tryInternal && !editMode &&
        !_externalFileServices->ConfirmArchiveFallback(fs2us(tempFilePath), GetParent(),
                                                       _settingsPath))
        return E_ABORT;
    StartExternalFileSession(tempDirectory, tempFileInfo, tempDirNorm, usePassword, password,
                             editMode, useEditor);
    return S_OK;
}
