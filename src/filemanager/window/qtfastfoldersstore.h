#pragma once
#include "filemanager/AppState.h"
#include <QObject>
#include <QVariantMap>

// Shared publication of native numeric bookmarks into window snapshots.
class QtFastFoldersStore final : public QObject {
    Q_OBJECT
  public:
    static QtFastFoldersStore *ForSettings(const QString &settings);
    explicit QtFastFoldersStore(const QString &settings, QObject *parent = nullptr);
    void Set(unsigned index, const UString &path, const QString &nativeIdentity);
    QString NativeIdentity(unsigned index, const UString &path) const;
    void CopyTo(CFastFolders &target) const;

  signals:
    void changed();
    void published();

  private:
    QString settings_;
    CFastFolders folders_;
    QVariantMap nativePlaces_;
};
