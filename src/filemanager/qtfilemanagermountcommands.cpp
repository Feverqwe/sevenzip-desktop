#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlangcontrols.h"
#include "mount/qtarchivemountmanager.h"
#include <QAction>
#include <QMenu>

void QtFileManagerCommands::AttachMountActions() {
    auto *menu = window_.findChild<QMenu *>("toolsMenu");
    auto *mount = menu->addAction(QtArchiveMountManager::tr("Mount read-only…"));
    mount->setObjectName("mountReadOnlyAction");
    QtBindLocalText(mount, "QtArchiveMountManager", "Mount read-only…");
    Bind(mount, MountReadOnly);
    auto *manage = menu->addAction(QtArchiveMountManager::tr("Read-only mounts…"));
    manage->setObjectName("manageMountsAction");
    QtBindLocalText(manage, "QtArchiveMountManager", "Read-only mounts…");
    Bind(manage, ManageMounts);
}
void QtFileManagerCommands::MountReadOnlyContainer() {
    auto *folder = dynamic_cast<CAgentFolder *>(window_.app_.GetFocusedPanel().GetFolder());
    auto *resource =
        folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
    if (!resource || !ArchiveUsesNativeItemIndexes(folder->GetArchiveProperties()))
        return;
    auto *mounts = window_.workspace_->CurrentMountManager(true);
    if (!mounts)
        return;
    const QString path = ToQString(GetFolderPath(folder));
    const QString label = path.isEmpty() ? QStringLiteral("/") : path;
    mounts->mount(resource, label, window_.projectedUi().archivePassword.toQString(), path);
}
void QtFileManagerCommands::ShowMounts() {
    if (auto *mounts = window_.workspace_->CurrentMountManager(true)) {
        mounts->show();
        mounts->raise();
        mounts->activateWindow();
    }
}
bool QtFileManagerCommands::CanCloseMounts() { return window_.workspace_->CanCloseMounts(); }
QWidget *QtFileManagerCommands::MountCloseBlocker() const {
    return window_.workspace_->MountCloseBlocker();
}
