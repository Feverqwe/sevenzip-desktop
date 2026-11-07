// Qt projection and language bindings for the native list/report dialog.
// Igor Pavlov, LGPL-2.1-or-later; source mapping remains with ListViewDialog.cpp.
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/views/qtlistview.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>

static const unsigned kOneStringMaxSize = 1024;

static UString ListValueText(UString value) {
    if (value.Len() > kOneStringMaxSize) {
        value.DeleteFrom(kOneStringMaxSize);
        value += " ...";
    }
    value.Replace(L"\r\n", L" ");
    value.Replace(L"\n", L" ");
    return value;
}

CListViewDialog::CListViewDialog(QWidget *parent) : QDialog(parent) {}

bool CListViewDialog::OnInit() {
    setObjectName("foldersHistoryDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowTitle(ToQString(Title));
    if (TitleID)
        QtBindLangText(this, *TitleID);
    _listView = new CListView(this);
    _listView->setObjectName(NumColumns > 1 ? "propertiesList" : "foldersHistoryList");
    _listView->setColumnCount((int)NumColumns);
    _listView->setHeaderHidden(NumColumns <= 1);
    _listView->setHeaderLabels(NumColumns > 1 ? QStringList{"", ""} : QStringList{""});
    _listView->setColumnWidth(0, NumColumns > 1 ? 100 : 400);
    if (NumColumns > 1)
        _listView->setColumnWidth(1, 300);
    _listView->installEventFilter(this);
    FOR_VECTOR(i, Strings) {
        _listView->InsertItem(i, Strings[i]);
        _listView->topLevelItem(int(i))->setData(0, kRowIdentityRole, i);
        if (NumColumns > 1 && i < Values.Size()) {
            _listView->SetSubItem(i, 1, ListValueText(Values[i]));
        }
    }
    if (SelectFirst && Strings.Size() > 0)
        _listView->SetItemState_FocusedSelected(0);
    _listView->SetColumnWidthAuto(0);
    if (NumColumns > 1)
        _listView->SetColumnWidthAuto(1);
    StringsWereChanged = false;
    const bool report = NumColumns > 1;
    auto *buttons = new QtLangButtonBox(
        report ? QDialogButtonBox::Close : QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("listViewButtons");
    if (report) {
        auto *copy = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
        copy->setObjectName("copyReportButton");
        copy->setEnabled(!Strings.IsEmpty());
        QtBindLocalText(copy, "DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Copy"));
        connect(copy, &QPushButton::clicked, this,
                [this] { CopyToClipboard(_listView->selectedItems().isEmpty()); });
        buttons->button(QDialogButtonBox::Close)->setDefault(true);
        QWidget::setTabOrder(_listView, copy);
        QWidget::setTabOrder(copy, buttons->button(QDialogButtonBox::Close));
        // Report callers retain the original OK result and focused row on Close.
        connect(buttons, &QDialogButtonBox::rejected, this, &CListViewDialog::OnOK);
    } else {
        buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Open"));
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    QtCreateDialogLayout(*this, *buttons)->addWidget(_listView, 1);
    connect(buttons, &QDialogButtonBox::accepted, this, &CListViewDialog::OnOK);
    connect(_listView, &QTreeWidget::itemDoubleClicked, this, &CListViewDialog::OnEnter);
    resize((report ? QSize(640, 420) : QSize(560, 360)).expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
    _listView->setFocus();
    return true;
}

void CListViewDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _listView) {
        for (int i = 0; i < _listView->topLevelItemCount(); ++i) {
            auto *item = _listView->topLevelItem(i);
            const auto row = item->data(0, kRowIdentityRole).toUInt();
            RefreshRow(unsigned(i), row);
            if (unsigned(i) < Strings.Size())
                item->setText(0, ToQString(Strings[unsigned(i)]));
            if (NumColumns > 1 && unsigned(i) < Values.Size())
                item->setText(1, ToQString(ListValueText(Values[unsigned(i)])));
        }
        _listView->SetColumnWidthAuto(0);
        if (NumColumns > 1)
            _listView->SetColumnWidthAuto(1);
    }
    QDialog::changeEvent(event);
}
