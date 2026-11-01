#pragma once
#include "7zip/Archive/IArchive.h"
#include "ui/progress/qtworkerprogress.h"
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>
#include <functional>
#include <memory>

class ArchiveCodecCatalog;
class ArchiveNativeOpen;
struct CArcErrorInfo;
struct ArchiveEntry;
struct NativeArchiveListing;
struct QtArchiveDirectoryExtractRequest;

// Joined worker around official opening and read-only archive operations.
class QtArchiveReadOperation final : public QThread {
  public:
    struct ItemError {
        QString Path;
        Int32 Result;
        bool Encrypted;
    };
    struct OpenError {
        QString Path;
        UInt32 ErrorFlags;
        UInt32 WarningFlags;
        QString ErrorMessage;
        QString WarningMessage;
    };
    enum class Kind { Test, List, ExtractEntry, ExtractDirectory };
    struct SourceOutput {
        QString Entry;
        QString Path;
        bool AltStream = false;
        UInt32 NativeIndex = UInt32(-1);
        UInt64 Limit = UInt64(-1);
    };
    QtArchiveReadOperation(QString path, QString password,
                           std::shared_ptr<const ArchiveCodecCatalog> catalog, Kind kind,
                           QString format = {}, QString moduleLocation = {});
    // Extract an entry from an already consented, retained reader generation.
    QtArchiveReadOperation(std::shared_ptr<ArchiveNativeOpen> reader, QString password);
    ~QtArchiveReadOperation() override;
    void cancel();
    void setPaused(bool paused);
    void providePassword(const QString &password);
    void provideOverwriteAnswer(int answer);
    HRESULT askOverwrite(const QString &path, const ArchiveEntry &entry, Int32 *answer);
    HRESULT checkpoint();
    HRESULT password(BSTR *value);
    void report(quint64 completed, quint64 total, const QString &path);
    HRESULT Result = E_FAIL;
    QString CatalogDiagnostic;
    QString OpenedFormat;
    std::unique_ptr<NativeArchiveListing> Listing;
    std::shared_ptr<const ArchiveCodecCatalog> catalog() const { return catalog_; }
    QList<ItemError> Errors;
    QList<OpenError> OpenErrors;
    QStringList Messages;
    std::shared_ptr<const QtArchiveDirectoryExtractRequest> Directory;
    SourceOutput Output;
    bool OutputCreated = false;
    bool OutputClosed = false;
    Int32 OutputOperation = -1;
    std::function<void(quint64, quint64, const QString &)> Progress;
    std::function<void()> PasswordRequired;
    std::function<void(const QString &, const ArchiveEntry &)> OverwriteRequired;

  private:
    void run() override;
    void recordOpenError(const QString &path, const CArcErrorInfo &error);
    // Native interfaces stay inside run and release before this module lease.
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    std::shared_ptr<ArchiveNativeOpen> retainedReader_;
    const QString path_;
    const Kind kind_;
    const QString format_;
    const QString moduleLocation_;
    QtWorkerProgress progress_;
    QMutex passwordMutex_;
    QWaitCondition passwordReady_;
    QString password_;
    bool passwordDefined_;
    bool passwordRequested_ = false;
    QMutex overwriteMutex_;
    QWaitCondition overwriteReady_;
    bool overwriteAnswered_ = false;
    int overwriteAnswer_ = -1;
};
