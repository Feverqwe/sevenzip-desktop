// Adapted from 7-Zip CPP/7zip/UI/FileManager/ListViewDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt replaces Win32 controls/clipboard/OnSize and activation notifications.
#include "dialogs/listview/ListViewDialog.h"
#include "dialogs/edit/EditDialog.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/PropertyName.h"
#include "filemanager/views/qtlistview.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

static const unsigned kOneStringMaxSize = 1024;
static constexpr int kRowBindingRole = Qt::UserRole + 14;

static UString ListValueText(UString value) {
    if (value.Len() > kOneStringMaxSize) {
        value.DeleteFrom(kOneStringMaxSize);
        value += " ...";
    }
    value.Replace(L"\r\n", L" ");
    value.Replace(L"\n", L" ");
    return value;
}

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

CListViewDialog::CListViewDialog(QWidget *parent) : QDialog(parent) {}

bool CListViewDialog::OnInit() {
    setObjectName("foldersHistoryDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowTitle(ToQString(Title));
    _listView = new CListView(this);
    _listView->setObjectName(NumColumns > 1 ? "propertiesList" : "foldersHistoryList");
    _listView->setColumnCount((int)NumColumns);
    _listView->setHeaderHidden(NumColumns <= 1);
    _listView->setHeaderLabels(NumColumns > 1 ? QStringList{"", ""} : QStringList{""});
    _listView->setColumnWidth(0, NumColumns > 1 ? 100 : 400);
    if (NumColumns > 1)
        _listView->setColumnWidth(1, 300);
    CFmSettings st;
    st.Load(SettingsPath.isEmpty() ? FmSettingsPath() : SettingsPath);
    if (st.SingleClick)
        _listView->SetSingleClickActivation(true);
    _listView->installEventFilter(this);
    FOR_VECTOR(i, Strings) {
        _listView->InsertItem(i, Strings[i]);
        const auto binding = _rowBindings.constFind(i);
        if (binding != _rowBindings.cend())
            _listView->topLevelItem(int(i))->setData(0, kRowBindingRole,
                                                     QVariant::fromValue(*binding));
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
    if (st.SingleClick)
        connect(_listView, &QTreeWidget::itemClicked, this, [this] {
            const auto modifiers = QApplication::keyboardModifiers();
            if (!(modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::ShiftModifier)))
                OnEnter();
        });
    else
        connect(_listView, &QTreeWidget::itemDoubleClicked, this, &CListViewDialog::OnEnter);
    resize((report ? QSize(640, 420) : QSize(560, 360)).expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
    _listView->setFocus();
    return true;
}

void CListViewDialog::QtBindPropertyName(unsigned row, UInt32 id, const wchar_t *fallback) {
    _rowBindings[row].Property = CPropertyNameBinding{
        id, fallback ? ToQString(UString(fallback)) : QString(), fallback != nullptr};
}

void CListViewDialog::QtBindDiagnosticRow(unsigned row, const char *name, const char *value) {
    _rowBindings[row].Diagnostic = {name, value};
}

void CListViewDialog::QtBindArchiveDiagnosticValue(unsigned row, const UString &source) {
    _rowBindings[row].ArchiveValue = ToQString(source);
}

void CListViewDialog::QtBindArchiveFlagsValue(unsigned row, UInt32 flags) {
    _rowBindings[row].ArchiveValue = QVariant::fromValue(quint32(flags));
}

void CListViewDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _listView) {
        for (int i = 0; i < _listView->topLevelItemCount(); ++i) {
            auto *item = _listView->topLevelItem(i);
            const auto binding = item->data(0, kRowBindingRole).value<CRowBinding>();
            const QVariant &archiveValue = binding.ArchiveValue;
            if (archiveValue.isValid() && unsigned(i) < Values.Size()) {
                const UString value =
                    archiveValue.metaType().id() == QMetaType::UInt
                        ? GetOpenArcErrorMessage(archiveValue.toUInt())
                        : QtArchiveDiagnosticText(ToUString(archiveValue.toString()));
                Values[unsigned(i)] = value;
                item->setText(1, ToQString(ListValueText(value)));
            }
            for (int column = 0; column < 2; ++column) {
                const QByteArray &key =
                    column == 0 ? binding.Diagnostic.first : binding.Diagnostic.second;
                if (key.isEmpty())
                    continue;
                const UString text = QtDiagnosticText(key.constData());
                auto &values = column == 0 ? Strings : Values;
                if (unsigned(i) < values.Size())
                    values[unsigned(i)] = text;
                item->setText(column, ToQString(text));
            }
            if (!binding.Property)
                continue;
            const auto &property = *binding.Property;
            const UString fallback = ToUString(property.Fallback);
            const UString text =
                GetNameOfProperty(property.ID, property.HasFallback ? fallback.Ptr() : nullptr);
            if (unsigned(i) < Strings.Size())
                Strings[unsigned(i)] = text;
            item->setText(0, ToQString(text));
        }
        _listView->SetColumnWidthAuto(0);
        if (NumColumns > 1)
            _listView->SetColumnWidthAuto(1);
    }
    QDialog::changeEvent(event);
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
        const auto binding = item->data(0, kRowBindingRole).value<CRowBinding>();
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
