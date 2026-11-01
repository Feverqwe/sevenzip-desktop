#include "fileops/transfer/filetransferoperation.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/transfer/qtfiletransfertransaction.h"
#include <QMetaMethod>
#include <QMutexLocker>

FileTransferOperation::FileTransferOperation(QStringList sources, QString destination, bool move,
                                             QObject *parent, FileSystemAdapter adapter,
                                             DestinationKind destinationKind)
    : QThread(parent), sources_(std::move(sources)), destination_(std::move(destination)),
      move_(move), destinationKind_(destinationKind), adapter_(std::move(adapter)) {
    if (!adapter_.trySourceRename)
        adapter_.trySourceRename = [](const QString &source, const QString &stage) {
            return QtRenameFileSystemEntry(source, stage);
        };
    if (!adapter_.removeSource)
        adapter_.removeSource = QtRemoveFileSystemEntry;
}

void FileTransferOperation::cancel() {
    {
        // Pair the stop flag with the wait predicate: a wake between its check
        // and wait() must not be lost during a synchronous close-time join.
        QMutexLocker lock(&overwriteMutex_);
        canceled_ = true;
        overwriteCondition_.wakeAll();
    }
    emit dismissOverwriteRequested();
}

void FileTransferOperation::submitOverwriteAnswer(int answer) {
    QMutexLocker lock(&overwriteMutex_);
    if (!overwriteWaiting_)
        return;
    overwriteAnswer_ = answer;
    overwriteCondition_.wakeAll();
}

int FileTransferOperation::askOverwrite(const OverwriteQuestion &question) {
    QMutexLocker lock(&overwriteMutex_);
    if (canceled_ ||
        !isSignalConnected(QMetaMethod::fromSignal(&FileTransferOperation::overwriteRequested))) {
        canceled_ = true;
        return NOverwriteAnswer::kCancel;
    }
    overwriteWaiting_ = true;
    overwriteAnswer_ = -1;
    lock.unlock();
    emit overwriteRequested(question);
    lock.relock();
    while (overwriteAnswer_ < 0 && !canceled_)
        overwriteCondition_.wait(&overwriteMutex_);
    const int answer = canceled_ ? NOverwriteAnswer::kCancel : overwriteAnswer_;
    overwriteWaiting_ = false;
    if (answer == NOverwriteAnswer::kCancel)
        canceled_ = true;
    return answer;
}

void FileTransferOperation::run() {
    try {
        QtFileTransferTransaction(*this).run();
    } catch (...) {
        resultKind_ = ResultKind::Failure;
        emit result(false, QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL)));
    }
}
