// Qt controls and bindings for the source long-path browser.
// Adapted from 7-Zip UI/FileManager/BrowseDialog.cpp and resources.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/BrowseDialogRes.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/browse/qtsourcebrowsedialog_p.h"
#include "filemanager/views/qtfilelisthover.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/theme/qtcommandicons.h"
#include <QComboBox>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace NSourceBrowse {
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
    connect(_filterCombo, &QComboBox::currentIndexChanged, this, [this] { Reload(); });
    connect(_list, &QTreeWidget::itemSelectionChanged, this, &CBrowseDialog::SetPathEditText);
    connect(_list->header(), &QHeaderView::sectionClicked, this, [this](int index) {
        if (index == _sortIndex)
            _ascending = !_ascending;
        else {
            _ascending = index == 0;
            _sortIndex = index;
        }
        _list->header()->setSortIndicator(index,
                                          _ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        Reload();
    });
}

void CBrowseDialog::ConfigureSingleClick(bool enabled) {
    auto *hover = new QtFileListHover(*_list);
    hover->SetEnabled(enabled);
    if (enabled)
        connect(_list, &QTreeWidget::itemClicked, this, [this] { OnItemEnter(); });
    else
        connect(_list, &QTreeWidget::itemDoubleClicked, this, [this] { OnItemEnter(); });
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
