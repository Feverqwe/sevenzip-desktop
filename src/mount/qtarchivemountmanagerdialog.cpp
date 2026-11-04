#include "Common/MyWindows.h"
#include "mount/qtarchivemountmanager.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
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
    auto *layout = new QVBoxLayout(this);
    list_ = new QTreeWidget(this);
    list_->setObjectName("mountList");
    list_->setHeaderLabels({tr("Source"), tr("Mount path"), tr("Status")});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    list_->setColumnWidth(0, list_->fontMetrics().horizontalAdvance(QString(28, u'M')));
    list_->header()->setStretchLastSection(true);
    list_->setTextElideMode(Qt::ElideMiddle);
    list_->setContextMenuPolicy(Qt::ActionsContextMenu);
    remove_ = new QAction(tr("Remove from List"), list_);
    remove_->setObjectName("removeMountAction");
    list_->addAction(remove_);
    connect(remove_, &QAction::triggered, this, &QtArchiveMountManager::removeSelected);
    layout->addWidget(list_, 2);
    details_ = new QPlainTextEdit(this);
    details_->setObjectName("mountDetails");
    details_->setReadOnly(true);
    details_->setAccessibleName(tr("Mount details"));
    layout->addWidget(details_, 1);
    auto *buttons = new QDialogButtonBox(this);
    open_ = buttons->addButton(tr("Open Folder"), QDialogButtonBox::ActionRole);
    copy_ = buttons->addButton(tr("Copy Path"), QDialogButtonBox::ActionRole);
    unmount_ = buttons->addButton(tr("Unmount"), QDialogButtonBox::ActionRole);
    force_ = buttons->addButton(tr("Force Unmount…"), QDialogButtonBox::ActionRole);
    force_->setObjectName("forceUnmountButton");
    unmount_->setObjectName("unmountButton");
    copy_->setObjectName("copyMountPathButton");
    open_->setObjectName("openMountFolderButton");
    for (auto *button : {open_, copy_, unmount_, force_})
        button->setAutoDefault(false);
    layout->addWidget(buttons);
    auto *footer = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(footer);
    connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &QtArchiveMountManager::updateDetails);
    connect(open_, &QPushButton::clicked, this, [this] {
        if (const auto *entry = selected(); entry && !entry->instance->path().isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(entry->instance->path()));
    });
    connect(copy_, &QPushButton::clicked, this, [this] {
        if (const auto *entry = selected(); entry && !entry->instance->path().isEmpty())
            QApplication::clipboard()->setText(entry->instance->path());
    });
    connect(unmount_, &QPushButton::clicked, this, [this] { unmountSelected(false); });
    connect(force_, &QPushButton::clicked, this, [this] { unmountSelected(true); });
    resize(780, 420);
    updateDetails();
}
void QtArchiveMountManager::retranslate() {
    setWindowTitle(tr("Read-only mounts"));
    list_->setHeaderLabels({tr("Source"), tr("Mount path"), tr("Status")});
    details_->setAccessibleName(tr("Mount details"));
    open_->setText(tr("Open Folder"));
    copy_->setText(tr("Copy Path"));
    unmount_->setText(tr("Unmount"));
    force_->setText(tr("Force Unmount…"));
    remove_->setText(tr("Remove from List"));
    refresh();
}
void QtArchiveMountManager::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
}
const QtArchiveMountManager::Session *QtArchiveMountManager::selected() const {
    auto *item = list_->currentItem();
    const int row = item ? list_->indexOfTopLevelItem(item) : -1;
    return row >= 0 && row < sessions_.size() ? &sessions_.at(row) : nullptr;
}
void QtArchiveMountManager::updateDetails() {
    const auto *entry = selected();
    const auto *session = entry ? entry->instance : nullptr;
    const auto status = session ? session->status() : QtArchiveMountSession::Status::Stopped;
    const bool mounted = status == QtArchiveMountSession::Status::Mounted;
    open_->setEnabled(mounted && session && !session->path().isEmpty());
    copy_->setEnabled(open_->isEnabled());
    unmount_->setEnabled(session &&
                         (mounted || status == QtArchiveMountSession::Status::Preparing ||
                          status == QtArchiveMountSession::Status::Mounting));
    force_->setVisible(mounted && session && !session->error().isEmpty());
    remove_->setEnabled(session && status == QtArchiveMountSession::Status::Stopped);
    QStringList details;
    if (session) {
        details << session->label() << session->path() << tr("Read-only access");
        if (!session->limitations().isEmpty())
            details << session->limitations();
        if (!session->error().isEmpty())
            details << session->error();
        details << session->skippedEntries();
        details << entry->issues;
    }
    details_->setPlainText(details.join(u'\n'));
}
void QtArchiveMountManager::unmountSelected(bool force) {
    const auto *entry = selected();
    if (!entry)
        return;
    auto *session = entry->instance;
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
    session->unmount(force);
}
void QtArchiveMountManager::removeSelected() {
    const auto *entry = selected();
    if (!entry || entry->instance->status() != QtArchiveMountSession::Status::Stopped)
        return;
    auto *session = entry->instance;
    const int row = list_->indexOfTopLevelItem(list_->currentItem());
    sessions_.removeAt(row);
    disconnect(session, nullptr, this, nullptr);
    delete list_->takeTopLevelItem(row);
    session->deleteLater();
    if (!sessions_.isEmpty())
        list_->setCurrentItem(list_->topLevelItem(qMin(row, int(sessions_.size()) - 1)));
    refresh();
}
void QtArchiveMountManager::showManager() {
    show();
    raise();
    activateWindow();
}
