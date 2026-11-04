// Archive-layer lifetime adapted from 7-Zip PanelItemOpen.cpp and PanelFolderChange.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringConvert.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/archivelayercontext.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "localization/LangUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMessageBox>

using namespace NWindows;
using namespace NFile;

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
    folderLink.Password = ArchivePassword(ffp.Password);
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
                                      folderLinkPrev.Password.native()) != S_OK) {
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

void CPanel::CloseOneLevel() {
    ReleaseFolder();
    _library.Free();
    {
        CFolderLink &link = _parentFolders.Back();
        if (link.ParentFolder)
            SetNewFolder(link.ParentFolder);
        _library.Attach(link.Library.Detach());
    }
    if (_parentFolders.Size() > 1)
        OpenParentArchiveFolder();
    _parentFolders.DeleteBack();
    if (_parentFolders.IsEmpty())
        _flatMode = _flatModeForDisk;
}

void CPanel::CloseOpenFolders() {
    while (!_parentFolders.IsEmpty())
        CloseOneLevel();
    _flatMode = _flatModeForDisk;
    ReleaseFolder();
    _library.Free();
}
