#include "filemanager/window/qtworkspacetheme.h"
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLayout>
#include <QPalette>
#include <QStatusBar>

namespace QtWorkspaceTheme {
int metric(const QWidget &widget, int value) {
    return qRound(value * qMax(1.0, QFontMetrics(widget.font()).height() / 16.0));
}

void pane(QWidget &widget) { surface(widget, colors().secondary); }

void fileView(QAbstractItemView &view) {
    view.setPalette(QtUiTheme::palette());
    view.setFrameShape(QFrame::NoFrame);
    view.viewport()->setAutoFillBackground(true);
}

void header(QWidget &header) {
    const auto c = colors();
    header.setStyleSheet(
        QStringLiteral("QHeaderView { background:%1; }"
                       "QHeaderView::section { background:%1; color:%2; border:0;"
                       "border-bottom:1px solid %3; padding:0 12px; font-weight:400; }"
                       "QHeaderView::section:first { padding-left:16px; }")
            .arg(c.secondary.name(), c.subtle.name(), c.stroke.name()));
    if (auto *view = qobject_cast<QHeaderView *>(&header)) {
        view->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        view->setFixedHeight(metric(header, 28));
    }
}
void statusBar(QStatusBar &status) {
    const auto c = colors();
    pane(status);
    auto p = status.palette();
    p.setColor(QPalette::WindowText, c.subtle);
    status.setPalette(p);
    status.setMinimumHeight(QFontMetrics(status.font()).height() + metric(status, 6));
    status.layout()->setContentsMargins(metric(status, 12), metric(status, 2), metric(status, 12),
                                        metric(status, 2));
    status.setStyleSheet(
        QStringLiteral("QStatusBar { border-top:1px solid %1; color:%2; }"
                       "QStatusBar::item { border:0; } QStatusBar QLabel { color:%2; }")
            .arg(c.stroke.name(), c.subtle.name()));
}
} // namespace QtWorkspaceTheme
