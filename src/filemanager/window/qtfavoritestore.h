#pragma once
#include "filemanager/window/qtsavedlocation.h"
#include <QAbstractListModel>
struct QtFavorite {
    QString id;
    QString label;
    QString path;
    QString defaultLabel;
    QString nativeIdentity;
    QtArchiveLocation archive;
    bool diskFolder = false;
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
    void Add(const QString &path, const QString &label = {}, const QString &nativeIdentity = {},
             const QtArchiveLocation &archive = {}, bool diskFolder = false);
    QtFavorite Favorite(int row) const;
    QModelIndex IndexForId(const QString &id) const;
    void RefreshLabels();

  private:
    void Save();
    QString settingsPath_;
    QList<QtFavorite> favorites_;
};
