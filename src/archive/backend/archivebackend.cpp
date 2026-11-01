#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/codecs/archivecodecprobe.h"
#include "archive/codecs/archivecodelocation.h"
#include "archive/resources/qtarchivedirectoryextract.h"
#include "archive/resources/qtarchivehashoperation.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
ArchiveBackend::ArchiveBackend(QObject *parent, QString runtimeLocation) : QObject(parent) {
    workerProgressTimer_.setInterval(33);
    connect(&workerProgressTimer_, &QTimer::timeout, this, &ArchiveBackend::reportWorkerProgress);
    configuredModule_ = FindArchiveModule(runtimeLocation);
}

ArchiveBackend::~ArchiveBackend() {
    blockSignals(true);
    codecProbe_.reset();
    cancelAndWait();
}

bool ArchiveBackend::busy() const {
    return job_ != Job::None || overwritePromptActive_ || codecDiscoveryActive_;
}

HRESULT ArchiveBackend::lastResult() const { return lastResult_; }

HRESULT ArchiveBackend::setPaused(bool paused) {
    if (readOperation_) {
        readOperation_->setPaused(paused);
        return S_OK;
    }
    if (hashOperation_) {
        hashOperation_->setPaused(paused);
        return S_OK;
    }
    return E_FAIL;
}

void ArchiveBackend::provideOverwriteAnswer(int answer) {
    if (overwritePromptActive_ && readOperation_) {
        overwritePromptActive_ = false;
        readOperation_->provideOverwriteAnswer(answer);
    }
}

void ArchiveBackend::list(const QString &archive, const QString &password) {
    startListing(archive, password, {});
}

void ArchiveBackend::openFolder(const QString &archive, const QString &format,
                                const QString &password) {
    if (!busy() && !format.isEmpty()) {
        if (const auto catalog = codecCatalog(); catalog && catalog->result() == S_OK) {
            CObjectVector<COpenType> types;
            if (!catalog->parseOpenTypes(format, types)) {
                lastResult_ = E_INVALIDARG;
                emit finished(false, QCoreApplication::translate("archivebackend",
                                                                 "Invalid archive open type: %1")
                                         .arg(format));
                return;
            }
        }
    }
    if (format.isEmpty())
        list(archive, password);
    else
        startListing(archive, password, format);
}

void ArchiveBackend::startListing(const QString &archive, const QString &password,
                                  const QString &format) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend",
                                                         "Another operation is still running."));
        return;
    }
    archive_ = QFileInfo(archive).absoluteFilePath();
    listedSourceArchives_.remove(archive_);
    entries_.clear();
    startNativeRead(Job::List, archive_, password, format);
}

bool ArchiveBackend::hasListedSourceArchive(const QString &archive) const {
    if (busy())
        return false;
    const QFileInfo file(archive);
    const auto found = listedSourceArchives_.constFind(file.absoluteFilePath());
    return found != listedSourceArchives_.cend() && file.isFile() && !file.isSymLink() &&
           file.size() == found->Size && file.lastModified() == found->Modified &&
           file.metadataChangeTime() == found->MetadataChanged;
}

bool ArchiveBackend::restoreListedArchive(const QString &archive) {
    if (!hasListedSourceArchive(archive))
        return false;
    const QString path = QFileInfo(archive).absoluteFilePath();
    const ListedSourceSnapshot &snapshot = listedSourceArchives_.constFind(path).value();
    archive_ = path;
    entries_ = snapshot.Entries;
    return true;
}

void ArchiveBackend::extract(const QString &archive, const QString &destination,
                             const QStringList &selected, const QString &password,
                             const ExtractionOptions &options) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend",
                                                         "Another operation is still running."));
        return;
    }
    lastResult_ = E_FAIL;
    cancelled_ = false;
    if (!restoreListedArchive(archive)) {
        emit finished(
            false, QCoreApplication::translate("archivebackend",
                                               "Open the archive and inspect its contents first."));
        return;
    }
    if (options.pathMode == ExtractionOptions::PathMode::AbsolutePaths || options.ElimDup.Val ||
        options.NtSecurity.Val) {
        emit finished(
            false, QCoreApplication::translate(
                       "archivebackend", "These extraction options require the source workflow."));
        return;
    }
    QStringList selectedEntries;
    for (const QString &name : selected) {
        QString normalized = name;
        normalized.replace('\\', '/');
        bool found = false;
        for (const ArchiveEntry &entry : entries_) {
            QString entryPath = entry.path;
            entryPath.replace('\\', '/');
            if (entryPath == normalized) {
                selectedEntries << entry.path;
                found = true;
                break;
            }
        }
        if (!found) {
            emit finished(
                false, QCoreApplication::translate("archivebackend",
                                                   "Selected file is missing from the archive: %1")
                           .arg(name));
            return;
        }
    }
    const auto output = prepareExtractionDestination(destination, selectedEntries, options);
    if (!output) {
        emit finished(false, output.error());
        return;
    }
    auto request = std::make_shared<QtArchiveDirectoryExtractRequest>();
    request->Destination = *output;
    request->Selected = selectedEntries;
    request->Options = options;
    startNativeRead(Job::Extract, archive_, password, {}, std::move(request));
}

void ArchiveBackend::test(const QString &archive, const QString &password) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend",
                                                         "Another operation is still running."));
        return;
    }
    lastResult_ = E_FAIL;
    if (!restoreListedArchive(archive)) {
        emit finished(
            false, QCoreApplication::translate("archivebackend",
                                               "Open the archive and inspect its contents first."));
        return;
    }
    startNativeRead(Job::Test, archive_, password);
}

void ArchiveBackend::testSourceArchive(const QString &archive, const QString &password) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend",
                                                         "Another operation is still running."));
        return;
    }
    const QFileInfo file(archive);
    if (!file.isFile() || file.isSymLink()) {
        lastResult_ = E_INVALIDARG;
        emit finished(
            false, QCoreApplication::translate("archivebackend", "Source archive is unavailable."));
        return;
    }
    archive_ = file.absoluteFilePath();
    startNativeRead(Job::Test, archive_, password);
}

void ArchiveBackend::hash(const QStringList &paths, const QString &method) {
    hashMethods(paths, {method});
}

void ArchiveBackend::cancel() {
    if (!busy())
        return;
    cancelled_ = true;
    if (hashOperation_)
        hashOperation_->cancel();
    if (readOperation_)
        readOperation_->cancel();
    if (overwritePromptActive_) {
        overwritePromptActive_ = false;
        emit dismissOverwriteRequested();
    }
}

void ArchiveBackend::cancelAndWait() {
    const QPointer<ArchiveBackend> owner(this);
    const QPointer<QtArchiveHashOperation> hash(hashOperation_.get());
    const QPointer<QtArchiveReadOperation> read(readOperation_.get());
    if (busy())
        cancel();
    if (!owner)
        return;
    // Dismissing a prompt can join this worker and start its replacement.
    // Completion owns the join; only finish the operation cancelled above.
    if (hash && hashOperation_.get() == hash.data()) {
        finishHashOperation();
        return;
    }
    if (read && readOperation_.get() == read.data())
        finishReadOperation();
}

void ArchiveBackend::providePassword(const QString &password) {
    if (readOperation_)
        readOperation_->providePassword(password);
}
