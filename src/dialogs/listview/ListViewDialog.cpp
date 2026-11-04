// Adapted from 7-Zip CPP/7zip/UI/FileManager/ListViewDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt replaces Win32 controls/clipboard/OnSize and activation notifications.
#include "dialogs/listview/ListViewDialog.h"
#include "dialogs/edit/EditDialog.h"
#include "filemanager/views/qtlistview.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPushButton>

static void ListView_GetSelected(CListView &listView, CUIntVector &vector) {
    vector.Clear();
    int index = -1;
    for (;;) {
        index = listView.GetNextSelectedItem(index);
        if (index < 0)
            break;
        vector.Add((unsigned)index);
    }
}

void CListViewDialog::CopyToClipboard(bool all) {
    CUIntVector indexes;
    if (all) {
        for (unsigned i = 0; i < Strings.Size(); ++i)
            indexes.Add(i);
    } else
        ListView_GetSelected(*_listView, indexes);
    UString s;
    FOR_VECTOR(i, indexes) {
        unsigned index = indexes[i];
        s += Strings[index];
        if (NumColumns > 1 && index < Values.Size()) {
            const UString &v = Values[index];
            s += ": ";
            s += v;
        }
#ifdef _WIN32
        s += "\r\n";
#else
        s += "\n";
#endif
    }
    QApplication::clipboard()->setText(ToQString(s));
}

void CListViewDialog::ShowItemInfo() {
    CUIntVector indexes;
    ListView_GetSelected(*_listView, indexes);
    if (indexes.Size() != 1)
        return;
    unsigned index = indexes[0];
    CEditDialog dlg(this);
    if (NumColumns == 1)
        dlg.Text = Strings[index];
    else {
        dlg.Title = Strings[index];
        if (index < Values.Size())
            dlg.Text = Values[index];
    }
#ifdef _WIN32
    if (dlg.Text.Find(L'\r') < 0)
        dlg.Text.Replace(L"\n", L"\r\n");
#endif
    dlg.OnInit();
    if (NumColumns > 1) {
        auto *item = _listView->topLevelItem(int(index));
        const auto binding = _rowBindings.value(item->data(0, kRowIdentityRole).toUInt());
        if (binding.Property && binding.Property->ID < 1000)
            QtBindLangText(&dlg, 1000 + binding.Property->ID, ToQString(Strings[index]));
    }
    dlg.setObjectName("listItemInfoDialog");
    dlg.exec();
}

void CListViewDialog::DeleteItems() {
    for (;;) {
        const int index = _listView->GetNextSelectedItem(-1);
        if (index < 0)
            break;
        StringsWereChanged = true;
        _listView->DeleteItem((unsigned)index);
        if ((unsigned)index < Strings.Size())
            Strings.Delete((unsigned)index);
        if ((unsigned)index < Values.Size())
            Values.Delete((unsigned)index);
    }
    const int focusedIndex = _listView->GetFocusedItem();
    if (focusedIndex >= 0)
        _listView->SetItemState_FocusedSelected(focusedIndex);
    _listView->SetColumnWidthAuto(0);
    if (auto *copy = findChild<QPushButton *>("copyReportButton"))
        copy->setEnabled(!Strings.IsEmpty());
}

void CListViewDialog::OnEnter() {
    if (QApplication::keyboardModifiers().testFlag(Qt::AltModifier) || NumColumns > 1) {
        ShowItemInfo();
        return;
    }
    OnOK();
}

bool CListViewDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _listView && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers();
        switch (key->key()) {
        case Qt::Key_Delete:
            if (!DeleteIsAllowed)
                return false;
            DeleteItems();
            return true;
        case Qt::Key_A:
            if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
                _listView->selectAll();
                return true;
            }
            break;
        case Qt::Key_Insert:
        case Qt::Key_C:
            if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
                CopyToClipboard();
                return true;
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            OnEnter();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CListViewDialog::OnOK() {
    FocusedItemIndex = _listView->GetFocusedItem();
    QDialog::accept();
}

void CListViewDialog::accept() { OnOK(); }
