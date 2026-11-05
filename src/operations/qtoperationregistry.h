#pragma once
#include "operations/qtoperationprotocol.h"
#include <QObject>
#include <QPointer>
#include <QUuid>
#include <memory>
#include <vector>

class QtOperationLauncher;

struct QtOperationReview {
    QUuid Id;
    QtOperationJob Job;
    QtOperationResult Result;
};

// Application lifetime for independent observations and results whose child
// could not present a verified native outcome. No worker or decoder lives here.
class QtOperationRegistry final : public QObject {
    Q_OBJECT
  public:
    static QtOperationRegistry &Application();
    ~QtOperationRegistry() override;
    void Register(QtOperationLauncher &operation, const QtOperationJob &job);
    QUuid Registration(const QtOperationLauncher &operation) const;
    QList<QtOperationLauncher *> Active() const;
    QList<QtOperationReview> Reviews() const;
    void Acknowledge(const QUuid &id);
  signals:
    void reviewRequired(const QUuid &id);
    void changed();

  private:
    explicit QtOperationRegistry(QObject *parent);
    struct Observation {
        QUuid id;
        QPointer<QtOperationLauncher> operation;
    };
    struct Pending;
    QList<Observation> observations_;
    std::vector<std::unique_ptr<Pending>> pending_;
};
