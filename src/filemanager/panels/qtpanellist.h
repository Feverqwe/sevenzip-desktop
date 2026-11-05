// Qt control adapter for the selection/focus portion of CPanel's CListView.
#pragma once
#include "settings/ViewSettings.h"
#include <QAbstractItemView>
#include <QPointer>
#include <functional>
#include <utility>

class QtPanelList {
  public:
    enum Role {
        OperationPathRole = Qt::UserRole,
        DirectoryRole = Qt::UserRole + 1,
        SortValueRole = Qt::UserRole + 2,
        NameRole = Qt::UserRole + 3,
        PrefixRole = Qt::UserRole + 5,
        SourceIndexRole = Qt::UserRole + 6,
        DropHighlightedRole = Qt::UserRole + 19,
        MarkedRole = Qt::UserRole + 20,
        CutRole = Qt::UserRole + 21
    };
    using SortColumnResolver = std::function<int(PROPID, Int32)>;
    void Attach(QAbstractItemView *view, SortColumnResolver sortColumn = {},
                std::function<void(int)> editLabel = {}) {
        view_ = view;
        sortColumn_ = std::move(sortColumn);
        editLabel_ = std::move(editLabel);
    }
    void EditLabel(int row) {
        if (editLabel_)
            editLabel_(row);
    }
    void SetPresentationView(QAbstractItemView *view) { presentationView_ = view; }
    bool IsBound() const { return view_ && view_->model() && view_->selectionModel(); }
    int GetItemCount() const;
    int GetSelectedCount() const;
    int GetFocusedItem() const;
    bool IsItemSelected(int row) const;
    UString GetItemName(unsigned row) const;
    QString GetItemRelPath(unsigned row) const;
    bool GetItemParam(unsigned row, quintptr &param) const;
    void ClearSelection();
    void DeleteListItems();
    void SelectItem(int row);
    void SetItemState_Selected(int row, bool selected);
    void SelectOnly(int row);
    void SetItemMarked(int row, bool marked);
    static void SetItemState_DropHighlighted(QAbstractItemView *view, int row, bool highlighted);
    void SetItemState_DropHighlighted(int row, bool highlighted) {
        SetItemState_DropHighlighted(view_.data(), row, highlighted);
    }
    void SetSingleSelection(bool enabled);
    void SetFocusedItem(int row);
    void EnsureVisible(int row);
    void SortItems(PROPID property, bool ascending, Int32 raw);
    void Invalidate() {
        if (view_)
            view_->viewport()->update();
        if (presentationView_ && presentationView_ != view_)
            presentationView_->viewport()->update();
    }

  private:
    QPointer<QAbstractItemView> view_;
    QPointer<QAbstractItemView> presentationView_;
    SortColumnResolver sortColumn_;
    std::function<void(int)> editLabel_;
};
