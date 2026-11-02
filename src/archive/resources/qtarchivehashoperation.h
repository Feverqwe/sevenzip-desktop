#pragma once
#include "archive/hashing/HashCalc.h"
#include "ui/progress/qtworkerprogress.h"
#include <QStringList>
#include <QThread>
#include <functional>
#include <memory>

class ArchiveCodecCatalog;

// Thread/lifetime adapter around original HashCalc. File enumeration, method
// parsing, reads, statistics and digest aggregation belong to that source body.
class QtArchiveHashOperation final : public QThread, private IHashCallbackUI {
    // Declared first so DLL images outlive Bundle's retained IHasher instances.
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    // The same provider inventory serves synchronous validation and worker reads.
    std::unique_ptr<const CExternalCodecs> providers_;

  public:
    QtArchiveHashOperation(QStringList paths, QStringList methods,
                           std::shared_ptr<const ArchiveCodecCatalog> catalog,
                           std::unique_ptr<const CExternalCodecs> providers);
    void cancel();
    void setPaused(bool paused);
    HRESULT Result = E_FAIL;
    CHashBundle Bundle;
    QStringList Output;
    QStringList Errors;
    std::function<void(quint64, quint64, const QString &)> Progress;

  private:
    void run() override;
    HRESULT StartScanning() override;
    HRESULT ScanProgress(const CDirItemsStat &st, const FString &path, bool isDir) override;
    HRESULT ScanError(const FString &path, DWORD error) override;
    HRESULT FinishScanning(const CDirItemsStat &st) override;
    HRESULT SetNumFiles(UInt64 files) override;
    HRESULT SetTotal(UInt64 size) override;
    HRESULT SetCompleted(const UInt64 *completed) override;
    HRESULT CheckBreak() override;
    HRESULT BeforeFirstFile(const CHashBundle &hb) override;
    HRESULT GetStream(const wchar_t *name, bool isFolder) override;
    HRESULT OpenFileError(const FString &path, DWORD error) override;
    HRESULT SetOperationResult(UInt64 size, const CHashBundle &hb, bool showHash) override;
    HRESULT AfterLastFile(CHashBundle &hb) override;
    QStringList paths_;
    QStringList methods_;
    QString currentPath_;
    UString firstFileName_;
    UInt64 files_ = 0;
    quint64 total_ = 0;
    bool currentIsFolder_ = false;
    QtWorkerProgress progress_;
};
