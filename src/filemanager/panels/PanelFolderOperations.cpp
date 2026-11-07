// Adapted from 7-Zip PanelOperations.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/panels/PanelFolderOperations.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "ui/progress/qtprogresstransport.h"
#include <QScopeGuard>
#include <memory>
#ifdef _WIN32
#include "Windows/COM.h"
#endif

HRESULT CThreadFolderOperations::ProcessVirt() {
#ifdef _WIN32
    NWindows::NCOM::CComInitializer comInitializer;
#endif
    switch ((int)OpType) {
    case FOLDER_TYPE_CREATE_FOLDER:
        return FolderOperations->CreateFolder(Name, UpdateCallback);
    case FOLDER_TYPE_DELETE: {
        auto *folder = dynamic_cast<NFsFolder::CFSFolder *>(FolderOperations.Interface());
        const auto clearCheckBreak = qScopeGuard([folder] {
            if (folder)
                folder->CheckBreak = {};
        });
        if (folder)
            folder->CheckBreak = [this](const QString &path) {
                Sync.Set_FilePath(path.toStdWString().c_str());
                return Sync.CheckStop() == S_OK;
            };
        return FolderOperations->Delete(Indices.ConstData(), Indices.Size(), UpdateCallback);
    }
    case FOLDER_TYPE_RENAME:
        return FolderOperations->Rename(Index, Name, UpdateCallback);
    default:
        return E_FAIL;
    }
}

HRESULT CThreadFolderOperations::DoOperation(CPanel &panel, const UString &progressTitle,
                                             const UString &titleError) {
    UpdateCallback.SetFromCls(new CUpdateCallback100Imp);
    UpdateCallback->ProgressDialog = this;

    WaitMode = true;
    Sync.FinalMessage.ErrorMessage.Title = titleError;
    UpdateCallback->Init();

    if (!panel.GetParentFolders().IsEmpty()) {
        const CFolderLink &folderLink = panel.GetParentFolders().Back();
        UpdateCallback->PasswordArchivePath = folderLink.VirtualPath;
        UpdateCallback->PasswordIsDefined = folderLink.UsePassword;
        UpdateCallback->Password = folderLink.Password.native();
    }

    MainWindow = panel.GetParent();
    MainTitle = "7-Zip";
    MainAddTitle = progressTitle + L' ';
    SetLocations(QString::fromWCharArray(panel.GetCurrentFolderPrefix()));

    std::unique_ptr<IQtProgressTransport> transport;
    if (auto *services = dynamic_cast<IQtProgressServices *>(FolderOperations.Interface()))
        transport = services->CreateProgressTransport(*this);
    RINOK(Create(progressTitle, MainWindow))
    return Result;
}
