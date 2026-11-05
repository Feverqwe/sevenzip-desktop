#pragma once
#include "filemanager/AppState.h"
#include "filemanager/window/qtsavedlocation.h"
#include <QAbstractListModel>
#include <QHash>

struct QtRecentPlace {
    QString path;
    QString nativeIdentity;
    QtArchiveLocation archive;
};

// CFolderHistory retains ordering, comparison and bounds; this adapter publishes
// immediate changes to every window using the same settings store.
class QtRecentPlacesStore final : public QAbstractListModel {
    Q_OBJECT
  public:
    static QtRecentPlacesStore *ForSettings(const QString &settings);
    explicit QtRecentPlacesStore(const QString &settings, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QtRecentPlace Place(int row) const;
    int IndexOf(const QString &path) const;
    void Reload();
    void Visit(const QtRecentPlace &place);
    void Remove(const QString &path);
    void Clear();

  signals:
    void historyChanged();

  private:
    void Publish();
    QtRecentPlace Target(const UString &path) const;
    QString settings_;
    CFolderHistory history_;
    UStringVector paths_;
    QHash<QString, QtRecentPlace> targets_;
};
