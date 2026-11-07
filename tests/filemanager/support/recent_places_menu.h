#pragma once

#include <QDir>
#include <QMenu>
#include <QtTest>

namespace RecentPlacesMenu {
inline QMenu *Visible(QWidget &owner) {
    for (auto *popup : owner.findChildren<QMenu *>("recentPlacesMenu"))
        if (popup->isVisible())
            return popup;
    return nullptr;
}

inline QAction *Find(QMenu &popup, const QString &path) {
    const auto normalized = [](const QString &value) {
        return QDir::cleanPath(QDir::fromNativeSeparators(value));
    };
    for (auto *item : popup.actions())
        if (item->isEnabled() && normalized(item->data().toString()) == normalized(path))
            return item;
    return nullptr;
}

inline void Choose(QMenu &popup, QAction &item) {
    popup.setActiveAction(&item);
    QTest::keyClick(&popup, Qt::Key_Return);
    QCoreApplication::processEvents();
}
} // namespace RecentPlacesMenu
