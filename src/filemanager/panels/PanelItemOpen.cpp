// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItemOpen.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source archive-link/update bodies; Qt presentation and typed manager boundary.
#include "7zip/Common/StreamObjects.h"
#include "7zip/UI/Common/ExtractMode.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/AutoPtr.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileName.h"
#include "Windows/PropVariantConv.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/operations/ExtractingFilePath.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/qtarchiveitemopen.h"
#include "archive/resources/virtualextractfilesystem.h"
#include "filemanager/App.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "localization/LangUtils.h"
#include "platform/temp/qtownedtempregistry.h"
#include "settings/RegistryUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMessageBox>
#include <QWidget>
#include <memory>
#include <utility>

using namespace NWindows;
using namespace NFile;
using namespace NDir;
static const FChar *const kTempDirPrefix = FTEXT("7zO");
static const UInt32 kParentIndex = (UInt32)-1;

void CPanel::MessageBox_LastError() const {
    MessageBox_Error_HRESULT(GetLastError_noZero_HRESULT());
}

QWidget *CPanel::GetParent() const {
    if (_parentWindow)
        return _parentWindow;
    return _listView ? _listView->window() : _mainWindow.data();
}

void CPanel::MessageBox_Error_UnsupportOperation() const {
    MessageBox_Error(LangString(IDS_OPERATION_IS_NOT_SUPPORTED));
}

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
        if (folderLink.FileInfo.IsDir() && !IsSparseBundleName(virtualFilePath))
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

static const char *const kExeExtensions = " exe bat ps1 com lnk"
                                          " ";

static const char *const kStartExtensions =
#ifdef UNDER_CE
    " cab"
#endif
    " exe bat ps1 com lnk"
    " chm"
    " msi doc dot xls ppt pps wps wpt wks xlr wdb vsd pub"

    " docx docm dotx dotm xlsx xlsm xltx xltm xlsb xps"
    " xlam pptx pptm potx potm ppam ppsx ppsm vsdx xsn"
    " mpp"
    " msg"
    " dwf"

    " flv swf"

    " epub"
    " odt ods"
    " wb3"
    " pdf"
    " ps"
    " txt"
    " xml xsd xsl xslt hxk hxc htm html xhtml xht mht mhtml htw asp aspx css cgi jsp shtml"
    " h hpp hxx c cpp cxx m mm go swift"
    " awk sed hta js json php php3 php4 php5 phptml pl pm py pyo rb tcl ts vbs"
    " asm"
    " mak clw csproj vcproj sln dsp dsw"
    " ";

// bool FindExt(const char *p, const UString &name, AString &s);
bool FindExt(const char *p, const UString &name, CStringFinder &finder);

static bool DoItemAlwaysStart(const UString &name) {
    CStringFinder finder;
    return FindExt(kStartExtensions, name, finder);
}

void CPanel::OpenFolderExternal(unsigned index) {
    UString prefix = GetFsPath();
    UString path = prefix;
    if (index == kParentIndex) {
        if (prefix.IsEmpty())
            return;
        const wchar_t c = prefix.Back();
        if (!IS_PATH_SEPAR(c) && c != ':')
            return;
        prefix.DeleteBack();
        int pos = prefix.ReverseFind_PathSepar();
        if (pos < 0)
            return;
        prefix.DeleteFrom((unsigned)(pos + 1));
        path = prefix;
    } else {
        path += GetItemRelPath(index);
        path.Add_PathSepar();
    }
    // This disk/folder caller closes any returned native handle without waiting.
    if (_externalFileServices)
        _externalFileServices->StartApplication(prefix, path, GetParent());
    else
        MessageBox_Error_UnsupportOperation();
}

void CPanel::OpenItem(unsigned index, bool tryInternal, bool tryExternal, const wchar_t *type) {
    std::optional<CDisableTimerProcessing> disableTimerProcessing(std::in_place, *this);
    const UString name = GetItemRelPath2(index);
    if (tryExternal)
        if (IsVirus_Message(name))
            return;
    if (!_parentFolders.IsEmpty()) {
        // The nested opener retains this guard until its own refresh handoff.
        disableTimerProcessing.reset();
        OpenItemInArchive(index, tryInternal, tryExternal, false, false, type);
        return;
    }
    std::optional<CDisableNotify> disableNotify(std::in_place, *this);
    UString prefix = GetFsPath();
    UString fullPath = prefix + name;
    if (tryInternal)
        if (!tryExternal || !DoItemAlwaysStart(name)) {
            // OpenAsArc_Index guards binding itself; Refresh must retain the
            // caller's enabled state when its Qt rows finish asynchronously.
            disableNotify.reset();
            disableTimerProcessing.reset();
            HRESULT res = OpenAsArc_Index(index, type);
            if (res != S_OK)
                disableTimerProcessing.emplace(*this);
            _selectionList.Invalidate();
            if (res == S_OK || res == E_ABORT)
                return;
            if (res != S_FALSE) {
                // OpenAsArc_Msg already presented this opening failure.
                return;
            }
        }
    if (tryExternal) {
        if (_externalFileServices)
            _externalFileServices->StartApplication(prefix, fullPath, GetParent());
        else
            MessageBox_Error_UnsupportOperation();
    }
}

void CPanel::EditItem(unsigned index, bool useEditor) {
    if (!_parentFolders.IsEmpty()) {
        OpenItemInArchive(index, false, true, true, useEditor);
        return;
    }
    if (!_externalFileServices) {
        MessageBox_Error_UnsupportOperation();
        return;
    }
    StartEditApplication(GetItemFullPath(index), useEditor, GetParent(), *_externalFileServices,
                         _settingsPath);
}

void CApp::DiffFiles() {
    const CPanel &panel = GetFocusedPanel();
    if (!panel.Is_IO_FS_Folder()) {
        panel.MessageBox_Error_UnsupportOperation();
        return;
    }
    CRecordVector<UInt32> indices;
    panel.Get_ItemIndices_Selected(indices);
    UString path1, path2;
    if (indices.Size() == 2) {
        path1 = panel.GetItemFullPath(indices[0]);
        path2 = panel.GetItemFullPath(indices[1]);
    } else if (indices.Size() == 1 && NumPanels >= 2) {
        const CPanel &destPanel = Panels[1 - LastFocusedPanel];
        if (!destPanel.Is_IO_FS_Folder()) {
            panel.MessageBox_Error_UnsupportOperation();
            return;
        }
        path1 = panel.GetItemFullPath(indices[0]);
        CRecordVector<UInt32> indices2;
        destPanel.Get_ItemIndices_Selected(indices2);
        if (indices2.Size() == 1)
            path2 = destPanel.GetItemFullPath(indices2[0]);
        else {
            UString relPath = panel.GetItemRelPath2(indices[0]);
            if (panel._flatMode && !destPanel._flatMode)
                relPath = panel.GetItemName(indices[0]);
            path2 = destPanel._currentFolderPrefix + relPath;
        }
    } else
        return;
    DiffFiles(path1, path2);
}

void CApp::DiffFiles(const UString &path1, const UString &path2) {
    UString command;
    ReadRegDiff(command, _settingsPath);
    if (command.IsEmpty())
        return;
    UStringVector params;
    params.Add(path1);
    params.Add(path2);
    auto *process = GetFocusedPanel()._externalFileServices;
    if (!process) {
        GetFocusedPanel().MessageBox_Error_UnsupportOperation();
        return;
    }
    const HRESULT res = StartAppWithParams(command, params, *process, _window);
    if (res == S_OK)
        return;
    GetFocusedPanel().MessageBox_Error(LangString(IDS_CANNOT_START_EDITOR));
}

bool CPanel::IsVirus_Message(const UString &name) {
    UString name2;

    const wchar_t cRLO = (wchar_t)0x202E;
    bool isVirus = false;
    bool isSpaceError = false;
    {
        unsigned numSpaces = 0;
        for (unsigned i = 0; i < name.Len(); i++) {
            const wchar_t c = name[i];
            switch (c) {
            case ' ':
            case '\t':
            case 0x00A0: // Non-Breaking Space
            case 0x2002: // En Space
            case 0x2003: // Em Space
            case 0x2004: // Three-Per-Em Space
            case 0x2005: // Four-Per-Em Space
            case 0x2006: // Six-Per-Em Space
            case 0x2007: // Figure Space
            case 0x2008: // Punctuation Space
            case 0x2009: // Thin Space
            case 0x200A: // Hair Space
            case 0x200B: // Zero Width Space
            case 0x202F: // Narrow Non-Breaking Space
            case 0x205F: // Medium Mathematical Space
            case 0x3000: // Ideographic Space
                numSpaces++;
                break;
            default:
                numSpaces = 0;
                break;
            }
            if (numSpaces <= 1)
                name2 += c;
            if (numSpaces >= 5) {
                isVirus = true;
                isSpaceError = true;
            }
        }
    }
    if (name2.Find(cRLO) >= 0) {
        const UString badString(cRLO);
        name2.Replace(badString, L"[RLO]");
        isVirus = true;
    }

#ifdef _WIN32
    {
        unsigned i;
        for (i = name2.Len(); i != 0;) {
            wchar_t c = name2[i - 1];
            if (c != '.' && c != ' ')
                break;
            i--;
            name2.ReplaceOneCharAtPos(i, '_');
        }
        if (i != name2.Len()) {
            CStringFinder finder;
            UString name3 = name2;
            name3.DeleteFrom(i);
            if (FindExt(kExeExtensions, name3, finder))
                isVirus = true;
        }
    }
#endif

    if (!isVirus)
        return false;

    UString s = LangString(IDS_VIRUS);

    if (!isSpaceError) {
        const int pos1 = s.Find(L'(');
        if (pos1 >= 0) {
            const int pos2 = s.Find(L')', (unsigned)pos1 + 1);
            if (pos2 >= 0) {
                s.Delete((unsigned)pos1, (unsigned)pos2 + 1 - (unsigned)pos1);
                if (pos1 > 0 && s[pos1 - 1] == ' ' && s[pos1] == '.')
                    s.Delete(pos1 - 1);
            }
        }
    }

    UString name3 = name;
    name3.Replace(L'\n', L'_');
    name2.Replace(L'\n', L'_');

    s.Add_LF();
    s += name2;
    s.Add_LF();
    s += name3;

    MessageBox_Error(s);
    return true;
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

// Native OnOpenItemChanged message body. A typed request replaces LPARAM and
// the window handle; the external service retains the temp file during the call.
bool CPanel::OnOpenItemChanged(const CTempFileInfo &tpi, const UString &fullPathFolderPrefix,
                               bool usePassword, const UString &password,
                               std::optional<CSelectedState> *refreshSelection) {
    if (refreshSelection)
        refreshSelection->reset();
    if (fullPathFolderPrefix != _currentFolderPrefix || !_folder)
        return false;
    UInt32 fileIndex = tpi.FileIndex;
    UInt32 numItems = 0;
    if (_folder->GetNumberOfItems(&numItems) != S_OK)
        return false;
    // Source limitation: duplicate names can still make a relocated index
    // ambiguous after another archive update.
    if (fileIndex >= numItems || GetItemRelPath(fileIndex) != tpi.RelPath) {
        UInt32 i;
        for (i = 0; i < numItems; i++)
            if (GetItemRelPath(i) == tpi.RelPath)
                break;
        if (i == numItems)
            return false;
        fileIndex = i;
    }
    CSelectedState state;
    SaveSelectedState(state);
    CDisableNotify disableNotify(*this);
    HRESULT result = OnOpenItemChanged(fileIndex, fs2us(tpi.FilePath), usePassword, password);
    // Publish the reached source refresh phase after the caller's operation
    // guards unwind, including when the native writer failed.
    if (refreshSelection)
        *refreshSelection = state;
    else
        RefreshListCtrl(state);
    return result == S_OK;
}

class CThreadCopyFrom : public CProgressThreadVirt {
    HRESULT ProcessVirt() override;

  public:
    UString FullPath;
    UInt32 ItemIndex;
    CMyComPtr<IFolderOperations> FolderOperations;
    CMyComPtr2<IProgress, CUpdateCallback100Imp> UpdateCallback;
};

HRESULT CThreadCopyFrom::ProcessVirt() {
    return FolderOperations->CopyFromFile(ItemIndex, FullPath, UpdateCallback);
}

HRESULT CPanel::OnOpenItemChanged(UInt32 index, const wchar_t *fullFilePath, bool usePassword,
                                  const UString &password) {
    if (!_folderOperations) {
        MessageBox_Error_UnsupportOperation();
        return E_FAIL;
    }
    CThreadCopyFrom t;
    t.UpdateCallback.SetFromCls(new CUpdateCallback100Imp);
    t.UpdateCallback->ProgressDialog = &t;
    t.ItemIndex = index;
    t.FullPath = fullFilePath;
    t.FolderOperations = _folderOperations;
    std::unique_ptr<IQtProgressTransport> transport;
    if (auto *services =
            dynamic_cast<IQtProgressServices *>((IFolderOperations *)_folderOperations))
        transport = services->CreateProgressTransport(t);
    t.UpdateCallback->Init();
    t.UpdateCallback->PasswordIsDefined = usePassword;
    t.UpdateCallback->Password = password;
    RINOK(t.Create(GetItemName(index), GetParent()))
    return t.Result;
}
