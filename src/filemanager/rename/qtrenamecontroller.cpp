#include "filemanager/rename/qtrenamecontroller.h"
#include "7zip/UI/FileManager/resource.h"
#include "archive/backend/archivebackend.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include <QAbstractItemDelegate>
#include <QFileInfo>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <utility>

namespace {
// Qt equivalent of rejecting the native LV_DISPINFO edit without a reload.
void rejectLabelEdit(CPanel &panel, QTableView &view, UInt32 sourceIndex) {
    for (int row = 0; row < view.model()->rowCount(); ++row)
        if (view.model()->index(row, 0).data(QtPanelList::SourceIndexRole).toUInt() ==
            sourceIndex) {
            view.model()->setData(view.model()->index(row, 0),
                                  ToQString(panel.GetItemName(sourceIndex)), Qt::EditRole);
            break;
        }
    CSelectedState state;
    panel.SaveSelectedState(state);
    panel.RestoreLoadedSelection(state);
}
} // namespace

struct QtRenameController::Request {
    enum class Phase { Editing, Committed, Cancelled };
    Request(unsigned panel, IFolderFolder *folder, const QModelIndex &item, quint32 sourceIndex,
            QString path, QAbstractItemView *view)
        : panel(panel), folder(folder), item(item), sourceIndex(sourceIndex), path(std::move(path)),
          view(view) {}
    const unsigned panel;
    const CMyComPtr<IFolderFolder> folder;
    const QPersistentModelIndex item;
    const quint32 sourceIndex;
    const QString path;
    const QPointer<QAbstractItemView> view;
    QPointer<QLineEdit> editor;
    QString name;
    std::function<void()> refresh;
    Phase phase = Phase::Editing;
};

QtRenameController::QtRenameController(MainWindow &window)
    : QObject(&window), window_(window), completion_(this) {
    completion_.setSingleShot(true);
    connect(&completion_, &QTimer::timeout, this, &QtRenameController::Complete);
}

void QtRenameController::Attach(QAbstractItemView &view, unsigned panel) {
    connect(view.itemDelegate(), &QAbstractItemDelegate::commitData, this,
            [this, view = &view, panel](QWidget *editor) {
                if (!request_ || request_->panel != panel || request_->view != view ||
                    !request_->editor || request_->editor != editor ||
                    request_->phase != Request::Phase::Editing)
                    return;
                request_->name = request_->editor->text();
                request_->phase = Request::Phase::Committed;
                completion_.start();
            });
    connect(view.itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this, view = &view, panel](QWidget *editor, QAbstractItemDelegate::EndEditHint) {
                if (request_ && request_->view == view && request_->editor == editor)
                    Cancel(panel);
            });
}

void QtRenameController::Begin(int row) {
    if (request_ || window_.app_.QtOperations().closed())
        return;
    auto &panel = window_.app_.GetFocusedPanel();
    const bool streams = panel.GetFolder() && panel.IsAltStreamsFolder();
    const bool archive = window_.projectedUi().state == MainWindow::State::Viewing;
    if (window_.projectedUi().state != MainWindow::State::Filesystem && !archive && !streams)
        return;
    if (!window_.commands_->CanExecuteCommand(IDM_RENAME) || row < 0 ||
        row >= window_.projectedUi().table->rowCount() || panel.OnBeginLabelEdit(row))
        return;
    const QModelIndex item = window_.projectedUi().table->model()->index(row, 0);
    if (!item.isValid() || item.data(QtPanelList::SourceIndexRole).isNull())
        return;
    const quint32 sourceIndex = item.data(QtPanelList::SourceIndexRole).toUInt();
    auto *view = window_.projectedUi().viewStack->currentWidget() == window_.projectedUi().iconList
                     ? static_cast<QAbstractItemView *>(window_.projectedUi().iconList)
                     : static_cast<QAbstractItemView *>(window_.projectedUi().table);
    const auto request =
        std::make_shared<Request>(window_.projectedPanel_, panel.GetFolder(), item, sourceIndex,
                                  ToQString(archive ? panel.GetItemRelPath2(sourceIndex)
                                                    : panel.GetItemFullPath(sourceIndex)),
                                  view);
    request_ = request;
    const QPointer<QtRenameController> guard(this);
    auto retireUnopened = qScopeGuard([this, guard, request] {
        if (guard && request_ == request)
            request_.reset();
    });
    view->scrollTo(item);
    if (!guard || request_ != request)
        return;
    if (!request->item.isValid())
        return;
    view->edit(request->item);
    if (!guard || request_ != request)
        return;
    for (auto *editor : view->findChildren<QLineEdit *>())
        if (editor->isVisibleTo(view)) {
            request->editor = editor;
            break;
        }
    if (!request->editor) {
        // Qt may refuse to create an editor. There is no deferred key/edit
        // completion to wait for, so normal activation must remain available.
        return;
    }
    retireUnopened.dismiss();
    if (archive || streams) {
        request->editor->setText(ToQString(panel.GetItemName(sourceIndex)));
        request->editor->selectAll();
    }
}

void QtRenameController::Cancel(unsigned panel) {
    if (!request_ || request_->panel != panel || request_->phase != Request::Phase::Editing)
        return;
    request_->phase = Request::Phase::Cancelled;
    completion_.start();
}

bool QtRenameController::PendingFor(unsigned panel) const {
    return request_ && request_->panel == panel;
}

void QtRenameController::AfterEdit(unsigned panel, std::function<void()> refresh) {
    if (PendingFor(panel))
        request_->refresh = std::move(refresh);
}

void QtRenameController::Stop() {
    completion_.stop();
    request_.reset();
}

void QtRenameController::Complete() {
    const auto request = request_;
    if (!request)
        return;
    const QPointer<QtRenameController> guard(this);
    if (request->phase == Request::Phase::Committed)
        Apply(*request);
    if (guard && request_ == request) {
        request_.reset();
        if (request->refresh)
            request->refresh();
    }
}

void QtRenameController::Apply(const Request &request) {
    if (window_.app_.QtOperations().closed() || window_.app_.QtOperations().blocking() ||
        window_.sourceArchiveProgress_)
        return;
    QtAppOperationState::CallScope call(window_.app_.QtOperations());
    window_.withPanelProjection(request.panel, [&] {
        auto &panel = window_.app_.GetFocusedPanel();
        if (panel.GetFolder() != request.folder || !request.item.isValid() ||
            !panel.ShouldProcessNotifications() ||
            request.sourceIndex >= panel.GetLoadedItemCount())
            return;
        const bool archive = window_.projectedUi().state == MainWindow::State::Viewing;
        const bool streams = panel.IsAltStreamsFolder();
        if (window_.projectedUi().state != MainWindow::State::Filesystem && !archive && !streams)
            return;
        const QString path = ToQString(archive ? panel.GetItemRelPath2(request.sourceIndex)
                                               : panel.GetItemFullPath(request.sourceIndex));
        if (path != request.path)
            return;
        QScopedValueRollback<unsigned> projection(window_.panelProjectionDepth_,
                                                  window_.panelProjectionDepth_ + 1);
        CPanel::CDisableNotify peer(window_.app_.Panels[1 - request.panel]);
        if (request.name == ToQString(panel.GetItemName(request.sourceIndex)))
            return;
        if (!archive && !streams &&
            QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath() !=
                window_.projectedUi().directory)
            return;
        Finish(request, panel);
    });
}

void QtRenameController::Finish(const Request &request, CPanel &panel) {
    QString destinationDirectory;
    QString publishedArchive;
    {
        QScopedValueRollback<bool> source(window_.sourceArchiveProgress_, true);
        if (request.name.contains(QChar::Null) ||
            (panel.IsArcFolder() && !ArchiveBackend::safePath(request.name))) {
            CPanel::CDisableTimerProcessing disableTimer(panel);
            panel.MessageBox_Error_HRESULT(E_INVALIDARG);
        } else
            panel.OnEndLabelEdit(request.sourceIndex, ToUString(request.name),
                                 &destinationDirectory, &publishedArchive);
    }
    const auto selection = panel.TakeRefreshSelection();
    if (!selection) {
        // The source validation return rejects the edit without reloading.
        rejectLabelEdit(panel, *window_.projectedUi().table, request.sourceIndex);
        window_.refreshActions();
        return;
    }
    // The source worker owns operation errors. Its posted reload ignores the
    // publication result and must not add a second rename diagnostic.
    if (window_.renderBoundFolder() == S_OK)
        window_.restoreListSelection(*selection);
    if (!publishedArchive.isEmpty()) {
        if (auto *application = QtWorkspaceApplication::Find())
            application->ArchivePublished(publishedArchive, &panel);
        else
            window_.ArchivePublished(publishedArchive, &panel);
    }
    if (!destinationDirectory.isEmpty()) {
        const QStringList changed{QFileInfo(request.path).absolutePath(), destinationDirectory};
        if (auto *application = QtWorkspaceApplication::Find())
            application->FilesystemPublished(changed);
        else
            window_.guiOutputRefresh_->Enqueue(changed, {});
    }
}
