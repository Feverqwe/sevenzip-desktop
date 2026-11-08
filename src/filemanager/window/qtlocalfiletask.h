#pragma once
#include <QEventLoopLocker>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class QThread;
class QWidget;
class CProgressDialog;
class COverwriteDialog;
class FileTransferOperation;
class QtCallbackProgressTransport;
class CPanel;
struct OverwriteQuestion;

// Presentation and cancellation for a source's filesystem/volume worker.
class QtLocalFileTask final : public QObject {
    Q_OBJECT
  public:
    QtLocalFileTask(FileTransferOperation &worker, const QString &title, const QString &detail);
    ~QtLocalFileTask() override;
    void BindOwner(CPanel &source, QWidget &window);
    void SetLocations(const QString &source, const QString &destination);
    void StopAndWait();
    static void StopForSource(CPanel &source);
    void Start();
    void Show();
    bool OwnsWindow(const QWidget &window) const;
    static QList<QtLocalFileTask *> Active();
  signals:
    void finished(bool success, bool cancelled, const QString &message, bool partial);
    void transferCompleted(bool complete);
    void transferOutput(const QStringList &movedSources);

  private:
    QtLocalFileTask(QThread &worker, const QString &title, const QString &detail,
                    std::function<void()> cancel);
    void AskOverwrite(FileTransferOperation &worker, const OverwriteQuestion &question);
    void Finish(bool success, bool cancelled, const QString &message, bool partial,
                const QStringList &movedSources, bool retainedOutput);
    void PresentFinish(bool success, bool cancelled, const QString &message, bool partial);
    void RequestPresentation(std::function<void()> action);
    void TryPresent();
    bool eventFilter(QObject *object, QEvent *event) override;
    QThread &worker_;
    std::function<void()> cancel_;
    QPointer<CProgressDialog> progress_;
    QPointer<COverwriteDialog> overwrite_;
    std::unique_ptr<QtCallbackProgressTransport> transport_;
    QEventLoopLocker keepAlive_;
    std::function<void()> pending_;
    QPointer<QWidget> blockedModal_;
    QPointer<CPanel> source_;
    bool stopped_ = false;
    unsigned presentationDepth_ = 0;
};
