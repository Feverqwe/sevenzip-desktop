#include "filemanager/window/qtfastfoldersstore.h"
#include "filemanager/panels/Panel.h"
#include <QApplication>

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
}

void QtFastFoldersStore::Set(unsigned index, const UString &path) {
    // Merge with the existing native persistence before changing this slot.
    folders_.Read(settings_);
    CPanel::SetBookmark(folders_, index, path);
    folders_.Save(settings_);
    emit changed();
    emit published();
}

void QtFastFoldersStore::CopyTo(CFastFolders &target) const { target = folders_; }
