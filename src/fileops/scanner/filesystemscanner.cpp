#include "fileops/scanner/filesystemscanner.h"
#include "Common/MyCom.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/filesystemfolderloader.h"
#include <QPointer>
#include <QTimer>

struct FilesystemScanner::Presentation {
    explicit Presentation(IFolderFolder *source) : folder(source) {}
    const CMyComPtr<IFolderFolder> folder;
    quint32 nextIndex = 0;
};

FilesystemScanner::FilesystemScanner(QObject *parent) : QObject(parent) {
    progressTimer_.setInterval(33);
    connect(&progressTimer_, &QTimer::timeout, this, [this] {
        if (!loader_)
            return;
        const quint64 count = loader_->completedCount();
        if (count != progressCount_) {
            progressCount_ = count;
            emit advanced(count);
        }
    });
}

FilesystemScanner::~FilesystemScanner() { cancelAndWait(); }

void FilesystemScanner::cancelAndWait() {
    cancel();
    const auto loaders = loaders_;
    loaders_.clear();
    for (auto *loader : loaders) {
        disconnect(loader, nullptr, this, nullptr);
        loader->cancel();
        loader->wait();
        delete loader;
    }
}

bool FilesystemScanner::start(const QString &directory, bool flat, IFolderFolder *boundFolder) {
    if (boundFolder)
        for (auto *loader : loaders_)
            if (loader->loadsFolder(boundFolder) && loader->isRunning())
                return false;
    cancel();
    loader_ = new FilesystemFolderLoader(directory, this, flat, boundFolder);
    loaders_.append(loader_);
    const quint64 generation = generation_;
    auto *loader = loader_;
    progressTimer_.start();
    const QPointer<FilesystemFolderLoader> guard(loader);
    connect(loader, &QThread::finished, this, [this, guard, generation] {
        // Disconnect does not remove already queued signals. A close-time join
        // may have deleted this loader before its finished delivery reaches us.
        if (guard)
            loaderFinished(guard, generation);
    });
    loader->start();
    return true;
}

void FilesystemScanner::loaderFinished(FilesystemFolderLoader *loader, quint64 generation) {
    loader->wait();
    loaders_.removeOne(loader);
    const bool current = generation == generation_ && loader == loader_;
    if (current) {
        loader_ = nullptr;
        progressTimer_.stop();
    }
    const quint64 count = loader->completedCount();
    const HRESULT result = loader->result();
    CMyComPtr<IFolderFolder> loaded;
    loaded.Attach(loader->takeFolder());
    loader->deleteLater();
    if (!current)
        return;
    const QPointer<FilesystemScanner> progressGuard(this);
    if (count != progressCount_) {
        progressCount_ = count;
        emit advanced(count);
    }
    if (!progressGuard || generation != generation_)
        return;
    if (result != S_OK) {
        emit failed(qint32(result));
        return;
    }
    const auto presentation = std::make_shared<Presentation>(loaded);
    presentation_ = presentation;
    const QPointer<FilesystemScanner> guard(this);
    emit folderReady(presentation->folder);
    if (!guard)
        return;
    if (presentation == presentation_)
        QTimer::singleShot(0, this, [this, generation] { scanBatch(generation); });
}

void FilesystemScanner::cancel() {
    ++generation_;
    progressTimer_.stop();
    progressCount_ = 0;
    if (loader_)
        loader_->cancel();
    loader_ = nullptr;
    presentation_.reset();
}

bool FilesystemScanner::isRunning() const { return loader_ || presentation_; }

void FilesystemScanner::scanBatch(quint64 generation) {
    const auto presentation = presentation_;
    if (generation != generation_ || !presentation)
        return;
    const quint32 firstIndex = presentation->nextIndex;
    auto *spec = static_cast<NFsFolder::CFSFolder *>((IFolderFolder *)presentation->folder);
    // Row creation can include native icon lookup. Bound each GUI delivery;
    // path and row projection belong to the receiving panel.
    const quint32 count =
        firstIndex < spec->Files.Size() ? qMin(quint32(32), spec->Files.Size() - firstIndex) : 0;
    presentation->nextIndex += count;
    const QPointer<FilesystemScanner> guard(this);
    if (count)
        emit batchReady(firstIndex, count);
    if (!guard)
        return;
    if (presentation != presentation_)
        return;
    if (presentation->nextIndex < spec->Files.Size()) {
        QTimer::singleShot(0, this, [this, generation] { scanBatch(generation); });
        return;
    }
    presentation_.reset();
    emit completed();
}
