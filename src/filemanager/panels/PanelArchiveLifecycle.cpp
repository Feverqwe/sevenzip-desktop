// Archive-layer lifetime adapted from 7-Zip PanelItemOpen.cpp and PanelFolderChange.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/external/qtexternalfilerecord.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanellocationtransaction.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "localization/LangUtils.h"
#include "platform/temp/qtownedtempidentity.h"
#include "ui/progress/ProgressDialog2.h"
#include <QFileInfo>
#include <QMessageBox>

using namespace NWindows;
using namespace NFile;

namespace {
bool RetainNestedArchiveCopy(const CFolderLink &link, const NFind::CFileInfo &info) {
    if (!link.OwnedTempLease)
        return false;
    const QtExternalFileRecord record{QFileInfo(ToQString(fs2us(link.FilePath))).fileName(),
                                      ToQString(link.RelPath),
                                      ToQString(link.VirtualPath),
                                      {info.Size, info.MTime},
                                      {},
                                      false};
    return link.OwnedTempLease->SetContext(record.Save());
}
} // namespace

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
    ffp.DisableUserQuestions = !_archiveOpenDialogs;
    if (_archiveOpenPassword) {
        ffp.Encrypted = true;
        ffp.Password = _archiveOpenPassword->native();
    }
    ffp.SettingsPath = _settingsPath;
    ffp.OwnedStreamFile = inStream && tempFileInfo.NeedDelete && !tempFileInfo.FilePath.IsEmpty();
    ffp.MaterializeStreamFile = materializeStream;
    const HRESULT res = ffp.OpenFileFolderPlugin(
        inStream, folderLink.FilePath.IsEmpty() ? us2fs(virtualFilePath) : folderLink.FilePath,
        arcFormat, virtualFilePath, GetParent());
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
        // A recognized encrypted archive must not fall through to an external
        // application when the supplied password did not open it.
        if (res == S_FALSE)
            return E_FAIL;
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

bool CPanel::CloseOneLevel(bool allowCancel) {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    const bool nested = _parentFolders.Size() > 1;
    CFolderLink &link = _parentFolders.Back();
    int answer = QMessageBox::No;
    NFind::CFileInfo current;
    if (nested && !_archiveContextOutdated && current.Find(link.FilePath) &&
        link.WasChanged_from_FolderLink(current)) {
        const UString message = MyFormatNew(IDS_WANT_UPDATE_MODIFIED_FILE, link.RelPath);
        auto buttons = QMessageBox::Yes | QMessageBox::No;
        if (allowCancel)
            buttons |= QMessageBox::Cancel;
        QMessageBox box(QMessageBox::Question, QStringLiteral("7-Zip"),
                        QString::fromWCharArray(message.Ptr()), buttons, GetParent());
        box.setObjectName("modifiedArchiveQuestion");
        box.setTextFormat(Qt::PlainText);
        box.setDefaultButton(QMessageBox::Yes);
        box.setEscapeButton(allowCancel ? QMessageBox::Cancel : QMessageBox::No);
        answer = box.exec();
        if (allowCancel && answer != QMessageBox::Yes && answer != QMessageBox::No)
            return false;
    }

    if (_archiveContextOutdated)
        PreserveOutdatedArchiveCopies();
    ReleaseFolder();
    _library.Free();
    if (link.ParentFolder)
        SetNewFolder(link.ParentFolder);
    _library.Attach(link.Library.Detach());
    QString publishedArchive;
    if (nested && !link.KeepOnOutdatedClose) {
        bool removeCopy = true;
        if (!_archiveContextOutdated && answer == QMessageBox::Yes) {
            const auto &parent = _parentFolders[_parentFolders.Size() - 2];
            const auto updated = UpdateExternalFile(link.FileIndex, fs2us(link.FilePath),
                                                    parent.UsePassword, parent.Password.native());
            if (_parentFolders.Size() == 2 && updated.Published == true)
                publishedArchive = ToQString(parent.VirtualPath);
            removeCopy = updated.Result == S_OK;
            if (!removeCopy) {
                if (current.Find(link.FilePath))
                    RetainNestedArchiveCopy(link, current);
                MessageBox_Error(MyFormatNew(IDS_CANNOT_UPDATE_FILE, fs2us(link.FilePath)));
            }
        }
        if (removeCopy)
            link.DeleteDirAndFile();
    }
    _parentFolders.DeleteBack();
    if (_parentFolders.IsEmpty()) {
        _archiveContextOutdated = false;
        _flatMode = _flatModeForDisk;
    }
    if (!publishedArchive.isEmpty() && _archivePublication)
        _archivePublication(publishedArchive, _locationTransactions.empty() ? this : nullptr);
    return true;
}

void CPanel::CloseOpenFolders() {
    while (!_parentFolders.IsEmpty())
        CloseOneLevel();
    _flatMode = _flatModeForDisk;
    ReleaseFolder();
    _library.Free();
    _archiveContextOutdated = false;
}

void CPanel::ReleaseArchiveContext(bool removeTemporaryFiles) {
    // An operation owns its publication decisions. Destruction must not ask
    // navigation questions or turn a failed inner update into outer success.
    Disable_Processing_Timer_Notify_StatusBar();
    ReleaseFolder();
    _library.Free();
    while (!_parentFolders.IsEmpty()) {
        if (removeTemporaryFiles)
            _parentFolders.Back().DeleteDirAndFile();
        _parentFolders.DeleteBack();
    }
    _archiveContextOutdated = false;
}

bool CPanel::MarkArchiveOutdated(const QString &path) {
    bool matched = false;
    if (!_parentFolders.IsEmpty()) {
        const QString current = ToQString(_parentFolders.Front().VirtualPath);
        const QString identity = QtOwnedTempIdentity(path);
        if (QFileInfo(current).absoluteFilePath() == QFileInfo(path).absoluteFilePath() ||
            (!identity.isEmpty() && identity == QtOwnedTempIdentity(current))) {
            _archiveContextOutdated = true;
            matched = true;
        }
    }
    for (auto *transaction : _locationTransactions)
        matched = transaction->MarkArchiveOutdated(path) || matched;
    return matched;
}

bool CPanel::PreserveOutdatedArchiveCopies() {
    if (!_archiveContextOutdated)
        return true;
    bool complete = true;
    for (unsigned level = 1; level < _parentFolders.Size(); ++level) {
        auto &link = _parentFolders[level];
        if (!link.NeedDelete || link.FilePath.IsEmpty())
            continue;
        NFind::CFileInfo current;
        const bool found = current.Find(link.FilePath);
        if (!found) {
            IFolderFolder *folder = level + 1 < _parentFolders.Size()
                                        ? _parentFolders[level + 1].ParentFolder.Interface()
                                        : GetFolder();
            const auto *native = dynamic_cast<CAgentFolder *>(folder);
            const auto *resource =
                native ? dynamic_cast<QtArchiveFileResource *>(native->GetEngineResource())
                       : nullptr;
            // Direct streams have no materialized copy. A missing file-backed
            // input cannot prove that its private edits are safe to release.
            if (link.IsVirtual && resource && resource->HasStreamInput())
                continue;
            link.KeepOnOutdatedClose = true;
            complete = false;
            continue;
        }
        if (!link.WasChanged_from_FolderLink(current))
            continue;
        link.KeepOnOutdatedClose = true;
        if (!RetainNestedArchiveCopy(link, current))
            complete = false;
    }
    return complete;
}
