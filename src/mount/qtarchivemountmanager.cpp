#include "mount/qtarchivemountmanager.h"
#include "Common/MyCom.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountsettings.h"
#include "mount/qtsmbmountplatform.h"
#include "ui/theme/qtcommandicons.h"
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

void QtArchiveMountManager::mount(IUnknown *resource, const QString &label,
                                  const QString &archivePassword, const QString &folder) {
    CMyComPtr<IUnknown> retained = resource;
    const QString unavailable = QtSmbMountPlatform::unavailableReason();
    if (!unavailable.isEmpty()) {
        QMessageBox::information(owner_, tr("Mount read-only"), unavailable);
        return;
    }
    const auto options = QtAskMountOptions(owner_, label, archivePassword, retained, folder);
    if (!options.Accepted)
        return;
    QString helper = QDir(QCoreApplication::applicationDirPath()).filePath("SevenzipSmb");
#ifdef Q_OS_WIN
    helper += QStringLiteral(".exe");
#endif
    if (!QFileInfo(helper).isExecutable()) {
        QMessageBox::warning(owner_, tr("Mount read-only"),
                             tr("The bundled SMB server is missing or cannot be executed."));
        return;
    }
    auto *session = new QtArchiveMountSession(label, this);
    sessions_.append(Session{session, {}, nullptr, !folder.isEmpty()});
    bindSession(*session);
    refresh();
    pendingFocus_ = sessions_.last().row;
    showManager();
    QTimer::singleShot(0, this, &QtArchiveMountManager::focusPendingEntry);
    session->start(retained, options.Quota, options.Password, helper, folder, options.View);
}

void QtArchiveMountManager::focusPendingEntry() {
    if (!isActiveWindow() || !pendingFocus_)
        return;
    auto *row = pendingFocus_.data();
    pendingFocus_.clear();
    if (auto *scroll = findChild<QScrollArea *>("dialogContentScroll"))
        scroll->ensureWidgetVisible(row);
    row->FocusPrimaryAction();
}

void QtArchiveMountManager::refresh() {
    using Status = QtArchiveMountSession::Status;
    empty_->setVisible(sessions_.isEmpty());
    for (auto &entry : sessions_) {
        if (!entry.row) {
            entry.row = new QtMountRow(
                QtCommandSymbol(entry.folder ? QtCommandIcon::Folder : QtCommandIcon::Archive),
                this);
            entries_->addWidget(entry.row);
            connect(entry.row, &QtMountRow::Requested, this,
                    [this, guard = QPointer<QtArchiveMountSession>(entry.instance)](
                        QtMountRow::Action action) {
                        if (guard)
                            requestAction(*guard.data(), action);
                    });
        }
        entry.row->SetData(rowData(entry));
        entry.row->show();
    }
    if (!closingOwner_)
        return;
    if (std::any_of(sessions_.cbegin(), sessions_.cend(), [](const Session &entry) {
            return entry.instance->status() == Status::Mounted &&
                   !entry.instance->error().isEmpty();
        })) {
        closingOwner_ = false;
        closeOwner_ = {};
        showManager();
    } else if (std::all_of(sessions_.cbegin(), sessions_.cend(), [](const Session &entry) {
                   return entry.instance->status() == Status::Stopped;
               })) {
        closingOwner_ = false;
        QTimer::singleShot(0, this, std::exchange(closeOwner_, {}));
    }
}
bool QtArchiveMountManager::hasActiveSessions() const {
    return std::any_of(sessions_.cbegin(), sessions_.cend(), [](const Session &entry) {
        return entry.instance->status() != QtArchiveMountSession::Status::Stopped;
    });
}

void QtArchiveMountManager::rebindOwner(QWidget &owner) {
    const bool visible = isVisible();
    owner_ = &owner;
    setParent(&owner, windowFlags());
    if (visible)
        show();
}

bool QtArchiveMountManager::canCloseOwner(std::function<void()> closeOwner) {
    if (!hasActiveSessions())
        return true;
    if (closingOwner_)
        return false;
    const QPointer<QWidget> requestedWindow(owner_);
    QMessageBox prompt(QMessageBox::Question, tr("Disconnect mounts before closing"),
                       closeOwner ? tr("Unmount the active connections and close this tab?")
                                  : tr("Unmount the active connections and close this window?"),
                       QMessageBox::Cancel, owner_);
    auto *close = prompt.addButton(tr("Unmount and Close"), QMessageBox::AcceptRole);
    prompt.setDefaultButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() != close)
        return false;
    closingOwner_ = true;
    if (!closeOwner) {
        closeOwner = [requestedWindow] {
            if (requestedWindow)
                requestedWindow->close();
        };
    }
    closeOwner_ = std::move(closeOwner);
    for (const auto &entry : sessions_)
        entry.instance->unmount();
    refresh();
    return false;
}
