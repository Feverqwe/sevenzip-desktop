#include "operations/qtoperationregistry.h"
#include "operations/qtoperationlauncher.h"
#include <QCoreApplication>
#include <QEventLoopLocker>
#include <algorithm>

namespace {
QtOperationListing Listing(const QUuid &id, const QtOperationJob &job,
                           const QtOperationPresentation &presentation,
                           std::optional<QtOperationResult::Outcome> result) {
    return {id, job.Operation, job.Paths.isEmpty() ? job.ArchivePath : job.Paths.first(),
            presentation, result};
}
} // namespace

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
    observations_.append({id, &operation, job});
    // Launch() registers before start(); publish after that synchronous attempt.
    QMetaObject::invokeMethod(this, &QtOperationRegistry::changed, Qt::QueuedConnection);
    connect(&operation, &QtOperationLauncher::presentationChanged, this,
            &QtOperationRegistry::changed);
    connect(&operation, &QtOperationLauncher::accepted, this, &QtOperationRegistry::changed);
    connect(&operation, &QObject::destroyed, this, [this, id] {
        observations_.removeIf([&](const Observation &entry) { return entry.id == id; });
        emit changed();
    });
    connect(&operation, &QtOperationLauncher::finished, this,
            [this, id, job](const QtOperationResult &result) {
                observations_.removeIf([&](const Observation &entry) { return entry.id == id; });
                using Outcome = QtOperationResult::Outcome;
                if (result.Status == Outcome::Success || result.Status == Outcome::Warning) {
                    // Recent display values have no keep-alive hold or full job.
                    completed_.prepend(Listing(id, job, {}, result.Status));
                    constexpr qsizetype recentResultLimit = 20;
                    if (completed_.size() > recentResultLimit)
                        completed_.removeLast();
                }
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

QList<QtOperationListing> QtOperationRegistry::Listings() const {
    QList<QtOperationListing> result;
    for (const auto &entry : observations_)
        if (entry.operation && entry.operation->active())
            result.append(
                Listing(entry.id, entry.job, entry.operation->presentation(), std::nullopt));
    for (const auto &entry : pending_)
        result.append(
            Listing(entry->review.Id, entry->review.Job, {}, entry->review.Result.Status));
    for (const auto &entry : completed_)
        result.append(entry);
    return result;
}

void QtOperationRegistry::Show(const QUuid &id) {
    for (const auto &entry : observations_)
        if (entry.id == id && entry.operation && entry.operation->active()) {
            entry.operation->Show();
            return;
        }
    for (const auto &entry : pending_)
        if (entry->review.Id == id) {
            emit reviewActivated(id);
            return;
        }
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
