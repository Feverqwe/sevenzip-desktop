#pragma once
#include "Common/MyCom.h"
#include "archive/resources/archivereadonlyview.h"
#include <QHash>
#include <QMutex>
#include <QTemporaryDir>
#include <atomic>

class ArchiveInputLayer;
class ArchiveNativeOpen;
class QtArchiveReadOperation;

struct ArchiveReadOnlyView::State {
    struct Item {
        UInt32 Index = UInt32(-1);
        quint64 Used = 0;
        quint64 Bytes = 0;
        QString Path;
        std::shared_ptr<ArchiveInputLayer> Stream;
        Int32 Operation = -1;
        bool Encrypted = false;
    };
    CMyComPtr<IUnknown> Resource;
    std::shared_ptr<ArchiveNativeOpen> Reader;
    bool Prepared = false, Ready = false;
    quint64 Depth = 0;
    QString Limitations;
    QString FolderPrefix;
    QStringList Skipped;
    QList<Entry> Entries;
    QHash<quint64, Item> Items;
    QHash<QString, quint64> Paths;
    quint64 Quota, Cached = 0, Clock = 0;
    unsigned Active = 0;
    QString Password;
    std::function<bool(const QString &, quint64)> ExtractionConsent;
    std::function<bool(const QString &, quint64, Int32)> RecoveryConsent;
    std::unique_ptr<QTemporaryDir> Directory;
    std::atomic_bool Cancelled{false};
    QMutex OperationMutex;
    QtArchiveReadOperation *Operation = nullptr;

    State(IUnknown *resource, quint64 quota, QString password,
          std::function<bool(const QString &, quint64)> extractionConsent,
          std::function<bool(const QString &, quint64, Int32)> recoveryConsent,
          const QString &folder);
    void skip(const QString &path, const QString &reason);
    CMyComPtr<IProgress> readRequest() const;
    bool evict(quint64 except, quint64 needed);
    HRESULT stream(quint64 id, ReadResult &result);
    HRESULT materialize(quint64 id, const CMyComPtr<IProgress> &request, ReadResult &result);
    static void describe(const Item &item, ReadResult &result);
};
