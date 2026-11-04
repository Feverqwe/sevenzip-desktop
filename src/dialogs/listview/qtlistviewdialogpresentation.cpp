// Qt projection and language bindings for the native list/report dialog.
// Igor Pavlov, LGPL-2.1-or-later; source mapping remains with ListViewDialog.cpp.
#include "archive/diagnostics/archiveerrormessages.h"
#include "dialogs/listview/ListViewDialog.h"
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
    _rowBindings[row].Property =
        CPropertyNameBinding{id, fallback ? std::make_optional<UString>(fallback) : std::nullopt};
}

void CListViewDialog::QtBindDiagnosticRow(unsigned row, const char *name, const char *value) {
    _rowBindings[row].Diagnostic = {name, value};
}

void CListViewDialog::QtBindArchiveDiagnosticValue(unsigned row, const UString &source) {
    _rowBindings[row].ArchiveValue = source;
}

void CListViewDialog::QtBindArchiveFlagsValue(unsigned row, UInt32 flags) {
    _rowBindings[row].ArchiveValue = flags;
}

void CListViewDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _listView) {
        for (int i = 0; i < _listView->topLevelItemCount(); ++i) {
            auto *item = _listView->topLevelItem(i);
            const auto binding = _rowBindings.value(item->data(0, kRowIdentityRole).toUInt());
            const auto *flags = std::get_if<UInt32>(&binding.ArchiveValue);
            const auto *source = std::get_if<UString>(&binding.ArchiveValue);
            if ((flags || source) && unsigned(i) < Values.Size()) {
                const UString value =
                    flags ? GetOpenArcErrorMessage(*flags) : QtArchiveDiagnosticText(*source);
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
            const UString text = GetNameOfProperty(
                property.ID, property.Fallback ? property.Fallback->Ptr() : nullptr);
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
