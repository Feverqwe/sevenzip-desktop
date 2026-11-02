#include "filemanager/panels/Panel.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include <QFileInfo>
#include <QScopedValueRollback>

namespace {
// Qt equivalent of rejecting the native LV_DISPINFO edit without a reload.
void rejectLabelEdit(CPanel &panel, QTableView &view, UInt32 sourceIndex) {
    for (int row = 0; row < view.model()->rowCount(); ++row)
        if (view.model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == sourceIndex) {
            view.model()->setData(view.model()->index(row, 0),
                                  ToQString(panel.GetItemName(sourceIndex)), Qt::EditRole);
            break;
        }
    CSelectedState state;
    panel.SaveSelectedState(state);
    panel.RestoreLoadedSelection(state);
}

} // namespace

void MainWindow::connectRenameEditor(QAbstractItemView &view) {
    renameController_->Attach(view, projectedPanel_);
}

void MainWindow::beginRename(int row) { renameController_->Begin(row); }

void MainWindow::finishFilesystemRename(quint32 sourceIndex, const QString &originalPath,
                                        const QString &name) {
    const bool archive = projectedUi().state == State::Viewing;
    auto &panel = app_.GetFocusedPanel();
    const bool streams = panel.GetFolder() && panel.IsAltStreamsFolder();
    if (projectedUi().state != State::Filesystem && !archive && !streams)
        return;
    if (!archive && !streams && QFileInfo(originalPath).absolutePath() != projectedUi().directory &&
        !panel.GetFlatMode())
        return;
    {
        QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
        if (name.contains(QChar::Null)) {
            CPanel::CDisableTimerProcessing disableTimer(panel);
            panel.MessageBox_Error_HRESULT(E_INVALIDARG);
        } else
            panel.OnEndLabelEdit(sourceIndex, ToUString(name));
    }
    const auto selection = panel.TakeRefreshSelection();
    if (!selection) {
        // The source validation return rejects the edit without reloading.
        rejectLabelEdit(panel, *projectedUi().table, sourceIndex);
        refreshActions();
        return;
    }
    // The source worker owns operation errors. Its posted reload ignores the
    // publication result and must not add a second rename diagnostic.
    if (renderBoundFolder() == S_OK)
        restoreListSelection(*selection);
}
