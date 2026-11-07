#pragma once
#include "filemanager/AppState.h"
#include "filemanager/window/qtsavedlocation.h"
#include <QHash>
#include <QObject>

struct QtRecentPlace {
    QString path;
    QString nativeIdentity;
    QtArchiveLocation archive;
};

// CFolderHistory owns visit ordering and comparison. Windows share this store.
class QtRecentPlacesStore final : public QObject {
    Q_OBJECT
  public:
    static QtRecentPlacesStore *ForSettings(const QString &settings);
    explicit QtRecentPlacesStore(const QString &settings, QObject *parent = nullptr);
    int Count() const;
    QtRecentPlace Place(int row) const;
    int IndexOf(const QString &path) const;
    void Reload();
    void Visit(const QtRecentPlace &place);

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
