#pragma once
#include <QAbstractListModel>
#include <QListView>

class QtFavoriteList final : public QListView {
  public:
    using QListView::QListView;

  protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

struct QtFavorite {
    QString id;
    QString label;
    QString path;
    QString defaultLabel;
};

class QtFavoriteStore final : public QAbstractListModel {
  public:
    explicit QtFavoriteStore(const QString &settingsPath, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    bool moveRows(const QModelIndex &source, int row, int count, const QModelIndex &destination,
                  int before) override;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indices) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override;
    void Add(const QString &path, const QString &label = {});
    QtFavorite Favorite(int row) const;
    void RefreshLabels();

  private:
    void Save();
    QString settingsPath_;
    QList<QtFavorite> favorites_;
};
