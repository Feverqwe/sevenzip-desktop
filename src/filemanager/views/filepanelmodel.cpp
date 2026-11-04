#include "filemanager/views/filepanelmodel.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/sorting/filenamesort.h"
#include "filemanager/views/filetable.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QBrush>
#include <QMimeData>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <numeric>

FilePanelModel::FilePanelModel(FileTable &view) : QAbstractTableModel(&view), view_(view) {}
int FilePanelModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}
int FilePanelModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : labels_.size();
}

QVariant FilePanelModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() >= rows_.size() ||
        index.column() >= labels_.size())
        return {};
    const Row &row = rows_[index.row()];
    const int column = index.column();
    const auto *panel = view_.sourcePanel;
    const bool bound = panel && view_.textFolder && panel->GetFolder() == view_.textFolder;
    const bool readable = bound && panel->ShouldProcessNotifications();
    const bool parent = row.sourceIndex == quint32(-1);
    switch (role) {
    case QtPanelList::OperationPathRole:
        return row.operationPath;
    case QtPanelList::DirectoryRole:
        return row.directory;
    case QtPanelList::NameRole:
        return row.name;
    case QtPanelList::PrefixRole:
        return row.prefix;
    case QtPanelList::SourceIndexRole:
        return row.sourceIndex;
    case QtPanelList::DropHighlightedRole:
        return row.dropHighlighted;
    case QtPanelList::MarkedRole:
        return row.marked;
    case Qt::ToolTipRole:
        return row.toolTip;
    case Qt::DecorationRole:
        return column == 0 || column == view_.columnForVisibleProperty(0) ? QVariant(row.icon)
                                                                          : QVariant();
    case Qt::TextAlignmentRole:
        if (const auto *prop = view_.sourceColumn(column))
            return int(GetColumnAlign(prop->ID, prop->Type));
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::BackgroundRole:
        return row.marked ? QVariant(QBrush(QtSemanticColors::marked(view_.palette())))
                          : QVariant();
    case Qt::ForegroundRole:
        if (readable && !parent) {
            try {
                const QColor color = panel->OnCustomDraw(row.sourceIndex);
                if (color.isValid())
                    return QBrush(QtSemanticColors::readable(
                        color, row.marked ? QtSemanticColors::marked(view_.palette())
                                          : view_.palette().color(QPalette::Base)));
            } catch (...) {
            }
        }
        return {};
    case QtPanelList::SortValueRole:
        if (parent)
            return {};
        if (column == 6)
            return row.sourceIndex;
        if (readable &&
            (view_.propertyId(column) == kpidSize || view_.propertyId(column) == kpidPackSize)) {
            NWindows::NCOM::CPropVariant value;
            try {
                if (panel->GetFolder()->GetProperty(row.sourceIndex, view_.propertyId(column),
                                                    &value) == S_OK &&
                    value.vt == VT_UI8)
                    return QVariant::fromValue(quint64(value.uhVal.QuadPart));
            } catch (...) {
            }
        }
        return {};
    case Qt::EditRole:
        if (column == 0)
            return row.editName ? *row.editName : row.name.normalized(QString::NormalizationForm_C);
        [[fallthrough]];
    case Qt::DisplayRole:
        if (panel && !panel->ShouldDisplayItemText())
            return QString();
        if (column == 1)
            return row.path;
        if (column == 5)
            return parent ? QString() : GetExtensionPtr(QStringView(row.name)).toString();
        if (column == 6)
            return parent ? QString() : QString::number(row.sourceIndex);
        if (const auto found = row.text.constFind(column);
            found != row.text.cend() &&
            (!readable || found->languageGeneration == languageGeneration_))
            return found->value;
        if (panel && !panel->ShouldProcessNotifications())
            return QString();
        if (readable) {
            if (const auto *prop = view_.sourceColumn(column)) {
                try {
                    QString text = panel->GetItemText(row.sourceIndex, *prop);
                    if (column == 0)
                        text = (row.prefix + text).normalized(QString::NormalizationForm_C);
                    row.text.insert(column, {text, languageGeneration_});
                    return text;
                } catch (...) {
                    return QString();
                }
            }
        }
        return column == 0 ? (row.prefix + row.name).normalized(QString::NormalizationForm_C)
                           : QString();
    default:
        return {};
    }
}

bool FilePanelModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.model() != this || index.row() >= rows_.size() ||
        index.column() >= labels_.size())
        return false;
    Row &row = rows_[index.row()];
    int firstColumn = index.column(), lastColumn = firstColumn;
    switch (role) {
    case Qt::EditRole:
        if (index.column() != 0)
            return false;
        row.editName = value.toString();
        break;
    case Qt::DisplayRole:
        row.text.remove(index.column());
        break;
    case Qt::DecorationRole:
        row.icon = qvariant_cast<QIcon>(value);
        break;
    case Qt::ToolTipRole:
        row.toolTip = value.toString();
        break;
    case QtPanelList::NameRole:
        row.name = value.toString();
        row.text.clear();
        break;
    case QtPanelList::MarkedRole:
        if (row.marked == value.toBool())
            return true;
        row.marked = value.toBool();
        firstColumn = 0;
        lastColumn = columnCount() - 1;
        break;
    case QtPanelList::DropHighlightedRole:
        if (row.dropHighlighted == value.toBool())
            return true;
        row.dropHighlighted = value.toBool();
        firstColumn = 0;
        lastColumn = columnCount() - 1;
        break;
    default:
        return false;
    }
    emit dataChanged(this->index(index.row(), firstColumn), this->index(index.row(), lastColumn),
                     role == QtPanelList::MarkedRole
                         ? QList<int>{role, Qt::BackgroundRole, Qt::ForegroundRole}
                         : QList<int>{role});
    return true;
}

Qt::ItemFlags FilePanelModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    if (index.model() != this || index.row() >= rowCount() || index.column() >= columnCount())
        return Qt::NoItemFlags;
    auto result =
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    if (index.column() == 0 && rows_[index.row()].sourceIndex != quint32(-1))
        result |= Qt::ItemIsEditable;
    return result;
}

QVariant FilePanelModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || section < 0 || section >= labels_.size())
        return {};
    if (role == Qt::DisplayRole)
        return labels_[section];
    if (role == Qt::InitialSortOrderRole)
        return int(CPanel::GetDefaultSortAscending(view_.propertyId(section))
                       ? Qt::AscendingOrder
                       : Qt::DescendingOrder);
    return {};
}
bool FilePanelModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value,
                                   int role) {
    if (orientation != Qt::Horizontal || section < 0 || section >= labels_.size() ||
        (role != Qt::DisplayRole && role != Qt::EditRole))
        return false;
    labels_[section] = value.toString();
    emit headerDataChanged(orientation, section, section);
    return true;
}

void FilePanelModel::setColumns(const QStringList &labels) {
    if (labels.size() == labels_.size()) {
        labels_ = labels;
        if (!labels.isEmpty())
            emit headerDataChanged(Qt::Horizontal, 0, labels.size() - 1);
        return;
    }
    const int previous = labels_.size();
    if (labels.size() > previous) {
        beginInsertColumns({}, previous, labels.size() - 1);
        labels_ = labels;
        endInsertColumns();
    } else {
        beginRemoveColumns({}, labels.size(), previous - 1);
        labels_ = labels;
        for (auto &row : rows_)
            row.text.clear();
        endRemoveColumns();
    }
    if (!labels.isEmpty())
        emit headerDataChanged(Qt::Horizontal, 0, labels.size() - 1);
}
void FilePanelModel::append(Row row) {
    const int position = rows_.size();
    beginInsertRows({}, position, position);
    rows_.append(std::move(row));
    endInsertRows();
}
bool FilePanelModel::removeRows(int row, int count, const QModelIndex &parent) {
    if (parent.isValid() || row < 0 || count <= 0 || row > rows_.size() - count)
        return false;
    beginRemoveRows({}, row, row + count - 1);
    rows_.remove(row, count);
    endRemoveRows();
    return true;
}
void FilePanelModel::invalidateText(int column) {
    for (auto &row : rows_)
        if (column < 0)
            row.text.clear();
        else
            row.text.remove(column);
    if (!rows_.isEmpty() && columnCount())
        emit dataChanged(index(0, column < 0 ? 0 : column),
                         index(rowCount() - 1, column < 0 ? columnCount() - 1 : column),
                         {Qt::DisplayRole});
}

void FilePanelModel::invalidateLanguageText() {
    ++languageGeneration_;
    if (rowCount() && columnCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole});
}

void FilePanelModel::sort(int column, Qt::SortOrder order) {
    auto *panel = view_.sourcePanel;
    if (column < 0 || column >= columnCount() || rows_.size() < 2 || !panel || !view_.textFolder ||
        panel->GetFolder() != view_.textFolder)
        return;
    QVector<int> permutation(rows_.size());
    std::iota(permutation.begin(), permutation.end(), 0);
    std::stable_sort(permutation.begin(), permutation.end(), [&](int a, int b) {
        const auto &first = rows_[a];
        const auto &second = rows_[b];
        if (first.sourceIndex == quint32(-1) || second.sourceIndex == quint32(-1))
            return first.sourceIndex != second.sourceIndex && first.sourceIndex == quint32(-1);
        if (first.directory != second.directory)
            return first.directory;
        const int compared = panel->CompareItemIndices(
            first.sourceIndex, second.sourceIndex, view_.propertyId(column),
            view_.usesSourceColumns() ? panel->_isRawSortProp != 0 : view_.isRawProperty(column));
        return order == Qt::AscendingOrder ? compared < 0 : compared > 0;
    });
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const QModelIndexList oldIndices = persistentIndexList();
    QVector<int> destination(rows_.size());
    QVector<Row> sorted;
    sorted.reserve(rows_.size());
    for (int next = 0; next < permutation.size(); ++next) {
        destination[permutation[next]] = next;
        sorted.append(std::move(rows_[permutation[next]]));
    }
    rows_ = std::move(sorted);
    QModelIndexList newIndices;
    for (const auto &old : oldIndices)
        newIndices.append(index(destination[old.row()], old.column()));
    changePersistentIndexList(oldIndices, newIndices);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

QStringList FilePanelModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData *FilePanelModel::mimeData(const QModelIndexList &indexes) const {
    auto *mime = new QMimeData;
    if (!view_.filesystem)
        return mime;
    QList<QUrl> urls;
    QSet<int> included;
    for (const auto &index : indexes) {
        if (!index.isValid() || index.model() != this || included.contains(index.row()))
            continue;
        included.insert(index.row());
        const auto &row = rows_[index.row()];
        if (row.sourceIndex != quint32(-1) && !row.path.isEmpty())
            urls.append(QUrl::fromLocalFile(row.path));
    }
    mime->setUrls(urls);
    return mime;
}

Qt::DropActions FilePanelModel::supportedDragActions() const {
    return Qt::CopyAction | Qt::MoveAction;
}
