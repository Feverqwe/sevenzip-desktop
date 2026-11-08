#pragma once

#include "7zip/PropID.h"
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Common/MyWindows.h"
#include "filemanager/views/filepanelmodel.h"
#include <QListView>
#include <QTableView>
#include <QVector>
#include <functional>
class CPanel;
class CPropColumns;
struct CPropColumn;
class QMenu;
class QStyleOptionViewItem;
QAbstractItemDelegate *CreateFileRowDelegate(QWidget *parent, bool workspace = false);
bool FileRowContainsPoint(const QAbstractItemDelegate &delegate, QStyleOptionViewItem option,
                          const QModelIndex &index, QPoint point);

class FileTable : public QTableView {
    Q_OBJECT
  public:
    explicit FileTable(int columns, QWidget *parent = nullptr);
    void refreshLanguageText();
    void InstallSelectionGesture(std::function<bool()> activate);
    virtual void invalidateLanguageText();
    FilePanelModel *panelModel() const { return model_; }
    int rowCount() const { return model_->rowCount(); }
    int columnCount() const { return model_->columnCount(); }
    void clearRows() { model_->removeRows(0, rowCount()); }
    int currentRow() const { return currentIndex().row(); }
    void setCurrentCell(int row, int column) { setCurrentIndex(model_->index(row, column)); }
    void setCurrentCell(int row, int column, QItemSelectionModel::SelectionFlags flags) {
        selectionModel()->setCurrentIndex(model_->index(row, column), flags);
    }
    void sortItems(int column, Qt::SortOrder order = Qt::AscendingOrder) {
        sortByColumn(column, order);
    }
    bool filesystem = true;
    CPanel *sourcePanel = nullptr;
    // Published callback identity only. CPanel owns the provider and must be
    // able to close its archive streams before deleting nested temporary files.
    IFolderFolder *textFolder = nullptr;
    std::function<void()> dragStarter;
    void setColumnProperties(const QVector<PROPID> &properties, bool raw = false);
    virtual void configureSourceColumns();
    virtual QString typeLabel(const QString &name, bool directory) const;
    virtual void appendPresentationColumnActions(QMenu &) {}
    virtual void sourceColumnVisibilityChanged() {}
    CPropColumns sourceColumnLayout() const;
    bool usesSourceColumns() const { return sourceColumnsBound_; }
    const CPropColumn *sourceColumn(int column) const;
    int columnForCatalogProperty(unsigned index) const;
    int columnForVisibleProperty(unsigned index) const;
    void removeSourceColumn(unsigned index);
    void appendSourceColumn(unsigned catalogIndex);
    bool deferSourceColumnClick(std::function<void()> action);
    PROPID propertyId(int column) const;
    bool isRawProperty(int column) const;
    int columnForProperty(PROPID id) const;
    void keyboardSearch(const QString &search) override;

  protected:
    void startDrag(Qt::DropActions actions) override;
    void changeEvent(QEvent *event) override;

  private:
    struct ColumnBinding {
        PROPID property;
        bool raw = false;
        int catalogIndex = -1;
        int visibleIndex = -1;
    };
    QVector<ColumnBinding> columns_;
    bool sourceColumnsBound_ = false;
    int findVisibleColumn(unsigned index) const;
    FilePanelModel *model_;
};

class FileIconList final : public QListView {
  public:
    using QListView::QListView;
    void ConfigureLargeIcons();
    void InstallSelectionGesture(std::function<bool()> activate);
    std::function<void()> dragStarter;

  protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void startDrag(Qt::DropActions actions) override;
    void changeEvent(QEvent *event) override;

  private:
    bool containsItemAt(QPoint point) const;
    bool largeIconsConfigured_ = false;
};
