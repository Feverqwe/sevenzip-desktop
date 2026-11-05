#include "mount/qtarchivemountsource.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivereadonlyview.h"
#include <QLocalServer>
#include <QMutexLocker>
#include <QRemoteObjectHost>
#include <QThread>

namespace {
MountTreeEntry MountEntry(const ArchiveReadOnlyView::Entry &entry) {
    return MountTreeEntry(entry.Id, entry.Parent, entry.Size, entry.Modified, entry.Name,
                          entry.Directory, entry.ChildrenLoaded, entry.SymlinkTarget);
}
} // namespace

QtArchiveMountSource::QtArchiveMountSource(IUnknown *resource, quint64 quota,
                                           QString archivePassword, QString user, QString password,
                                           QUrl endpoint, QString share, const QString &folder,
                                           std::shared_ptr<ArchiveReadOnlyView> preparedView)
    : user_(std::move(user)), password_(std::move(password)), share_(std::move(share)),
      endpoint_(std::move(endpoint)) {
    qRegisterMetaType<ArchiveMountDecisionPtr>();
    const auto consent = [this](const QString &path, quint64 size) {
        if (cancelled_)
            return false;
        if (extractionAllowed_)
            return *extractionAllowed_;
        const auto decision = std::make_shared<ArchiveMountDecision>();
        emit extractionRequired(path, size, decision);
        const bool allowed = decision->wait(cancelled_);
        if (!cancelled_)
            extractionAllowed_ = allowed;
        return allowed;
    };
    // This constructor runs on the panel owner thread before navigation can
    // replace its mutable resource. Enumeration and reads stay on the worker.
    const auto recovery = [this](const QString &path, quint64 size, Int32 operation) {
        if (cancelled_)
            return false;
        const auto decision = std::make_shared<ArchiveMountDecision>();
        emit unverifiedDataRequired(path, size, operation, decision);
        return decision->wait(cancelled_);
    };
    if (preparedView) {
        view_ = std::move(preparedView);
        view_->setReadOptions(quota, archivePassword, consent, recovery);
    } else {
        view_ = ArchiveBackend::retainReadOnlyView(resource, quota, archivePassword, consent,
                                                   recovery, folder, true);
    }
}
QtArchiveMountSource::~QtArchiveMountSource() = default;

void QtArchiveMountSource::initialize() {
    QString error;
    std::shared_ptr<ArchiveReadOnlyView> view;
    {
        QMutexLocker lock(&viewMutex_);
        view = view_;
    }
    const HRESULT result = ArchiveBackend::prepareReadOnlyView(view, error);
    if (cancelled_)
        return;
    if (result != S_OK) {
        emit failure(error.isEmpty() ? QStringLiteral("Could not prepare the retained reader.")
                                     : error);
        return;
    }
    for (const auto &entry : view->entries())
        entries_.append(MountEntry(entry));
    host_ = std::make_unique<QRemoteObjectHost>();
    QRemoteObjectHost::setLocalServerOptions(QLocalServer::UserAccessOption);
    if (!host_->setHostUrl(endpoint_) || !host_->enableRemoting<ArchiveMountSourceAPI>(this)) {
        emit failure(QStringLiteral("Could not create the private mount channel."));
        return;
    }
    emit prepared(view->limitations(), view->skippedEntries());
}

void QtArchiveMountSource::requestCancel() {
    cancelled_ = true;
    if (thread())
        thread()->requestInterruption();
    QMutexLocker lock(&viewMutex_);
    if (view_)
        view_->cancel();
}
void QtArchiveMountSource::close() {
    requestCancel();
    host_.reset();
    {
        QMutexLocker lock(&viewMutex_);
        view_.reset();
    }
    emit closed();
}
QList<MountTreeEntry> QtArchiveMountSource::entries() const { return entries_; }
QString QtArchiveMountSource::user() const { return user_; }
QString QtArchiveMountSource::password() const { return password_; }
QString QtArchiveMountSource::share() const { return share_; }
MountTreeRead QtArchiveMountSource::read(quint64 id, quint64 offset, quint32 length) {
    if (cancelled_ || !view_)
        return MountTreeRead(quint32(E_ABORT), -1, {}, {});
    const auto result = view_->read(id, offset, length);
    const bool unverified = result.Result == S_OK && result.Operation > 0;
    const quint32 issue = unverified ? quint32(S_FALSE) : quint32(result.Result);
    if (((result.Result != S_OK && result.Result != E_ABORT) || unverified) &&
        !reported_.value(id).contains(issue)) {
        reported_[id].insert(issue);
        const QString path = entryPath(id);
        const QString error = result.Error.isEmpty()
                                  ? QStringLiteral("Reader error 0x%1")
                                        .arg(quint32(result.Result), 8, 16, QLatin1Char('0'))
                                  : result.Error;
        emit entryIssue(id, issue, path + QStringLiteral(": ") + error, result.Operation);
    }
    if (result.Result == S_OK && result.Operation == 0)
        emit extracted(id);
    return MountTreeRead(quint32(result.Result), result.Operation, result.Data, result.Error);
}
void QtArchiveMountSource::listening(quint16 port) {
    if (port && !cancelled_)
        emit serverListening(port);
}
MountTreeDirectory QtArchiveMountSource::listDirectory(quint64 id) {
    if (cancelled_ || !view_)
        return MountTreeDirectory(quint32(E_ABORT), {}, {});
    const qsizetype skipped = view_->skippedEntries().size();
    const auto result = view_->listDirectory(id);
    QList<MountTreeEntry> entries;
    for (const auto &entry : result.Entries)
        entries.append(MountEntry(entry));
    if (result.Result != S_OK && result.Result != E_ABORT)
        emit entryIssue(id, quint32(result.Result),
                        entryPath(id) + QStringLiteral(": Reader error 0x%1")
                                            .arg(quint32(result.Result), 8, 16, QLatin1Char('0')),
                        -1);
    for (qsizetype i = skipped; i < view_->skippedEntries().size(); ++i)
        emit entryIssue(id, quint32(S_FALSE), view_->skippedEntries().at(i), -1);
    return MountTreeDirectory(quint32(result.Result), entries, result.Error);
}
void QtArchiveMountSource::failed(QString error) { emit failure(std::move(error)); }
QString QtArchiveMountSource::entryPath(quint64 id) const {
    QString path;
    const auto &entries = view_->entries();
    for (quint64 current = id; current > 1 && current <= quint64(entries.size());) {
        const auto &entry = entries.at(qsizetype(current - 1));
        path = path.isEmpty() ? entry.Name : entry.Name + u'/' + path;
        current = entry.Parent;
    }
    return path.isEmpty() ? QStringLiteral("/") : path;
}
