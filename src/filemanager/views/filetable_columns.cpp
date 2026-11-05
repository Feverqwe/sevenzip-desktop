#include "filemanager/PropertyName.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/views/filetable.h"
#include <QCoreApplication>
#include <QHeaderView>
#include <QSignalBlocker>

namespace {
bool sameColumnIdentity(const CPropColumn &left, const CPropColumn &right) {
    return left.ID == right.ID && left.Type == right.Type && left.IsRawProp == right.IsRawProp &&
           left.Name == right.Name;
}
} // namespace

void FileTable::refreshLanguageText() {
    for (int column = 0; column < columnCount(); ++column) {
        if (isRawProperty(column))
            continue;
        const CPropColumn *prop = sourceColumn(column);
        const QString label = propertyId(column) == kpidNoProperty
                                  ? QCoreApplication::translate("mainwindow_language", "Order")
                                  : QString::fromWCharArray(GetNameOfProperty(
                                        propertyId(column), prop ? prop->Name.Ptr() : nullptr));
        model_->setHeaderData(column, Qt::Horizontal, label);
    }
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex index = model_->index(row, 0);
        if (index.data(QtPanelList::SourceIndexRole).toUInt() == UInt32(-1))
            model_->setData(index,
                            QCoreApplication::translate("mainwindow_language", "Parent folder"),
                            Qt::ToolTipRole);
    }
    model_->invalidateLanguageText();
}

void FileTable::setColumnProperties(const QVector<PROPID> &properties, bool raw) {
    columns_.clear();
    for (PROPID property : properties)
        columns_.append(ColumnBinding{property, raw});
    sourceColumnsBound_ = false;
}

void FileTable::configureSourceColumns() {
    setColumnProperties(
        {kpidName, kpidPath, kpidSize, kpidPackSize, kpidMTime, kpidExtension, kpidNoProperty});
    sourceColumnsBound_ = true;
    QStringList labels;
    for (const auto &binding : columns_)
        labels << (binding.property == kpidNoProperty
                       ? QCoreApplication::translate("mainwindow_columns", "Order")
                       : QString::fromWCharArray(GetNameOfProperty(binding.property, nullptr)));
    const CPropColumns &catalog = sourcePanel->GetColumns();
    FOR_VECTOR(i, catalog) {
        const CPropColumn &prop = catalog[i];
        int column = -1;
        if (prop.ID == kpidName)
            column = 0;
        else if (prop.ID == kpidSize)
            column = 2;
        else if (prop.ID == kpidPackSize)
            column = 3;
        else if (prop.ID == kpidMTime)
            column = 4;
        if (column < 0 || prop.IsRawProp || columns_[column].catalogIndex >= 0) {
            column = columns_.size();
            columns_.append(ColumnBinding{prop.ID});
            labels << ToQString(prop.Name);
        }
        columns_[column] = {prop.ID, prop.IsRawProp, int(i)};
        labels[column] = ToQString(prop.Name);
    }
    model_->setColumns(labels);
    // Match against the complete catalog before installing visible aliases.
    // Equal identities can occur more than once in a provider's catalog.
    const CPropColumns &visible = sourcePanel->GetVisibleColumns();
    QVector<int> matches;
    FOR_VECTOR(i, visible) { matches.append(findVisibleColumn(i)); }
    FOR_VECTOR(i, visible) {
        if (matches[i] >= 0)
            columns_[matches[i]].visibleIndex = int(i);
    }
    FOR_VECTOR(i, catalog) {
        const CPropColumn &prop = catalog[i];
        const int column = columnForCatalogProperty(i);
        setColumnWidth(column, qMax(32, int(prop.Width)));
        setColumnHidden(column, !prop.IsVisible);
    }
    for (int column : {1, 5, 6})
        setColumnHidden(column, true);
    for (int column : {0, 2, 3, 4}) {
        if (!sourceColumn(column))
            setColumnHidden(column, true);
    }
    CIntArr columnOrder;
    const bool ordered = sourcePanel->GetColumnOrder(columnOrder);
    FOR_VECTOR(i, visible) {
        const int column = columnForVisibleProperty(ordered ? unsigned(columnOrder[i]) : i);
        if (column >= 0)
            horizontalHeader()->moveSection(horizontalHeader()->visualIndex(column), int(i));
    }
}

CPropColumns FileTable::sourceColumnLayout() const {
    const auto *header = horizontalHeader();
    QVector<int> orders(columnCount());
    int order = 0;
    for (int visual = 0; visual < header->count(); ++visual) {
        const int column = header->logicalIndex(visual);
        if (!isColumnHidden(column))
            orders[column] = order++;
    }
    CPropColumns layout;
    const CPropColumns &visible = sourcePanel->GetVisibleColumns();
    // Preserve source logical indices; the header supplies only display metadata.
    FOR_VECTOR(i, visible) {
        const int column = columnForVisibleProperty(i);
        if (column < 0 || isColumnHidden(column))
            continue;
        CPropColumn prop = visible[i];
        prop.IsVisible = true;
        prop.Order = orders[column];
        prop.Width = UInt32(header->sectionSize(column));
        layout.Add(prop);
    }
    return layout;
}

PROPID FileTable::propertyId(int column) const {
    return column >= 0 && column < columns_.size() ? columns_[column].property : kpidNoProperty;
}

bool FileTable::isRawProperty(int column) const {
    return column >= 0 && column < columns_.size() && columns_[column].raw;
}

int FileTable::columnForProperty(PROPID id) const {
    for (int column = 0; column < columns_.size(); ++column) {
        if (column != 1 && column != 5 && column != 6 && columns_[column].property == id)
            return column;
    }
    return id == kpidExtension ? 5 : id == kpidNoProperty ? 6 : -1;
}

const CPropColumn *FileTable::sourceColumn(int column) const {
    if (!sourcePanel || column < 0)
        return nullptr;
    const CPropColumns &catalog = sourcePanel->GetColumns();
    if (sourceColumnsBound_) {
        if (column >= columns_.size())
            return nullptr;
        const auto &binding = columns_[column];
        const CPropColumns &visible = sourcePanel->GetVisibleColumns();
        if (binding.visibleIndex >= 0 && unsigned(binding.visibleIndex) < visible.Size())
            return &visible[unsigned(binding.visibleIndex)];
        if (binding.catalogIndex < 0 || unsigned(binding.catalogIndex) >= catalog.Size())
            return nullptr;
        const CPropColumn &prop = catalog[unsigned(binding.catalogIndex)];
        return prop.ID == binding.property && prop.IsRawProp == binding.raw ? &prop : nullptr;
    }
    // Standalone adapters without a publication map retain their ID binding.
    const int index = catalog.FindItem_for_PropID(propertyId(column));
    return index >= 0 ? &catalog[unsigned(index)] : nullptr;
}

int FileTable::columnForCatalogProperty(unsigned index) const {
    for (int column = 0; column < columns_.size(); ++column)
        if (columns_[column].catalogIndex == int(index))
            return column;
    return -1;
}

int FileTable::columnForVisibleProperty(unsigned index) const {
    if (!sourcePanel || index >= sourcePanel->GetVisibleColumns().Size())
        return -1;
    if (sourceColumnsBound_) {
        for (int column = 0; column < columns_.size(); ++column)
            if (columns_[column].visibleIndex == int(index))
                return column;
        return -1;
    }
    return findVisibleColumn(index);
}

int FileTable::findVisibleColumn(unsigned index) const {
    const CPropColumns &visible = sourcePanel->GetVisibleColumns();
    const CPropColumn &prop = visible[index];
    unsigned occurrence = 0;
    for (unsigned previous = 0; previous < index; ++previous)
        if (sameColumnIdentity(visible[previous], prop))
            ++occurrence;
    for (int column = 0; column < columnCount(); ++column) {
        const CPropColumn *candidate = sourceColumn(column);
        if (candidate && sameColumnIdentity(*candidate, prop)) {
            if (occurrence == 0)
                return column;
            --occurrence;
        }
    }
    return -1;
}

void FileTable::removeSourceColumn(unsigned index) {
    for (int column = 0; column < columns_.size(); ++column)
        if (columns_[column].visibleIndex == int(index)) {
            setColumnHidden(column, true);
            break;
        }
    for (auto &binding : columns_) {
        if (binding.visibleIndex == int(index))
            binding.visibleIndex = -1;
        else if (binding.visibleIndex > int(index))
            --binding.visibleIndex;
    }
}

void FileTable::appendSourceColumn(unsigned catalogIndex) {
    const QSignalBlocker blocked(this);
    const QSignalBlocker selectionBlocked(selectionModel());
    const QModelIndexList selected = selectionModel()->selectedRows();
    int column = -1;
    for (int candidate = 0; candidate < columns_.size(); ++candidate)
        if (columns_[candidate].catalogIndex == int(catalogIndex) &&
            columns_[candidate].visibleIndex < 0) {
            column = candidate;
            break;
        }
    const unsigned index = sourcePanel->GetVisibleColumns().Size() - 1;
    const CPropColumn &prop = sourcePanel->GetVisibleColumns()[index];
    if (column < 0) {
        column = columnCount();
        columns_.append(ColumnBinding{prop.ID, prop.IsRawProp, int(catalogIndex)});
        QStringList labels;
        for (int i = 0; i < column; ++i)
            labels << model_->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        labels << ToQString(prop.Name);
        model_->setColumns(labels);
    } else {
        model_->invalidateText(column);
    }
    columns_[column].visibleIndex = int(index);
    setColumnWidth(column, qMax(32, int(prop.Width)));
    setColumnHidden(column, false);
    horizontalHeader()->moveSection(horizontalHeader()->visualIndex(column), columnCount() - 1);
    for (const QModelIndex &row : selected)
        selectionModel()->select(model()->index(row.row(), column), QItemSelectionModel::Select);
}
