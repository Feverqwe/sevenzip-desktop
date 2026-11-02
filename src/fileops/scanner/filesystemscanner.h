#pragma once

#include "7zip/UI/FileManager/IFolder.h"
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QTimer>
#include <memory>
class FilesystemFolderLoader;

class FilesystemScanner : public QObject {
    Q_OBJECT

  public:
    explicit FilesystemScanner(QObject *parent = nullptr);
    ~FilesystemScanner() override;

    // A bound folder must be quiescent while loading. Reject reuse until any
    // canceled worker that owns that folder has exited; navigation stays async.
    bool start(const QString &directory, bool flat = true, IFolderFolder *boundFolder = nullptr);
    void cancel();
    void cancelAndWait();
    bool isRunning() const;

  signals:
    // All signals are dispatched on the GUI thread. Retain folderReady's
    // borrowed pointer through CMyComPtr when it must outlive the signal.
    void folderReady(IFolderFolder *folder);
    void batchReady(const QFileInfoList &files, quint32 firstSourceIndex);
    void advanced(quint64 count);
    void failed(qint32 result);
    void completed();

  private:
    struct Presentation;
    void scanBatch(quint64 generation);
    void loaderFinished(FilesystemFolderLoader *loader, quint64 generation);

    quint64 generation_ = 0;
    QTimer progressTimer_{this};
    quint64 progressCount_ = 0;
    FilesystemFolderLoader *loader_ = nullptr;
    QList<FilesystemFolderLoader *> loaders_;
    std::shared_ptr<Presentation> presentation_;
};
