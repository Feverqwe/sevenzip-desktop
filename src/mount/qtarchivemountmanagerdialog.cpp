#include "Common/MyWindows.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "mount/qtarchivemountmanager.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
void ShowMountPrompt(QtArchiveMountSession &session, const QString &path, quint64 size,
                     const ArchiveMountDecisionPtr &decision, QWidget *owner, bool unverified) {
    auto *prompt =
        new QtMountExtractionPrompt(session.label(), path, size, decision, owner, unverified);
    prompt->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(&session, &QtArchiveMountSession::stopped, prompt, &QDialog::reject);
    prompt->open();
}
} // namespace

void QtArchiveMountManager::bindSession(QtArchiveMountSession &session) {
    connect(&session, &QtArchiveMountSession::changed, this, &QtArchiveMountManager::refresh);
    connect(&session, &QtArchiveMountSession::entryIssue, this,
            [this, session = &session](quint64, quint32 result, const QString &error, qint32) {
                if (result == quint32(E_ABORT))
                    return;
                const auto entry = std::ranges::find(sessions_, session, &Session::instance);
                if (entry == sessions_.end())
                    return;
                entry->issues.append(result == quint32(S_FALSE)
                                         ? tr("Unverified data: %1").arg(error)
                                         : tr("Read failed: %1").arg(error));
                refresh();
            });
    connect(&session, &QtArchiveMountSession::extractionRequired, this,
            [this, session = &session](const QString &path, quint64 size,
                                       const ArchiveMountDecisionPtr &decision) {
                ShowMountPrompt(*session, path, size, decision, owner_, false);
            });
    connect(&session, &QtArchiveMountSession::unverifiedDataRequired, this,
            [this, session = &session](const QString &path, quint64 size, qint32,
                                       const ArchiveMountDecisionPtr &decision) {
                ShowMountPrompt(*session, path, size, decision, owner_, true);
            });
}

QtArchiveMountManager::QtArchiveMountManager(QWidget *owner) : QDialog(owner), owner_(owner) {
    setObjectName("archiveMountManager");
    setWindowTitle(tr("Read-only mounts"));
    setModal(false);
    auto *footer = new QtLangButtonBox(QDialogButtonBox::Close, this);
    entries_ = QtCreateDialogLayout(*this, *footer, QtDialogContent::Scrollable);
    empty_ = new QtWrappingLabel(this);
    empty_->setObjectName("mountEmpty");
    empty_->setWordWrap(true);
    QtBindLocalText(empty_, "QtArchiveMountManager", QT_TR_NOOP("No mounts yet."));
    entries_->addWidget(empty_);
    connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(780, 420);
}

void QtArchiveMountManager::retranslate() {
    setWindowTitle(tr("Read-only mounts"));
    refresh();
}

void QtArchiveMountManager::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    else if (event->type() == QEvent::ActivationChange && isActiveWindow())
        QTimer::singleShot(0, this, &QtArchiveMountManager::focusPendingEntry);
}

QtMountRowData QtArchiveMountManager::rowData(const Session &entry) const {
    using Status = QtArchiveMountSession::Status;
    const auto &session = *entry.instance;
    const auto status = session.status();
    QtMountRowData data;
    data.sourcePath = session.label();
    data.name = QFileInfo(QDir::cleanPath(data.sourcePath)).fileName();
    if (data.name.isEmpty())
        data.name = data.sourcePath;
    data.mountPath = session.path();
    data.error = session.error();
    switch (status) {
    case Status::Preparing:
        data.status = tr("Preparing");
        break;
    case Status::Mounting:
        data.status = tr("Mounting");
        break;
    case Status::Mounted:
        data.status =
            data.mountPath.isEmpty() ? tr("Connection needs recovery") : tr("Mounted read-only");
        break;
    case Status::Unmounting:
        data.status = tr("Unmounting");
        break;
    case Status::Stopping:
        data.status = tr("Stopping");
        break;
    case Status::Stopped:
        data.status = tr("Disconnected");
        break;
    }
    data.canOpen = status == Status::Mounted && !data.mountPath.isEmpty();
    data.preparing = status == Status::Preparing || status == Status::Mounting;
    data.canUnmount = status == Status::Mounted || data.preparing;
    data.canForce = status == Status::Mounted && !data.error.isEmpty();
    data.canRemove = status == Status::Stopped;
    QStringList details;
    if (!session.limitations().isEmpty())
        details << session.limitations();
    if (!data.error.isEmpty())
        details << data.error;
    details << session.skippedEntries() << entry.issues;
    if (!details.isEmpty()) {
        details.prepend(tr("Read-only access"));
        details.prepend(data.mountPath);
        details.prepend(data.sourcePath);
        data.report = details.join('\n');
    }
    return data;
}

void QtArchiveMountManager::requestAction(QtArchiveMountSession &session,
                                          QtMountRow::Action action) {
    const auto entry = std::ranges::find(sessions_, &session, &Session::instance);
    if (entry == sessions_.end())
        return;
    const auto data = rowData(*entry);
    switch (action) {
    case QtMountRow::Action::Open:
        if (data.canOpen)
            QDesktopServices::openUrl(QUrl::fromLocalFile(data.mountPath));
        break;
    case QtMountRow::Action::CopyPath:
        if (data.canOpen)
            QApplication::clipboard()->setText(data.mountPath);
        break;
    case QtMountRow::Action::Unmount:
        if (data.canUnmount)
            unmountSession(session, false);
        break;
    case QtMountRow::Action::ForceUnmount:
        if (data.canForce)
            unmountSession(session, true);
        break;
    case QtMountRow::Action::Remove:
        if (data.canRemove)
            removeSession(session);
        break;
    }
}

void QtArchiveMountManager::unmountSession(QtArchiveMountSession &session, bool force) {
    if (force) {
        QMessageBox prompt(QMessageBox::Warning, tr("Force unmount"),
                           tr("Disconnect this mount even if another application is using it? "
                              "Open files may stop working."),
                           QMessageBox::Cancel, this);
        auto *confirm = prompt.addButton(tr("Force Unmount"), QMessageBox::AcceptRole);
        prompt.setDefaultButton(QMessageBox::Cancel);
        prompt.exec();
        if (prompt.clickedButton() != confirm)
            return;
    }
    session.unmount(force);
}

void QtArchiveMountManager::removeSession(QtArchiveMountSession &session) {
    const auto entry = std::ranges::find(sessions_, &session, &Session::instance);
    if (entry == sessions_.end() || session.status() != QtArchiveMountSession::Status::Stopped)
        return;
    auto *row = entry->row;
    sessions_.erase(entry);
    disconnect(&session, nullptr, this, nullptr);
    if (row) {
        entries_->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    session.deleteLater();
    refresh();
}

void QtArchiveMountManager::showManager() {
    show();
    raise();
    activateWindow();
}
