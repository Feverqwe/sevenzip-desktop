#pragma once
#include "operations/qtoperationprotocol.h"
#include <QObject>
#include <memory>

// One app-owned observer per operation; closing a manager window drops only its
// signal receivers. Closing this observer never terminates independent work.
class QtOperationLauncher final : public QObject {
    Q_OBJECT
  public:
    explicit QtOperationLauncher(QObject *parent = nullptr);
    ~QtOperationLauncher() override;
    static QString GuiProgram();
    static QtOperationLauncher *Launch(const QString &program, const QtOperationJob &job,
                                       QString &error);
    bool start(const QString &program, const QtOperationJob &job, QString &error);
    quint64 processId() const;
    bool active() const;
    QtOperationPresentation presentation() const;
    void Show();
    static QList<QtOperationLauncher *> Active();
  signals:
    void accepted();
    void finished(const QtOperationResult &result);
    void presentationChanged();

  private:
    class Channel;
    void acceptRequest();
    void rejectRequest(const QString &error);
    void recordOutputs(const QStringList &directories, const QStringList &archives);
    bool completeRequest(const OperationReport &report);
    void updatePresentation(int phase, const QString &title, bool hasWindow);
    void observe();
    void finish(QtOperationResult result);
    void protocolFailure(const QString &error);
    struct State;
    std::unique_ptr<State> state_;
};
