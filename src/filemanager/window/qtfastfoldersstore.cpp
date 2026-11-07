#include "filemanager/window/qtfastfoldersstore.h"
#include "filemanager/panels/Panel.h"
#include <QApplication>
#include <QSettings>

namespace {
constexpr auto kNativeBookmarks = "workspace/nativeBookmarks";
}

QtFastFoldersStore *QtFastFoldersStore::ForSettings(const QString &settings) {
    for (auto *object : qApp->children())
        if (auto *store = dynamic_cast<QtFastFoldersStore *>(object);
            store && store->settings_ == settings)
            return store;
    return new QtFastFoldersStore(settings, qApp);
}

QtFastFoldersStore::QtFastFoldersStore(const QString &settings, QObject *parent)
    : QObject(parent), settings_(settings) {
    folders_.Read(settings_);
    nativePlaces_ = QSettings(settings_, QSettings::IniFormat).value(kNativeBookmarks).toMap();
}

void QtFastFoldersStore::Set(unsigned index, const UString &path, const QString &nativeIdentity) {
    // Merge with the existing native persistence before changing this slot.
    folders_.Read(settings_);
    CPanel::SetBookmark(folders_, index, path);
    folders_.Save(settings_);
    QSettings settings(settings_, QSettings::IniFormat);
    nativePlaces_ = settings.value(kNativeBookmarks).toMap();
    const QString slot = QString::number(index);
    if (nativeIdentity.isEmpty())
        nativePlaces_.remove(slot);
    else
        nativePlaces_.insert(slot, QVariantMap{{"path", ToQString(path)}, {"id", nativeIdentity}});
    settings.setValue(kNativeBookmarks, nativePlaces_);
    settings.sync();
    emit changed();
    emit published();
}

QString QtFastFoldersStore::NativeIdentity(unsigned index, const UString &path) const {
    const auto entry = nativePlaces_.value(QString::number(index)).toMap();
    return entry.value("path").toString() == ToQString(path) ? entry.value("id").toString()
                                                             : QString();
}

void QtFastFoldersStore::CopyTo(CFastFolders &target) const { target = folders_; }
