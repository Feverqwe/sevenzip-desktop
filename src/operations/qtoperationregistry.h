#pragma once
#include "operations/qtoperationprotocol.h"
#include <QObject>
#include <QPointer>
#include <QUuid>
#include <memory>
#include <optional>
#include <vector>

class QtOperationLauncher;

struct QtOperationReview {
    QUuid Id;
    QtOperationJob Job;
    QtOperationResult Result;
};

struct QtOperationListing {
    QUuid Id;
    QtOperationJob::Command Operation = QtOperationJob::Command::Test;
    QString Source;
    QtOperationPresentation Presentation;
    std::optional<QtOperationResult::Outcome> Result;
};

// Application lifetime for independent observations and results whose child
// need review after the child exits. No worker or decoder lives here.
class QtOperationRegistry final : public QObject {
    Q_OBJECT
  public:
    static QtOperationRegistry &Application();
    ~QtOperationRegistry() override;
    void Register(QtOperationLauncher &operation, const QtOperationJob &job);
    QUuid Registration(const QtOperationLauncher &operation) const;
    QList<QtOperationLauncher *> Active() const;
    QList<QtOperationReview> Reviews() const;
    QList<QtOperationListing> Listings() const;
    void Show(const QUuid &id);
    void Acknowledge(const QUuid &id);
  signals:
    void reviewRequired(const QUuid &id);
    void reviewActivated(const QUuid &id);
    void changed();

  private:
    explicit QtOperationRegistry(QObject *parent);
    struct Observation {
        QUuid id;
        QPointer<QtOperationLauncher> operation;
        QtOperationJob job;
    };
    struct Pending;
    QList<Observation> observations_;
    std::vector<std::unique_ptr<Pending>> pending_;
    QList<QtOperationListing> completed_;
};
