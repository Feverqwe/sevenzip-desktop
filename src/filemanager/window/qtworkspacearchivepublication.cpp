#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include "localization/qtlangcontrols.h"
#include "operations/qtoperationlauncher.h"
#include "platform/temp/qtownedtempidentity.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QFileInfo>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>

namespace {
bool SameArchive(const QString &left, const QString &right) {
    if (left.isEmpty() || right.isEmpty())
        return false;
    const QFileInfo a(left), b(right);
    if (a.absoluteFilePath() == b.absoluteFilePath() ||
        (!a.canonicalFilePath().isEmpty() && a.canonicalFilePath() == b.canonicalFilePath()))
        return true;
    const QString identity = QtOwnedTempIdentity(left);
    return !identity.isEmpty() && identity == QtOwnedTempIdentity(right);
}

QtArchiveFileResource *LayerResource(const CPanel &source, unsigned level) {
    const auto &parents = source.GetParentFolders();
    IFolderFolder *folder = level + 1 < parents.Size() ? parents[level + 1].ParentFolder.Interface()
                                                       : source.GetFolder();
    const auto *native = dynamic_cast<CAgentFolder *>(folder);
    return native ? dynamic_cast<QtArchiveFileResource *>(native->GetEngineResource()) : nullptr;
}
} // namespace

bool QtWindowWorkspace::CanRefreshPublishedArchives() const {
    if (!Ready() || !QtTaskPresentation::CanPresent())
        return false;
    const auto local = QtTaskPresentation::Application().snapshot();
    if (local.hasWindow && local.phase == QtOperationPhase::Draft)
        return false;
    for (auto *task : QtOperationLauncher::Active())
        if (task->presentation().HasWindow && task->presentation().Phase == QtOperationPhase::Draft)
            return false;
    return true;
}

void QtWindowWorkspace::InvalidateArchive(const QString &path) {
    for (unsigned slot = 0; slot < panels_.size(); ++slot)
        for (auto &tab : panels_[slot].tabs) {
            CPanel *source = tab.runtime ? tab.runtime->source.get() : nullptr;
            const auto *parents = source ? &source->GetParentFolders() : nullptr;
            const bool live = source && source->MarkArchiveOutdated(path);
            const bool saved = (!parents || parents->IsEmpty()) &&
                               SameArchive(path, tab.archiveLocation.physicalPath);
            if (!live && !saved)
                continue;
            ++tab.archiveRevision;
        }
}

void QtWindowWorkspace::ShowArchiveChangeNotice(unsigned panel) {
    auto &ui = window_.panelUi_[panel];
    QtBindLocalText(ui.navigationError, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace",
                                      "The archive changed. Reopen it to see the saved changes. "
                                      "The previous view is available for copying."));
    ui.navigationNotice->show();
}

bool QtWindowWorkspace::RefreshPublishedArchive(unsigned panel, bool interactive) {
    auto &state = panels_[panel];
    if (state.tabs.empty() || !window_.panelUi_[panel].container)
        return false;
    auto &tab = state.tabs[state.current];
    if (tab.archiveRevision == tab.archiveApplied)
        return false;
    if (!Ready() || (!interactive && !CanRefreshPublishedArchives())) {
        window_.guiOutputRefresh_->Enqueue({}, {tab.archiveLocation.physicalPath});
        return true;
    }
    auto &source = window_.app_.Panels[panel];
    if (!source.IsArcFolder() || source.GetParentFolders().IsEmpty()) {
        tab.archiveApplied = tab.archiveRevision;
        return false;
    }
    if (!source.ArchiveContextOutdated()) {
        auto *resource = LayerResource(source, 0);
        const auto reader = resource ? resource->NativeReaderLease() : nullptr;
        if (reader && reader->sourceIsCurrent()) {
            tab.archiveApplied = tab.archiveRevision;
            return false;
        }
        source.MarkArchiveOutdated();
    }
    const auto pending = [&] {
        ShowArchiveChangeNotice(panel);
        window_.refreshActions();
    };
    if (!interactive && tab.archiveAttempted == tab.archiveRevision) {
        pending();
        return true;
    }
    const quint64 revision = tab.archiveRevision;
    tab.archiveAttempted = revision;
    const QtArchiveLocation location = QtArchiveLocation::Capture(
        source,
        static_cast<QtPanelAddressCombo *>(window_.panelUi_[panel].pathCombo)->LocationSegments());
    std::vector<ReopenLayer> layers;
    for (unsigned level = 0; level < source.GetParentFolders().Size(); ++level) {
        auto *resource = LayerResource(source, level);
        const auto reader = resource ? resource->NativeReaderLease() : nullptr;
        if (!reader || reader->resolvedOpenType().isEmpty()) {
            pending();
            return true;
        }
        const auto &link = source.GetParentFolders()[level];
        layers.push_back({reader->resolvedOpenType(),
                          link.UsePassword ? std::optional(link.Password) : std::nullopt});
    }
    if (location.physicalPath.isEmpty() || !source.PreserveOutdatedArchiveCopies()) {
        pending();
        return true;
    }
    tab.archiveLocation = location;
    CSelectedState selection;
    source.SaveSelectedState(selection);
    const auto previousState = window_.panelUi_[panel].state;
    const QPointer<QWidget> focus = QApplication::focusWidget();
    const QPointer<QWidget> active = QApplication::activeWindow();
    const quint64 focusGeneration = focusRequest_;
    auto *view =
        qobject_cast<QAbstractItemView *>(window_.panelUi_[panel].viewStack->currentWidget());
    const QPoint scroll =
        view ? QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value())
             : QPoint();
    window_.withPanelProjection(panel, [&] {
        const QScopedValueRollback<bool> changing(changing_, true);
        const QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
        QtAppOperationState::CallScope call(window_.app_.QtOperations());
        QtPanelLocationTransaction transaction(source);
        const bool dialogs = source.SetArchiveOpenDialogs(interactive);
        const auto restoreDialogs = qScopeGuard([&] { source.SetArchiveOpenDialogs(dialogs); });
        HRESULT result = OpenArchiveLocation(panel, location, &layers, interactive);
        auto *root = source.GetParentFolders().IsEmpty() ? nullptr : LayerResource(source, 0);
        const auto reader = root ? root->NativeReaderLease() : nullptr;
        if (result == S_OK &&
            (!reader || !reader->sourceIsCurrent() || tab.archiveRevision != revision))
            result = E_ABORT;
        if (result == S_OK) {
            transaction.Commit();
            tab.archiveApplied = revision;
            tab.runtime->dirty = false;
            window_.panelUi_[panel].navigationNotice->hide();
        } else {
            window_.cancelListPublication();
            transaction.Rollback();
            window_.setState(previousState);
            window_.renderBoundFolder();
            if (window_.panelUi_[panel].scanningArchive)
                window_.panelUi_[panel].listProjection->waitForFinished();
        }
        window_.restoreListSelection(selection);
        if (view) {
            view->horizontalScrollBar()->setValue(scroll.x());
            view->verticalScrollBar()->setValue(scroll.y());
        }
    });
    if (tab.archiveRevision != tab.archiveApplied)
        pending();
    auto *currentWindow = QApplication::activeWindow();
    if (focusRequest_ == focusGeneration && !QApplication::activeModalWidget() &&
        !qobject_cast<QLineEdit *>(QApplication::focusWidget()) &&
        (!currentWindow || currentWindow == active || currentWindow == &window_)) {
        if (active && active->isVisible() && active->isEnabled())
            QApplication::setActiveWindow(active);
        if (focus && focus->isVisible() && focus->isEnabled())
            focus->setFocus(Qt::OtherFocusReason);
    }
    window_.refreshActions();
    return true;
}
