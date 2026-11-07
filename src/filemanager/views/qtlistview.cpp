// Qt presentation adapter for Win32 CListView styles used by ListViewDialog.
#include "filemanager/views/qtlistview.h"

CListView::CListView(QWidget *parent) : QTreeWidget(parent) {
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setTextElideMode(Qt::ElideNone);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
}
