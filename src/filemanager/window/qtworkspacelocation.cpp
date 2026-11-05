#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopedValueRollback>
#include <QScrollBar>

void QtWindowWorkspace::RestoreViewPosition(CPanel &source, QAbstractItemView &view,
                                            const QPoint &position) {
    const UInt64 revision = source.FolderBindingRevision();
    const auto request = focusRequest_;
    WhenReady([this, owner = QPointer<CPanel>(&source), target = QPointer<QAbstractItemView>(&view),
               position, revision, request] {
        if (focusRequest_ != request || !owner || owner->FolderBindingRevision() != revision ||
            !target || !target->isVisible())
            return;
        target->horizontalScrollBar()->setValue(position.x());
        target->verticalScrollBar()->setValue(position.y());
    });
}

void QtWindowWorkspace::CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result) {
    auto &ui = window_.panelUi_[panel];
    auto &source = window_.app_.Panels[panel];
    source.LoadFullPath();
    const QString actual = ToQString(source.GetCurrentFolderPrefix());
    const auto normalized = [](const QString &value) {
        return value.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(value));
    };
    bool matched = normalized(actual) == normalized(path);
    if (!matched && QFileInfo(path).isDir())
        matched = QFileInfo(path).canonicalFilePath() == QFileInfo(actual).canonicalFilePath();
    auto &tab = panels_[panel].tabs[panels_[panel].current];
    tab.unavailable = result != S_OK || !matched;
    ui.locationUnavailable = tab.unavailable;
    if (!tab.unavailable)
        return;
    ui.filesystemScanner->cancelAndWait();
    window_.cancelListPublication();
    window_.setState(ui.state);
    ui.table->clearRows();
    window_.showPanelStatus(panel, {});
    ui.path->setText(path);
    ui.navigation->SetLocation(path);
    const QString message = result == E_ABORT ? tr("Opening canceled: %1").arg(path)
                                              : tr("Location unavailable: %1").arg(path);
    ui.navigationError->setText(message);
    ui.navigationNotice->show();
    window_.statusBar()->showMessage(message);
}

void QtWindowWorkspace::RetryLocation(unsigned panel, bool parent) {
    if (!Ready())
        return;
    window_.activatePanelUi(panel);
    auto &tab = panels_[panel].tabs[panels_[panel].current];
    if (parent)
        tab.path = QFileInfo(QDir::cleanPath(tab.path)).absolutePath();
    {
        const QScopedValueRollback<bool> changing(changing_, true);
        const QString path = tab.path;
        const HRESULT result = window_.navigateTo(path, true);
        CompleteLocationOpen(panel, path, result);
    }
    window_.refreshActions();
}
