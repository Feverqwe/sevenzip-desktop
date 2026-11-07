// Qt presentation for the upstream BrowseDialog2 workflow/resource.
// Source samples and control flow adapted from 7-Zip BrowseDialog2.cpp
// (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/BrowseDialog2Res.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "filemanager/FormatUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "settings/RegistryUtils.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtcommandicons.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString Text(const UString &s) { return QString::fromWCharArray(s.Ptr()); }
class BrowseRow final : public QTreeWidgetItem {
    CBrowseDialog2 &_dialog;
    bool operator<(const QTreeWidgetItem &other) const override {
        return _dialog.CompareItems(data(0, Qt::UserRole).toInt(),
                                    other.data(0, Qt::UserRole).toInt()) < 0;
    }

  public:
    BrowseRow(CBrowseDialog2 &dialog, QTreeWidget *list) : QTreeWidgetItem(list), _dialog(dialog) {}
    QVariant data(int column, int role) const override {
        if (column == 0 && role == Qt::ForegroundRole &&
            !QTreeWidgetItem::data(0, Qt::UserRole + 1).toString().isEmpty() && treeWidget())
            return treeWidget()->palette().brush(QPalette::Disabled, QPalette::Text);
        return QTreeWidgetItem::data(column, role);
    }
};
class BrowseAppearance final : public QStyledItemDelegate {
    bool _fullRow, _grid;

  public:
    BrowseAppearance(bool fullRow, bool grid, QObject *parent)
        : QStyledItemDelegate(parent), _fullRow(fullRow), _grid(grid) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        auto display = option;
        if (!_fullRow && index.column() != 0)
            display.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter, display, index);
        if (_grid) {
            painter->save();
            painter->setPen(option.palette.color(QPalette::Mid));
            painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
            painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
            painter->restore();
        }
    }
};
} // namespace
CBrowseDialog2::CBrowseDialog2(
    QWidget *parent, std::function<bool(const QUrl &)> opener,
    std::function<bool(const QtOwnedTempEntry &, const QString &, bool)> details)
    : QDialog(parent), _opener(std::move(opener)), _details(std::move(details)) {
    BuildControls();
}
CBrowseDialog2::~CBrowseDialog2() {
    // QDialog teardown changes focus before QObject disconnects its context.
    disconnect(qApp, nullptr, this, nullptr);
}
void CBrowseDialog2::BuildControls() {
    setObjectName("tempBrowseDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName("tempFooter");
    buttons->button(QDialogButtonBox::Close)->setObjectName("tempClose");
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    auto *commands = new QHBoxLayout;
    commands->setSpacing(8);
    _delete = new QPushButton(this);
    _delete->setObjectName("tempDelete");
    _delete->setProperty("sourceControlID", IDS_BUTTON_DELETE);
    _delete->setIcon(QtCommandSymbol(QtCommandIcon::Delete));
    QtBindResourceText(_delete, IDS_BUTTON_DELETE, "Delete");
    auto *refresh = new QPushButton(this);
    refresh->setObjectName("tempRefresh");
    refresh->setIcon(QtCommandSymbol(QtCommandIcon::Refresh));
    refresh->setProperty("sourceControlID", IDM_VIEW_REFRESH);
    QtBindResourceText(refresh, IDM_VIEW_REFRESH, "Refresh");
    commands->addWidget(_delete);
    commands->addWidget(refresh);
    if (_details) {
        _sessionDetails = new QPushButton(this);
        _sessionDetails->setObjectName("tempSessionDetails");
        QtBindLocalText(_sessionDetails, "qttempbrowsepresentation",
                        QT_TRANSLATE_NOOP("qttempbrowsepresentation", "Session details"));
        commands->addWidget(_sessionDetails);
        connect(_sessionDetails, &QPushButton::clicked, this, &CBrowseDialog2::OnDetails);
    }
    commands->addStretch();
    layout->addLayout(commands);
    auto *path = new QHBoxLayout;
    path->setSpacing(8);
    _parent = new QPushButton(this);
    _parent->setIcon(QtCommandSymbol(QtCommandIcon::Parent));
    _parent->setObjectName("tempParent");
    _parent->setProperty("sourceControlID", IDB_BROWSE2_PARENT);
    _parent->setProperty("languageToolTip", true);
    QtBindLocalText(_parent, "qttempbrowsepresentation",
                    QT_TRANSLATE_NOOP("qttempbrowsepresentation", "Parent folder"), true);
    path->addWidget(_parent);
    _pathEdit = new QLineEdit(this);
    _pathEdit->setReadOnly(true);
    _pathEdit->setObjectName("tempPath");
    _pathEdit->setProperty("sourceControlID", IDT_BROWSE2_FOLDER);
    QtBindLocalText(_pathEdit, "qttempbrowsepresentation",
                    QT_TRANSLATE_NOOP("qttempbrowsepresentation", "Temporary folder"), true);
    path->addWidget(_pathEdit, 1);
    layout->addLayout(path);
    _list = new QTreeWidget(this);
    _list->setObjectName("tempList");
    _list->setProperty("sourceControlID", IDL_BROWSE2);
    _list->setRootIsDecorated(false);
    _list->setUniformRowHeights(true);
    _list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Source OnInit loads the same FM preferences before constructing the list.
    CFmSettings st;
    st.Load(FmSettingsPath());
    _showDots = st.ShowDots;
    _list->setItemDelegate(new BrowseAppearance(st.FullRow, st.ShowGrid, _list));
    ReloadLanguagePresentation();
    InitializeColumns();
    _list->installEventFilter(this);
    _list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(_list, 1);
    _scopeLabel = new QtWrappingLabel(this);
    _scopeLabel->setObjectName("tempScope");
    _scopeLabel->setTextFormat(Qt::PlainText);
    _scopeLabel->setWordWrap(true);
    _scopeLabel->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(_scopeLabel);
    QtSizeIconButton(*_parent);
    ConnectControls(buttons, refresh);
    resize(QSize(fontMetrics().averageCharWidth() * 100, fontMetrics().lineSpacing() * 28)
               .expandedTo(minimumSizeHint()));
}
void CBrowseDialog2::InitializeColumns() {
    _list->header()->setStretchLastSection(false);
    _list->header()->setSectionsClickable(true);
    auto *sample = new QTreeWidgetItem(_list, {"123456789012345678901234567", "2009-09-09 09:09:09",
                                               "99999 MB+", "123456789+", "123456789+",
                                               "12345678901234567890"});
    sample->setIcon(0, QFileIconProvider().icon(QFileIconProvider::File));
    for (int column = 0; column != 6; ++column)
        _list->resizeColumnToContents(column);
    delete sample;
}
void CBrowseDialog2::PopulateRows(const UStringVector &selected, const UString &focused) {
    const QSignalBlocker selectionChanges(*_list);
    _list->setUpdatesEnabled(false);
    _list->clear();
    _parent->setEnabled(!IsExactTempFolder(_snapshot->Prefix));
    _pathEdit->setText(Text(_snapshot->Prefix));
    QTreeWidgetItem *cursor = nullptr;
    if (_showDots && _topDirPrefix != _snapshot->Prefix) {
        auto *parent = new BrowseRow(*this, _list);
        parent->setData(0, Qt::UserRole, -1);
        parent->setText(0, "..");
        parent->setIcon(0, QFileIconProvider().icon(QFileIconProvider::Folder));
        if (focused == UString(".."))
            cursor = parent;
    }
    FOR_VECTOR(i, _snapshot->Items) {
        const CBrowseItem &bi = _snapshot->Items[i];
        const auto &fi = _snapshot->Files[bi.MainFileIndex];
        const UString name = fs2us(fi.Name);
        auto *row = new BrowseRow(*this, _list);
        row->setData(0, Qt::UserRole, int(i));
        row->setText(0, Text(name));
        row->setIcon(0, QFileIconProvider().icon(QFileInfo(Text(_snapshot->Prefix + name))));
        FILETIME ft;
        FiTime_To_FILETIME(fi.MTime, ft);
        wchar_t time[64] = {};
        if (!FILETIME_IsZero(ft))
            ConvertUtcFileTimeToString(ft, time, kTimestampPrintLevel_MIN);
        row->setText(1, QString::fromWCharArray(time));
        wchar_t size[64];
        wchar_t *tail = Browse_ConvertSizeToString(bi.Size, size);
        if (bi.WasInterrupted) {
            *tail++ = '+';
            *tail = 0;
        }
        row->setText(2, QString::fromWCharArray(size));
        if (!fi.IsOsSymLink() && bi.NumFiles != 0)
            row->setText(3, QString::number(bi.NumFiles) + (bi.WasInterrupted ? "+" : ""));
        if (bi.NumDirs != 0)
            row->setText(4, QString::number(bi.NumDirs) + (bi.WasInterrupted ? "+" : ""));
        if (bi.SubFileIndex >= 0)
            row->setText(5, Text(fs2us(_snapshot->Files[bi.SubFileIndex].Name)));
        for (int column : {2, 3, 4})
            row->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        row->setSelected(selected.FindInSorted(name) != -1);
        if (focused == name && !cursor)
            cursor = row;
    }
    _list->sortItems(0, Qt::AscendingOrder);
    if (!cursor && _list->topLevelItemCount() > 0) {
        cursor = _list->topLevelItem(0);
        if (selected.IsEmpty())
            cursor->setSelected(true);
    }
    if (cursor) {
        _list->setCurrentItem(cursor, 0, QItemSelectionModel::NoUpdate);
        _list->scrollToItem(cursor);
    }
    _list->header()->setSortIndicator(_sortIndex,
                                      _ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    ReloadLanguagePresentation();
    _list->setUpdatesEnabled(true);
}
int CBrowseDialog2::GetRealItemIndex(int row) const {
    auto *item = _list->topLevelItem(row);
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

void CBrowseDialog2::ReloadLanguagePresentation() {
    if (property("sourceTitleLanguageID").isValid()) {
        QString title =
            QtLangText(property("sourceTitleLanguageID").toUInt(), "Delete Temporary Files");
        title.remove("...");
        setWindowTitle(title);
    }
    if (!_list)
        return;
    _list->setHeaderLabels({QtLangText(IDS_PROP_NAME), QtLangText(IDS_PROP_MTIME),
                            QtLangText(IDS_PROP_SIZE), QtLangText(IDS_PROP_FILES),
                            QtLangText(IDS_PROP_FOLDERS), QtLangText(IDS_PROP_NAME) + "-2"});
    for (int row = 0; row < _list->topLevelItemCount(); ++row) {
        auto *item = _list->topLevelItem(row);
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index < 0 || unsigned(index) >= _snapshot->Items.Size())
            continue;
        if (_snapshot->Files[_snapshot->Items[index].MainFileIndex].IsOsSymLink())
            item->setText(3, QtLangText(IDM_LINK));
        const auto &owner = _snapshot->Items[index].Owner;
        QString reason;
        if (owner.active)
            reason = QCoreApplication::translate("qttempbrowsepresentation",
                                                 "In use by an active operation or session");
        else if (owner.externallyProtected)
            reason = QCoreApplication::translate(
                "qttempbrowsepresentation",
                "Protected: external application completion is unconfirmed");
        item->setToolTip(0, reason);
        item->setData(0, Qt::UserRole + 1, reason);
    }
    UpdateDeleteAvailability();
}

void CBrowseDialog2::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        ReloadLanguagePresentation();
}
void CBrowseDialog2::GetSelected_RealIndexes(CUIntVector &vector) {
    vector.Clear();
    for (int row = 0; row < _list->topLevelItemCount(); ++row)
        if (_list->topLevelItem(row)->isSelected()) {
            const int realIndex = GetRealItemIndex(row);
            if (realIndex >= 0)
                vector.Add(unsigned(realIndex));
        }
}
void CBrowseDialog2::UpdateDeleteAvailability() {
    CUIntVector selected;
    GetSelected_RealIndexes(selected);
    bool enabled = selected.Size() != 0;
    FOR_VECTOR(i, selected) {
        const auto &owner = _snapshot->Items[selected[i]].Owner;
        if (owner.active || owner.externallyProtected)
            enabled = false;
    }
    _delete->setEnabled(enabled);
    if (_sessionDetails) {
        bool available = false;
        if (selected.Size() == 1) {
            const auto &item = _snapshot->Items[selected[0]];
            const auto &file = _snapshot->Files[item.MainFileIndex];
            const QString path = Text(_snapshot->Prefix + fs2us(file.Name));
            available = IsOwnedPath(item.Owner, path) && _details(item.Owner, path, false);
        }
        _sessionDetails->setEnabled(available);
    }
    if (_scopeLabel) {
        QString status = QCoreApplication::translate("qttempbrowsepresentation",
                                                     "Sevenzip owned temporary files");
        status += '\n';
        status += _snapshot->Items.IsEmpty()
                      ? QCoreApplication::translate("qttempbrowsepresentation",
                                                    "No Sevenzip temporary files")
                      : QtSelectionStatusText(int(selected.Size()), int(_snapshot->Items.Size()));
        if (!enabled)
            for (auto *item : _list->selectedItems())
                if (!item->toolTip(0).isEmpty()) {
                    status += '\n' + item->toolTip(0);
                    break;
                }
        _scopeLabel->setText(status);
    }
}
