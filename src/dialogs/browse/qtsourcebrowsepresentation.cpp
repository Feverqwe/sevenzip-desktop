// Qt presentation and bindings for the source long-path browser.
// Adapted from 7-Zip UI/FileManager/BrowseDialog.cpp and resources.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/BrowseDialogRes.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileFind.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "dialogs/browse/qtsourcebrowsedialog_p.h"
#include "filemanager/FormatUtils.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtinlinefoldername.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/theme/qtcommandicons.h"
#include <QComboBox>
#include <QEvent>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace NSourceBrowse {
using NWindows::NFile::NFind::CFileInfo;

int CBrowseDialog::GetRealItemIndex(QTreeWidgetItem *item) const {
    return item ? item->data(0, Qt::UserRole).toInt() : kParentIndex;
}
QTreeWidgetItem *CBrowseDialog::SelectedItem() const {
    const auto items = _list->selectedItems();
    return items.isEmpty() ? nullptr : items.front();
}

class BrowseRow final : public QTreeWidgetItem {
    const CBrowseDialog &_dialog;

  public:
    BrowseRow(QTreeWidget *list, const CBrowseDialog &dialog, int index)
        : QTreeWidgetItem(list), _dialog(dialog) {
        setData(0, Qt::UserRole, index);
    }
    bool operator<(const QTreeWidgetItem &other) const override {
        return _dialog.CompareItems(data(0, Qt::UserRole).toInt(),
                                    other.data(0, Qt::UserRole).toInt()) < 0;
    }
};

void CBrowseDialog::SetFolderText() {
    const QString fullPath = ToQString(DirPrefix);
    _folder->setText(_folder->fontMetrics().elidedText(
        fullPath, Qt::ElideMiddle, qMax(0, _folder->contentsRect().width() - 2)));
    _folder->setToolTip(fullPath);
    _folder->setAccessibleName(fullPath);
}

void CBrowseDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _list)
        _list->setHeaderLabels(
            {QtLangText(IDS_PROP_NAME), QtLangText(IDS_PROP_MTIME), QtLangText(IDS_PROP_SIZE)});
    QDialog::changeEvent(event);
}

void CBrowseDialog::PopulateRows(const UString &selectedName) {
    SetFolderText();
    const QSignalBlocker blocker(_list);
    _list->setUpdatesEnabled(false);
    _list->clear();
    QTreeWidgetItem *cursor = nullptr;
    if (_showDots && _topDirPrefix != DirPrefix) {
        auto *item = new BrowseRow(_list, *this, kParentIndex);
        item->setText(0, "..");
        item->setIcon(0, _extToIconMap.GetIcon(FILE_ATTRIBUTE_DIRECTORY, ToQString(DirPrefix)));
        if (selectedName.IsEmpty())
            cursor = item;
    }
    FOR_VECTOR(i, _files) {
        const CFileInfo &fi = _files[i];
        const UString name = fs2us(fi.Name);
        auto *item = new BrowseRow(_list, *this, (int)i);
        item->setText(0, ToQString(name));
        item->setIcon(0, _extToIconMap.GetIcon(fi.GetWinAttrib(), ToQString(DirPrefix + name)));
        if (!selectedName.IsEmpty() && CompareFileNames(name, selectedName) == 0)
            cursor = item;
        wchar_t s[64] = {};
        FILETIME time;
        FiTime_To_FILETIME(fi.MTime, time);
        if (!FILETIME_IsZero(time))
            ConvertUtcFileTimeToString(time, s, kTimestampPrintLevel_MIN);
        item->setText(1, QString::fromWCharArray(s));
        s[0] = 0;
        if (!fi.IsDir())
            Browse_ConvertSizeToString(fi.Size, s);
        item->setText(2, QString::fromWCharArray(s));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    }
    _list->sortItems(_sortIndex, Qt::AscendingOrder);
    _list->header()->setSortIndicator(_sortIndex,
                                      _ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    if (cursor) {
        _list->setCurrentItem(cursor);
        cursor->setSelected(true);
    } else if (_list->topLevelItemCount() > 0)
        _list->setCurrentItem(_list->topLevelItem(0), 0, QItemSelectionModel::NoUpdate);
    _list->scrollToItem(_list->currentItem());
    _list->setUpdatesEnabled(true);
}

CBrowseDialog::Controls CBrowseDialog::CreateControls() {
    QtUseContextDialogModality(*this);
    setObjectName("browseDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowTitle(Title.IsEmpty() ? QStringLiteral("7-Zip: Browse") : ToQString(Title));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("browseButtons");
    buttons->SetAcceptText("DialogRedesign",
                           FolderMode ? QT_TRANSLATE_NOOP("DialogRedesign", "Choose folder")
                           : SaveMode ? QT_TRANSLATE_NOOP("DialogRedesign", "Save")
                                      : QT_TRANSLATE_NOOP("DialogRedesign", "Open"));
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    auto *parent = new QPushButton(this);
    parent->setIcon(QtCommandSymbol(QtCommandIcon::Parent));
    parent->setObjectName("browseParent");
    parent->setProperty("languageToolTip", true);
    QtBindLangText(parent, IDM_OPEN_PARENT_FOLDER, {}, true);
    parent->setAutoDefault(true);
    auto *create = new QPushButton(this);
    create->setIcon(QtCommandSymbol(QtCommandIcon::NewFolder));
    create->setObjectName("browseCreateFolder");
    create->setProperty("languageToolTip", true);
    QtBindLangText(create, IDM_CREATE_FOLDER, {}, true);
    create->setAutoDefault(true);
    _folder = new QLabel(this);
    _folder->setObjectName("browseFolder");
    _folder->setTextFormat(Qt::PlainText);
    _folder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _folder->installEventFilter(this);
    top->addWidget(parent);
    top->addWidget(create);
    top->addWidget(_folder, 1);
    layout->addLayout(top);
    _folderDraft = new QtInlineFolderName(this);
    _folderDraft->OnCancel([this, create] {
        _folderDraft->hide();
        create->setFocus();
    });
    connect(_folderDraft->NameField(), &QLineEdit::returnPressed, this,
            &CBrowseDialog::CreateDirFromDraft);
    layout->addWidget(_folderDraft);
    _folderDraft->hide();
    _list = new QTreeWidget(this);
    _list->setObjectName("browseList");
    _list->setRootIsDecorated(false);
    _list->setSelectionMode(QAbstractItemView::SingleSelection);
    _list->setHeaderLabels(
        {QtLangText(IDS_PROP_NAME), QtLangText(IDS_PROP_MTIME), QtLangText(IDS_PROP_SIZE)});
    _list->header()->setStretchLastSection(false);
    auto *sample =
        new QTreeWidgetItem(_list, {"123456789012345678901234567", "2009-09-09 09:09", "9999 MB"});
    sample->setIcon(0, QFileIconProvider().icon(QFileIconProvider::File));
    for (int column = 0; column != 3; ++column)
        _list->resizeColumnToContents(column);
    delete sample;
    _list->header()->setSortIndicatorShown(true);
    _list->header()->setSortIndicator(0, Qt::AscendingOrder);
    _list->installEventFilter(this);
    layout->addWidget(_list, 1);
    _pathEdit = new QLineEdit(this);
    _pathEdit->setObjectName("browsePath");
    auto *pathLabel = new QLabel(this);
    pathLabel->setObjectName("browsePathLabel");
    QtBindLangText(pathLabel, IDS_PROP_PATH, "Path");
    QtBindLangText(_pathEdit, IDS_PROP_PATH, "Path", true);
    pathLabel->setBuddy(_pathEdit);
    layout->addWidget(pathLabel);
    layout->addWidget(_pathEdit);
    _filterCombo = new QComboBox(this);
    _filterCombo->setObjectName("browseFilter");
    _filterCombo->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    auto *filterLabel = new QLabel(this);
    filterLabel->setObjectName("browseFilterLabel");
    QtBindLangText(filterLabel, IDS_PROP_FILE_TYPE, "File type");
    QtBindLangText(_filterCombo, IDS_PROP_FILE_TYPE, "File type", true);
    filterLabel->setBuddy(_filterCombo);
    FOR_VECTOR(i, Filters)
    _filterCombo->addItem(ToQString(Filters[i].Description));
    if (Filters.Size() <= 1) {
        if (FolderMode) {
            _filterCombo->hide();
            filterLabel->hide();
        } else
            _filterCombo->setEnabled(false);
    }
    _filterCombo->setCurrentIndex((unsigned)FilterIndex < Filters.Size() ? FilterIndex : -1);
    FilterIndex = _filterCombo->currentIndex();
    layout->addWidget(filterLabel);
    layout->addWidget(_filterCombo);
    for (auto *button : {parent, create})
        QtSizeIconButton(*button);
    return {buttons, parent, create};
}

void CBrowseDialog::ConnectControls(const Controls &controls) {
    const auto [buttons, parent, create] = controls;
    connect(buttons, &QDialogButtonBox::accepted, this, &CBrowseDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(parent, &QPushButton::clicked, this, [this] { OnButtonClicked(IDB_BROWSE_PARENT); });
    connect(create, &QPushButton::clicked, this,
            [this] { OnButtonClicked(IDB_BROWSE_CREATE_DIR); });
    connect(_filterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const HRESULT result = Reload();
        if (result == S_OK) {
            FilterIndex = index;
            return;
        }
        {
            const QSignalBlocker blocked(_filterCombo);
            _filterCombo->setCurrentIndex(FilterIndex);
        }
        ShowReloadError(result);
    });
    connect(_list, &QTreeWidget::itemDoubleClicked, this, [this] { OnItemEnter(); });
    connect(_list, &QTreeWidget::itemSelectionChanged, this, &CBrowseDialog::SetPathEditText);
    connect(_list->header(), &QHeaderView::sectionClicked, this, [this](int index) {
        const int previousSort = _sortIndex;
        const bool previousAscending = _ascending;
        if (index == _sortIndex)
            _ascending = !_ascending;
        else {
            _ascending = index == 0;
            _sortIndex = index;
        }
        const HRESULT result = Reload();
        if (result != S_OK) {
            _sortIndex = previousSort;
            _ascending = previousAscending;
            _list->header()->setSortIndicator(_sortIndex, _ascending ? Qt::AscendingOrder
                                                                     : Qt::DescendingOrder);
            ShowReloadError(result);
        }
    });
}

void CBrowseDialog::CompletePresentation(const Controls &controls) {
    const auto [buttons, parent, create] = controls;
    setTabOrder(_pathEdit, _filterCombo);
    setTabOrder(_filterCombo, buttons->button(QDialogButtonBox::Ok));
    setTabOrder(buttons->button(QDialogButtonBox::Cancel), parent);
    setTabOrder(parent, create);
    setTabOrder(create, _list);
    _pathEdit->setFocus();
    resize(QSize(fontMetrics().averageCharWidth() * 80, fontMetrics().lineSpacing() * 28)
               .expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
}
} // namespace NSourceBrowse
