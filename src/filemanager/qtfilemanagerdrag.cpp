#include "filemanager/qtfilemanagerdrag.h"
#include "7zip/UI/FileManager/resource.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "filemanager/views/filetable.h"
#include "filemanager/views/qtfilelisthover.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QStorageInfo>

QtFileManagerDrag::QtFileManagerDrag(MainWindow &window) : QObject(&window), window_(window) {
#ifdef _WIN32
    nativeConverter_ = QtNativePanelDragConverter::ForApplication();
#endif
    window.app_.DropTarget().AttachPresentation(this);
    window.setAcceptDrops(true);
    window.installEventFilter(this);
    for (unsigned i = 0; i < kNumPanelsMax; ++i)
        AttachPanel(i);
}
void QtFileManagerDrag::AttachPanel(unsigned i) {
    if (revoked_ || i >= kNumPanelsMax)
        return;
    window_.app_.Panels[i].AttachDragServices(this);
    const auto &ui = window_.panelUi_[i];
    if (auto *table = dynamic_cast<FileTable *>(ui.table))
        table->dragStarter = [this, i] { StartDrag(i); };
    if (auto *icons = dynamic_cast<FileIconList *>(ui.iconList))
        icons->dragStarter = [this, i] { StartDrag(i); };
    if (ui.table)
        ui.table->viewport()->installEventFilter(this);
    if (ui.iconList)
        ui.iconList->viewport()->installEventFilter(this);
}
QtFileManagerDrag::~QtFileManagerDrag() { Revoke(); }
void QtFileManagerDrag::Revoke() {
    if (revoked_)
        return;
    revoked_ = true;
    rightPress_.reset();
    external_.NativeData.reset();
    window_.app_.DropTarget().ClearState();
    window_.app_.DropTarget().AttachPresentation(nullptr);
    window_.removeEventFilter(this);
    for (unsigned i = 0; i < kNumPanelsMax; ++i) {
        window_.app_.Panels[i].AttachDragServices(nullptr);
        const auto &ui = window_.panelUi_[i];
        if (auto *table = dynamic_cast<FileTable *>(ui.table))
            table->dragStarter = {};
        if (auto *icons = dynamic_cast<FileIconList *>(ui.iconList))
            icons->dragStarter = {};
        if (ui.table)
            ui.table->viewport()->removeEventFilter(this);
        if (ui.iconList)
            ui.iconList->viewport()->removeEventFilter(this);
    }
}
void QtFileManagerDrag::SuspendHover(bool suspended) {
    for (const auto &ui : window_.panelUi_) {
        if (ui.tableHover)
            ui.tableHover->SetSuspended(suspended);
        if (ui.iconHover)
            ui.iconHover->SetSuspended(suspended);
    }
}
void QtFileManagerDrag::StartDrag(unsigned panel, bool rightButton) {
    if (revoked_ || active_ || panel >= kNumPanelsMax || window_.backend_->busy() ||
        window_.sourceArchiveProgress_ || window_.app_.QtOperations().closed() ||
        QApplication::activeModalWidget())
        return;
    window_.activatePanelUi(panel);
    if (rightButton && !window_.app_.Panels[panel].IsFSFolder())
        return; // Native OnDrag permits right-button sources only in the filesystem.
    QScopedValueRollback<bool> active(active_, true);
    SuspendHover(true);
    const auto finishDrag = qScopeGuard([this] {
        window_.app_.DropTarget().ClearState();
        SuspendHover(false);
    });
    {
        QScopedValueRollback<bool> source(window_.sourceArchiveProgress_, true);
        window_.app_.Panels[panel].OnDrag(rightButton);
    }
}
HRESULT QtFileManagerDrag::DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                                      DWORD &effect) {
    const int i = window_.app_.DropTarget().SrcPanelIndex;
    if (i < 0 || i >= int(kNumPanelsMax))
        return E_ABORT;
    const auto &ui = window_.panelUi_[i];
    QWidget *view = ui.viewStack ? ui.viewStack->currentWidget() : ui.table;
    QtPanelDragTransport transport(view);
    return transport.DoDragDrop(data, source, allowed, effect);
}
int QtFileManagerDrag::PanelAtPoint(int x, int y) const {
    for (unsigned i = 0; i < kNumPanelsMax; ++i) {
        QWidget *container = window_.panelUi_[i].container;
        if (container && container->isVisible() &&
            container->rect().contains(container->mapFromGlobal(QPoint(x, y))))
            return int(i);
    }
    return -1;
}
bool QtFileManagerDrag::PanelEnabled(unsigned i) const {
    const auto &ui = window_.panelUi_[i];
    return ui.container && ui.container->isEnabled() && ui.table && ui.table->isEnabled();
}
int QtFileManagerDrag::FolderAtPoint(unsigned i, int x, int y, UString &name) const {
    const auto &ui = window_.panelUi_[i];
    auto *view =
        ui.viewStack ? qobject_cast<QAbstractItemView *>(ui.viewStack->currentWidget()) : nullptr;
    if (!view || !view->viewport()->rect().contains(view->viewport()->mapFromGlobal(QPoint(x, y))))
        return -1;
    const QModelIndex index =
        view->indexAt(view->viewport()->mapFromGlobal(QPoint(x, y))).siblingAtColumn(0);
    if (!index.isValid())
        return -1;
    const UInt32 real = index.data(QtPanelList::SourceIndexRole).toUInt();
    if (real == UInt32(-1) || !window_.app_.Panels[i].IsItem_Folder(real))
        return -1;
    name = window_.app_.Panels[i].GetItemName(real);
    return index.row();
}
void QtFileManagerDrag::Highlight(unsigned i, int item, bool enabled) {
    const auto &ui = window_.panelUi_[i];
    QtPanelList::SetItemState_DropHighlighted(ui.table, item, enabled);
}
bool QtFileManagerDrag::IsSameDrive(const UString &destination,
                                    const UStringVector &sources) const {
    if (sources.IsEmpty())
        return false;
    const QStorageInfo target(ToQString(destination));
    if (!target.isValid() || !target.isReady())
        return false;
    FOR_VECTOR(i, sources) {
        const QStorageInfo source(ToQString(sources[i]));
        if (!source.isValid() || !source.isReady() || source.device() != target.device() ||
            source.rootPath() != target.rootPath())
            return false;
    }
    return true;
}
HRESULT QtFileManagerDrag::ExecuteDrop(CDataObject *data, DWORD keys, const QPoint &position,
                                       DWORD &effect) {
    if (const auto result = HandleDroppedPathChoice(data, keys, effect))
        return *result;
    auto &target = window_.app_.DropTarget();
    CPanel *const owner = target.TargetPanel();
    std::optional<CSelectedState> refreshSelection;
    HRESULT result;
    {
        QScopedValueRollback<bool> source(window_.sourceArchiveProgress_, true);
        result = target.Drop(data, keys, position.x(), position.y(), &effect, &refreshSelection);
    }
    if (refreshSelection)
        for (unsigned index = 0; index < kNumPanelsMax; ++index)
            if (owner == &window_.app_.Panels[index]) {
                window_.withPanelProjection(index,
                                            [&] { owner->RefreshListCtrl(*refreshSelection); });
                window_.appPresentation_->RequestListFocus(index);
                break;
            }
    return result;
}

std::optional<HRESULT> QtFileManagerDrag::HandleDroppedPathChoice(CDataObject *data, DWORD keys,
                                                                  DWORD &effect) {
    const QString path = PathToOpen(data, keys);
    if (path.isEmpty())
        return std::nullopt;
    auto &target = window_.app_.DropTarget();
    CPanel *const panel = target.TargetPanel();
    DroppedPathAction choice;
    {
        QtAppOperationState::CallScope call(window_.app_.QtOperations());
        CPanel::CDisableTimerProcessing2 timer(panel);
        choice = ChooseDroppedPathAction(path);
    }
    if (choice == DroppedPathAction::Compress)
        return std::nullopt;
    target.ClearState();
    effect = DROPEFFECT_NONE;
    if (choice == DroppedPathAction::Cancel)
        return E_ABORT;
    for (unsigned i = 0; i < kNumPanelsMax; ++i)
        if (panel == &window_.app_.Panels[i])
            window_.activatePanelUi(i);
    // Opening never transfers ownership of the dropped item.
    const HRESULT result =
        QFileInfo(path).isDir() ? window_.navigateTo(path) : window_.openArchiveAs(path, {});
    if (result == S_OK)
        effect = DROPEFFECT_COPY;
    return result;
}

QString QtFileManagerDrag::PathToOpen(CDataObject *data, DWORD keys) const {
    const auto &target = window_.app_.DropTarget();
    if (active_ || data != &external_ || target.IsMenuDrop() || (keys & (MK_RBUTTON | MK_MBUTTON)))
        return {};
    const CPanel *panel = target.TargetPanel();
    if (!panel || !panel->IsFSFolder())
        return {};
    UInt32 flags = 0;
    if (data->ReadTransferFlags(flags))
        return {}; // Preserve the native 7-Zip transfer protocol.
    UStringVector names;
    if (data->GetData(names) != S_OK || names.Size() != 1)
        return {};
    const QString path = ToQString(names.Front());
    const QFileInfo file(path);
    const auto catalog = window_.backend_->codecCatalog();
    if (file.isSymLink() || !catalog || catalog->result() != S_OK)
        return {};
    if (!file.isDir() &&
        (!file.isFile() || catalog->nativeCodecs().FindFormatForArchiveName(names.Front()) < 0))
        return {};
    const auto &formats = catalog->nativeCodecs().Formats;
    FOR_VECTOR(i, formats)
    if (formats[i].UpdateEnabled)
        return path;
    return {};
}
