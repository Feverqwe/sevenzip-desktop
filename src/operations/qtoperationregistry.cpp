#include "operations/qtoperationregistry.h"
#include "operations/qtoperationlauncher.h"
#include <QCoreApplication>
#include <QEventLoopLocker>
#include <algorithm>

struct QtOperationRegistry::Pending {
    QtOperationReview review;
    QEventLoopLocker keepAlive;
    explicit Pending(QtOperationReview value) : review(std::move(value)) {}
};

QtOperationRegistry::QtOperationRegistry(QObject *parent) : QObject(parent) {}
QtOperationRegistry::~QtOperationRegistry() = default;

QtOperationRegistry &QtOperationRegistry::Application() {
    auto *application = QCoreApplication::instance();
    for (auto *child : application->children())
        if (auto *registry = qobject_cast<QtOperationRegistry *>(child))
            return *registry;
    return *new QtOperationRegistry(application);
}

void QtOperationRegistry::Register(QtOperationLauncher &operation, const QtOperationJob &job) {
    for (const auto &entry : observations_)
        if (entry.operation == &operation)
            return;
    const QUuid id = QUuid::createUuid();
    observations_.append({id, &operation});
    connect(&operation, &QObject::destroyed, this, [this, id] {
        observations_.removeIf([&](const Observation &entry) { return entry.id == id; });
        emit changed();
    });
    connect(&operation, &QtOperationLauncher::finished, this,
            [this, id, job](const QtOperationResult &result) {
                observations_.removeIf([&](const Observation &entry) { return entry.id == id; });
                using Outcome = QtOperationResult::Outcome;
                if (result.Status == Outcome::LaunchFailed ||
                    result.Status == Outcome::ProtocolFailed || result.Status == Outcome::Crashed ||
                    result.CancellationNeedsReview) {
                    // Acquire the review hold before the observer releases its
                    // own hold after all finished receivers have returned.
                    pending_.push_back(
                        std::make_unique<Pending>(QtOperationReview{id, job, result}));
                    emit reviewRequired(id);
                }
                emit changed();
            });
}

QList<QtOperationLauncher *> QtOperationRegistry::Active() const {
    QList<QtOperationLauncher *> result;
    for (const auto &entry : observations_)
        if (entry.operation && entry.operation->active())
            result.append(entry.operation);
    return result;
}

QUuid QtOperationRegistry::Registration(const QtOperationLauncher &operation) const {
    for (const auto &entry : observations_)
        if (entry.operation == &operation)
            return entry.id;
    return {};
}

QList<QtOperationReview> QtOperationRegistry::Reviews() const {
    QList<QtOperationReview> result;
    for (const auto &entry : pending_)
        result.append(entry->review);
    return result;
}

void QtOperationRegistry::Acknowledge(const QUuid &id) {
    const auto found = std::find_if(pending_.begin(), pending_.end(),
                                    [&](const auto &entry) { return entry->review.Id == id; });
    if (found == pending_.end())
        return;
    const auto completed = std::move(*found);
    pending_.erase(found);
    emit changed();
}
