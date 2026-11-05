#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QVector>
#include <optional>

class FileTable;

// Qt presentation of CPanel's loaded provider. No provider ownership: CPanel
// must release archive streams before removing nested temporary files.
class FilePanelModel final : public QAbstractTableModel {
  public:
    struct Row {
        struct Text {
            QString value;
            quint64 languageGeneration = 0;
        };
        quint32 sourceIndex = quint32(-1);
        QString name;
        QString prefix;
        QString path;
        QString operationPath;
        QString toolTip;
        QIcon icon;
        bool directory = false;
        bool filesystem = false;
        bool marked = false;
        bool dropHighlighted = false;
        std::optional<QString> editName;
        mutable QHash<int, Text> text;
    };
    explicit FilePanelModel(FileTable &view);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value,
                       int role = Qt::EditRole) override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;
    void append(Row row);
    void setColumns(const QStringList &labels);
    void invalidateText(int column = -1);
    void invalidateLanguageText();
    void setCutPaths(const QSet<QString> &paths);

  private:
    FileTable &view_;
    QVector<Row> rows_;
    QStringList labels_;
    QSet<QString> cutPaths_;
    quint64 languageGeneration_ = 0;
};
