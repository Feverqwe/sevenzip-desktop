#include "filemanager/panels/qtpanellist.h"
#include <QItemSelectionModel>
#include <QListView>
#include <QScrollBar>
#include <QTableView>

int QtPanelList::GetItemCount() const { return IsBound() ? view_->model()->rowCount() : 0; }
int QtPanelList::GetFocusedItem() const {
    return IsBound() ? view_->selectionModel()->currentIndex().row() : -1;
}
bool QtPanelList::IsItemSelected(int row) const {
    return IsBound() && view_->selectionModel()->isRowSelected(row, QModelIndex());
}
UString QtPanelList::GetItemName(unsigned row) const {
    const QModelIndex index = view_->model()->index((int)row, 0);
    const QVariant name = index.data(NameRole);
    return ToUString(name.isValid() ? name.toString() : index.data().toString());
}
QString QtPanelList::GetItemRelPath(unsigned row) const {
    return view_->model()->index((int)row, 0).data().toString();
}
bool QtPanelList::GetItemParam(unsigned row, quintptr &param) const {
    if (!IsBound())
        return false;
    const QModelIndex index = view_->model()->index(int(row), 0);
    if (!index.isValid())
        return false;
    const QVariant value = index.data(SourceIndexRole);
    if (!value.isValid()) {
        param = row; // Fixtures/legacy snapshots without an explicit source param.
        return true;
    }
    bool ok;
    param = value.toULongLong(&ok);
    return ok;
}

void QtPanelList::ClearSelection() {
    if (IsBound())
        view_->selectionModel()->clearSelection();
}

void QtPanelList::DeleteListItems() {
    if (IsBound())
        view_->model()->removeRows(0, GetItemCount());
}
void QtPanelList::SelectItem(int row) {
    view_->selectionModel()->select(view_->model()->index(row, 0),
                                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
}
void QtPanelList::SetSelectedRows(const std::function<bool(int)> &isSelected) {
    if (!IsBound())
        return;
    const auto *model = view_->model();
    const int count = model->rowCount();
    QItemSelection selection;
    int first = -1;
    for (int row = 0; row < count; ++row) {
        if (isSelected(row)) {
            if (first < 0)
                first = row;
        } else if (first >= 0) {
            selection.select(model->index(first, 0), model->index(row - 1, 0));
            first = -1;
        }
    }
    if (first >= 0)
        selection.select(model->index(first, 0), model->index(count - 1, 0));
    view_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect |
                                                   QItemSelectionModel::Rows);
}
void QtPanelList::SetItemState_DropHighlighted(QAbstractItemView *view, int row, bool highlighted) {
    if (!view || !view->model() || row < 0 || row >= view->model()->rowCount())
        return;
    for (int column = 0; column < view->model()->columnCount(); ++column)
        view->model()->setData(view->model()->index(row, column), highlighted, DropHighlightedRole);
}
void QtPanelList::SetFocusedItem(int row, bool keepColumn) {
    int column = 0;
    if (const auto *table = qobject_cast<QTableView *>(view_.data()); keepColumn && table) {
        const int current = table->currentIndex().column();
        if (current >= 0 && !table->isColumnHidden(current))
            column = current;
    }
    view_->selectionModel()->setCurrentIndex(view_->model()->index(row, column),
                                             QItemSelectionModel::NoUpdate);
}
void QtPanelList::EnsureVisible(int row, bool preserveHorizontal) {
    if (!IsBound() || row < 0 || row >= GetItemCount())
        return;
    auto *view = presentationView_ && presentationView_->model() == view_->model()
                     ? presentationView_.data()
                     : view_.data();
    const auto *list = qobject_cast<QListView *>(view);
    const int horizontalPosition = view->horizontalScrollBar()->value();
    view->scrollTo(view->model()->index(row, list ? list->modelColumn() : 0));
    if (preserveHorizontal && !list)
        view->horizontalScrollBar()->setValue(horizontalPosition);
}

void QtPanelList::SortItems(PROPID property, bool ascending, Int32 raw) {
    auto *table = qobject_cast<QTableView *>(view_.data());
    if (!IsBound() || !table || !sortColumn_)
        return;
    const int column = sortColumn_(property, raw);
    if (column >= 0 && column < table->model()->columnCount()) {
        table->sortByColumn(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        auto *visibleView = presentationView_ && presentationView_->model() == view_->model()
                                ? presentationView_.data()
                                : view_.data();
        visibleView->keyboardSearch({});
    }
}
