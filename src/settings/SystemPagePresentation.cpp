#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "localization/qtlangcontrols.h"
#include "settings/SystemPage.h"
#include "ui/controls/qtdisclosure.h"
#include <QApplication>
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

bool CSystemPage::IntegrationAvailable() const {
    return _services && (_services->CanApply(0) || _services->CanApply(1));
}

void CSystemPage::SelectFormat(unsigned index, bool selected) {
    if ((_items[index].Pair[0].State == kExtState_7Zip) == selected)
        return;
    const int before = _items[index].Pair[0].State;
    {
        // One checkbox choice can traverse two source states. Publish only
        // the completed choice: Changed observers may synchronously Apply.
        const QSignalBlocker blocked(this);
        CUIntVector indices;
        indices.Add(index);
        const bool restoreOther = !selected && _items[index].Pair[0].OldState == kExtState_Other;
        ChangeState(0, indices);
        // Undoing a pending checkbox choice must not request deletion of the
        // handler that was present before Apply. The source cycle owns restoration.
        if ((selected && _items[index].Pair[0].State != kExtState_7Zip) ||
            (restoreOther && _items[index].Pair[0].State == kExtState_Clear))
            ChangeState(0, indices);
    }
    if (_items[index].Pair[0].State != before)
        emit Changed();
}

void CSystemPage::SelectFormats(bool selected) {
    bool changed = false;
    {
        const QSignalBlocker blocked(this);
        for (int i = 0; i < _formatChoices.size(); ++i) {
            if (!_formatChoices[i]->isEnabled())
                continue;
            const int before = _items[unsigned(i)].Pair[0].State;
            SelectFormat(unsigned(i), selected);
            changed |= before != _items[unsigned(i)].Pair[0].State;
        }
    }
    if (changed)
        emit Changed();
}

void CSystemPage::FilterFormats() {
    if (!_formatList)
        return;
    const QString query = _formatSearch->text().trimmed();
    int visible = 0;
    for (int i = 0; i < _formatList->topLevelItemCount(); ++i) {
        auto *row = _formatList->topLevelItem(i);
        const bool matches = row->text(0).contains(query, Qt::CaseInsensitive) ||
                             row->text(1).contains(query, Qt::CaseInsensitive);
        row->setHidden(!matches);
        visible += matches;
    }
    _formatEmpty->setVisible(visible == 0);
    _formatList->setVisible(visible != 0);
}

void CSystemPage::RefreshFormatChoices() {
    int selected = 0;
    bool scopeChanged = false;
    for (int i = 0; i < _formatChoices.size(); ++i) {
        auto *choice = _formatChoices[i];
        const QSignalBlocker blocked(choice);
        const auto &extension = _extDB.Exts[unsigned(i)].Ext;
        const QString ext = QString::fromWCharArray(extension.Ptr());
        choice->setChecked(_items[unsigned(i)].Pair[0].State == kExtState_7Zip);
        choice->setEnabled(_services && _services->CanApplyFormat(0, extension));
        choice->setToolTip(_listView->topLevelItem(i)->toolTip(1));
        selected += choice->isChecked();
        _formatList->topLevelItem(i)->setText(
            0, QCoreApplication::translate("SystemPage", "%1 Archive").arg(ext.toUpper()));
        _formatList->topLevelItem(i)->setText(1, QStringLiteral(".") + ext);
        for (const auto &state : _items[unsigned(i)].Pair)
            scopeChanged |= state.State != state.OldState;
    }
    if (_formatCount) {
        _formatCount->setText(QCoreApplication::translate("SettingsRedesign", "%n selected formats",
                                                          nullptr, selected));
        _formatList->setHeaderLabels(
            {QtLangText(IDS_PROP_FILE_TYPE, "File type"), QString(), QStringLiteral("Sevenzip")});
        _formatSearch->setPlaceholderText(
            QCoreApplication::translate("SettingsRedesign", "Search by name or extension"));
        for (const char *name : {"associationSelectAll", "associationClearAll"})
            findChild<QPushButton *>(name)->setEnabled(_current->isEnabled());
        FilterFormats();
    }
    if (auto *scope = findChild<QtDisclosure *>("associationDetails"))
        scope->setSummary(scopeChanged ? QCoreApplication::translate("SettingsRedesign", "Changed")
                                       : QString());
}

void CSystemPage::SimplifyPresentation() {
    auto *details = new QWidget(this);
    details->setLayout(layout());
    details->layout()->setContentsMargins(0, 0, 0, 0);
    _label->hide();
    _listView->setColumnHidden(2, !_services || !_services->CanApply(1));
    _listView->setMinimumHeight(fontMetrics().lineSpacing() * 8);
    QtBindLocalText(_current, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Change selected"));
    QtBindLocalText(_all, "SettingsRedesign", QT_TRANSLATE_NOOP("SettingsRedesign", "All users"));
    auto *fields = new QVBoxLayout(this);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(12);
    auto *heading = new QLabel(this);
    heading->setWordWrap(true);
    heading->setForegroundRole(QPalette::PlaceholderText);
    QtBindLocalText(
        heading, "SettingsRedesign",
        QT_TRANSLATE_NOOP("SettingsRedesign", "Choose file types to open with Sevenzip."));
    fields->addWidget(heading);
    _formatSearch = new QLineEdit(this);
    _formatSearch->setObjectName("associationSearch");
    _formatSearch->setClearButtonEnabled(true);
    QtBindLocalText(_formatSearch, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Search formats"), true);
    fields->addWidget(_formatSearch);
    _formatList = new QTreeWidget(this);
    _formatList->setObjectName("associationChoices");
    _formatList->setRootIsDecorated(false);
    _formatList->setColumnCount(3);
    _formatList->header()->setStretchLastSection(false);
    _formatList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _formatList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _formatList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _formatList->setMinimumHeight(fontMetrics().lineSpacing() * 9);
    fields->addWidget(_formatList);
    FOR_VECTOR(i, _extDB.Exts) {
        const QString ext = QString::fromWCharArray(_extDB.Exts[i].Ext.Ptr());
        auto *row = new QTreeWidgetItem(_formatList);
        auto *cell = new QWidget(_formatList);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(8, 4, 8, 4);
        auto *choice = new QCheckBox(cell);
#ifdef Q_OS_MACOS
        choice->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
        choice->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        choice->ensurePolished();
        choice->setObjectName("association-" + ext);
        QtBindLocalText(choice, "SettingsRedesign",
                        QT_TRANSLATE_NOOP("SettingsRedesign", "Open .%1 with Sevenzip"), true,
                        {ext});
        cellLayout->addStretch();
        cellLayout->addWidget(choice);
        cellLayout->addStretch();
        row->setForeground(1, palette().brush(QPalette::PlaceholderText));
        _formatList->setItemWidget(row, 2, cell);
        row->setSizeHint(0, QSize(0, choice->sizeHint().height() + 8));
        _formatChoices.append(choice);
        connect(choice, &QCheckBox::clicked, this,
                [this, i](bool selected) { SelectFormat(i, selected); });
    }
    _formatEmpty = new QLabel(this);
    _formatEmpty->setObjectName("associationEmpty");
    _formatEmpty->setWordWrap(true);
    QtBindLocalText(_formatEmpty, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign",
                                      "No matching formats. Try another name or extension."));
    fields->addWidget(_formatEmpty);
    auto *actions = new QHBoxLayout;
    for (bool selected : {true, false}) {
        auto *button = new QPushButton(this);
        button->setObjectName(selected ? "associationSelectAll" : "associationClearAll");
        QtBindLocalText(button, "SettingsRedesign",
                        selected ? QT_TRANSLATE_NOOP("SettingsRedesign", "Select all formats")
                                 : QT_TRANSLATE_NOOP("SettingsRedesign", "Clear selection"));
        connect(button, &QPushButton::clicked, this, [this, selected] { SelectFormats(selected); });
        actions->addWidget(button);
    }
    _formatCount = new QLabel(this);
    _formatCount->setObjectName("associationCount");
    _formatCount->setForegroundRole(QPalette::PlaceholderText);
    actions->addStretch();
    actions->addWidget(_formatCount);
    fields->addLayout(actions);
    auto *scope = new QtDisclosure(details, this);
    scope->setObjectName("associationDetails");
    QtBindLocalText(scope->button(), "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Advanced associations"));
    fields->addWidget(scope);
    scope->setVisible(_services && _services->CanApply(1));
    connect(_formatSearch, &QLineEdit::textChanged, this, &CSystemPage::FilterFormats);
    auto *toggle = new QShortcut(QKeySequence(Qt::Key_Space), _formatList);
    toggle->setContext(Qt::WidgetShortcut);
    connect(toggle, &QShortcut::activated, this, [this] {
        const int i = _formatList->indexOfTopLevelItem(_formatList->currentItem());
        if (i >= 0)
            _formatChoices[i]->click();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        const int index = _formatChoices.indexOf(qobject_cast<QCheckBox *>(now));
        if (index >= 0)
            _formatList->scrollToItem(_formatList->topLevelItem(index));
    });
    RefreshFormatChoices();
}
