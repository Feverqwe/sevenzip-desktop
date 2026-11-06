#include "mount/qtarchivemountmanager.h"
#include "Common/MyCom.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountsettings.h"
#include "mount/qtsmbmountplatform.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
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
    sessions_.append(Session{session, {}});
    bindSession(*session);
    refresh();
    list_->setCurrentItem(list_->topLevelItem(sessions_.size() - 1));
    showManager();
    session->start(retained, options.Quota, options.Password, helper, folder, options.View);
}

void QtArchiveMountManager::refresh() {
    using Status = QtArchiveMountSession::Status;
    const auto text = [](Status status) {
        switch (status) {
        case Status::Preparing:
            return tr("Preparing");
        case Status::Mounting:
            return tr("Mounting");
        case Status::Mounted:
            return tr("Mounted read-only");
        case Status::Unmounting:
            return tr("Unmounting");
        case Status::Stopping:
            return tr("Stopping");
        case Status::Stopped:
            return tr("Disconnected");
        }
        return QString();
    };
    for (int row = 0; row < sessions_.size(); ++row) {
        auto *session = sessions_[row].instance;
        auto *item = list_->topLevelItem(row);
        if (!item)
            item = new QTreeWidgetItem(list_);
        item->setText(0, session->label());
        item->setToolTip(0, session->label());
        item->setText(1, session->path());
        const auto status = session->status() == Status::Mounted && session->path().isEmpty()
                                ? tr("Connection needs recovery")
                                : text(session->status());
        item->setText(2, session->error().isEmpty() ? status
                                                    : tr("%1 — attention required").arg(status));
    }
    updateDetails();
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
