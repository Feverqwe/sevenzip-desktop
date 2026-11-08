#include "filemanager/window/qtguioutputrefresh.h"
#include <QScopedValueRollback>
#include <utility>

QtGuiOutputRefresh::QtGuiOutputRefresh(Ready ready, Refresh refresh)
    : ready_(std::move(ready)), refresh_(std::move(refresh)) {
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, &QtGuiOutputRefresh::Flush);
}

void QtGuiOutputRefresh::Enqueue(const QStringList &directories, const QStringList &archives) {
    if (stopped_ || (directories.isEmpty() && archives.isEmpty()))
        return;
    for (auto &output : pending_) {
        output.directories.append(directories);
        output.directories.removeDuplicates();
        output.archives.append(archives);
        output.archives.removeDuplicates();
    }
    if (!timer_.isActive())
        timer_.start();
}

void QtGuiOutputRefresh::Stop() {
    stopped_ = true;
    timer_.stop();
    pending_ = {};
}

void QtGuiOutputRefresh::Flush() {
    if (draining_ || stopped_)
        return;
    QScopedValueRollback<bool> draining(draining_, true);
    for (unsigned i = 0; i < pending_.size(); ++i) {
        if (stopped_)
            return;
        if (pending_[i].empty() || !ready_(i))
            continue;
        // Reload may start another scan or deliver another child completion.
        // Detach this panel's batch before entering that source/UI boundary.
        const Outputs output = std::exchange(pending_[i], {});
        refresh_(i, output);
    }
    if (pending_[0].empty() && pending_[1].empty())
        timer_.stop();
}
