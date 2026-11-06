#include "ui/controls/qtrecentpathbutton.h"
#include <QAction>
#include <QMenu>

QtRecentPathButton::QtRecentPathButton(const QStringList &paths, QWidget *parent)
    : QPushButton(parent) {
    setFlat(true);
    setAutoDefault(false);
    setFocusPolicy(Qt::StrongFocus);
    auto *history = new QMenu(this);
    for (const QString &path : paths) {
        // Ampersands are filename characters, never menu mnemonics.
        auto *entry = history->addAction(QString(path).replace('&', "&&"));
        connect(entry, &QAction::triggered, this, [this, path] { emit pathSelected(path); });
    }
    setMenu(history);
    setEnabled(!paths.isEmpty());
}
