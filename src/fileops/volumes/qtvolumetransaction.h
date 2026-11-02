#pragma once

#include "Windows/FileIO.h"
#include <QList>
#include <QString>

// Owns only staged files and files successfully published by this operation.
// NIO owns file data; this adapter supplies no-overwrite publication/rollback.
class QtVolumeTransaction final {
    struct Output {
        QString target;
        QString staged;
    };
    QList<Output> outputs_;
    qsizetype publishedCount_ = 0;
    bool committed_ = false;
    QString failedPath_;

  public:
    QtVolumeTransaction() = default;
    QtVolumeTransaction(const QtVolumeTransaction &) = delete;
    QtVolumeTransaction &operator=(const QtVolumeTransaction &) = delete;
    ~QtVolumeTransaction();
    bool create(NWindows::NFile::NIO::COutFile &file, const FString &target);
    bool publish();
    const QString &failedPath() const { return failedPath_; }
};
