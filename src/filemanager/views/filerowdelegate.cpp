#include "filemanager/panels/qtpanellist.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

namespace {
QRect LabelBounds(const QStyleOptionViewItem &option, const QWidget &view) {
    const QRect text = view.style()->subElementRect(QStyle::SE_ItemViewItemText, &option, &view);
    const bool wrapped = option.features.testFlag(QStyleOptionViewItem::WrapText);
    const int flags = option.displayAlignment | (wrapped ? Qt::TextWordWrap : Qt::TextSingleLine);
    const QString label =
        wrapped ? option.text
                : option.fontMetrics.elidedText(option.text, option.textElideMode, text.width());
    return option.fontMetrics.boundingRect(text, flags, label).intersected(text);
}

void DrawItemContent(QPainter &painter, const QStyleOptionViewItem &option, bool cut) {
    painter.save();
    if (cut)
        painter.setOpacity(painter.opacity() * .5);
    const auto *widget = option.widget;
    (widget ? widget->style() : QApplication::style())
        ->drawControl(QStyle::CE_ItemViewItem, &option, &painter, widget);
    painter.restore();
}

void UseSelectedText(QStyleOptionViewItem &option) {
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        option.palette.setColor(group, QPalette::Text,
                                option.palette.color(group, QPalette::HighlightedText));
}

#ifdef Q_OS_MACOS
QRectF PaintMacIconSelection(QPainter &painter, QStyleOptionViewItem &option, const QListView &view,
                             const QModelIndex &index, bool selected, bool hovered) {
    const auto colors = QtWorkspaceTheme::colors();
    const auto *style = view.style();
    const QRect art = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, &view);
    const QRectF frame = QRectF(art.adjusted(-8, -4, 8, 4)).intersected(option.rect);
    if (selected || hovered) {
        painter.setBrush(colors.hover);
        painter.drawRoundedRect(frame, 6, 6);
    }
    if (selected) {
        const QRect label = LabelBounds(option, view);
        const bool colored = index.data(Qt::ForegroundRole).isValid();
        // Keep source emphasis (for example deleted entries) chromatic and
        // readable; an opaque accent can wash a red label into near-white.
        const QColor caption = colored ? option.palette.color(QPalette::Highlight) : colors.accent;
        painter.setBrush(caption);
        painter.drawRoundedRect(QRectF(label.adjusted(-4, -1, 4, 1)).intersected(option.rect), 4,
                                4);
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            option.palette.setColor(group, QPalette::Text,
                                    colored
                                        ? QtSemanticColors::readable(
                                              option.palette.color(group, QPalette::Text), caption)
                                        : QColor(Qt::white));
    }
    return frame;
}
#endif

void PaintWorkspaceIcon(QPainter &painter, QStyleOptionViewItem option, const QModelIndex &index,
                        const QListView &view, bool cut) {
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool current = view.hasFocus() && view.currentIndex().model() == index.model() &&
                         view.currentIndex().row() == index.row();
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const QBrush marked = option.backgroundBrush;
    const QRectF tile = QRectF(option.rect).adjusted(1, 1, -1, -1);
    QRectF focus = tile;
    painter.save();
    painter.setPen(Qt::NoPen);
    if (marked.style() != Qt::NoBrush) {
        painter.setBrush(marked);
        painter.drawRoundedRect(tile, 4, 4);
    }
#ifdef Q_OS_MACOS
    if (view.viewMode() == QListView::IconMode)
        focus = PaintMacIconSelection(painter, option, view, index, selected, hovered);
    else
#endif
    {
        if (selected || hovered) {
            painter.setBrush(selected ? option.palette.highlight()
                                      : QBrush(QtWorkspaceTheme::colors().hover));
            painter.drawRoundedRect(tile, 4, 4);
        }
        if (selected)
            UseSelectedText(option);
    }
    if (selected && marked.style() != Qt::NoBrush)
        painter.fillRect(
            QRect(option.rect.left() + 2, option.rect.top() + 4, 3,
                  qMax(0, option.rect.height() - 8)),
            QtSemanticColors::readable(marked.color(), option.palette.highlight().color()));
    option.backgroundBrush = QBrush();
    option.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
    DrawItemContent(painter, option, cut);
    if (current) {
        painter.setPen(QtWorkspaceTheme::colors().accent);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(focus.adjusted(.5, .5, -.5, -.5), 4, 4);
    }
    painter.restore();
}

class FileRowDelegate final : public QStyledItemDelegate {
    bool workspace_;

    void PrepareContent(QStyleOptionViewItem &option, const QModelIndex &index) const {
        const auto *list = qobject_cast<QListView *>(parent());
        if (list && list->viewMode() == QListView::IconMode) {
            const int margin =
                list->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, list);
            option.rect.adjust(margin, 0, -margin, 0);
        }
        initStyleOption(&option, index);
    }

  public:
    FileRowDelegate(QWidget *parent, bool workspace)
        : QStyledItemDelegate(parent), workspace_(workspace) {}
    bool ContainsPoint(QStyleOptionViewItem option, const QModelIndex &index, QPoint point) const {
        PrepareContent(option, index);
        const auto *view = qobject_cast<QWidget *>(parent());
        if (!view)
            return option.rect.contains(point);
        if (workspace_)
            if (const auto *table = dynamic_cast<const FileTable *>(view))
                option.rect.adjust(
                    index.column() == qMax(0, table->columnForVisibleProperty(0)) ? 12 : 8, 0, -8,
                    0);
        const QRect art =
            view->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, view);
        return (!option.icon.isNull() && art.contains(point)) ||
               LabelBounds(option, *view).contains(point);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (const auto *line = qobject_cast<QLineEdit *>(editor); line && line->isModified())
            return;
        QStyledItemDelegate::setEditorData(editor, index);
    }
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto *list = qobject_cast<QListView *>(parent());
        // The shared table model aligns names to the left. LVS_ICON labels
        // belong below the icon, centered within their own cell.
        if (list && list->viewMode() == QListView::IconMode) {
            option->displayAlignment = Qt::AlignHCenter | Qt::AlignTop;
            if (workspace_ && option->rect.isValid()) {
                const QRect text =
                    list->style()->subElementRect(QStyle::SE_ItemViewItemText, option, list);
                const int height = 2 * option->fontMetrics.lineSpacing();
                if (text.height() > height)
                    option->rect.setBottom(option->rect.bottom() - text.height() + height);
                option->textElideMode = Qt::ElideRight;
            }
        }
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QSize size = QStyledItemDelegate::sizeHint(option, index);
        const auto *list = qobject_cast<QListView *>(parent());
        // ListMode otherwise paints the full size hint across adjacent grid cells.
        if (list && list->viewMode() == QListView::ListMode && list->gridSize().isValid())
            return size.boundedTo(list->gridSize());
        return size;
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override {
        QStyledItemDelegate::updateEditorGeometry(editor, option, index);
#ifdef Q_OS_MACOS
        // Leave room for Cocoa's external focus ring inside an icon/list cell.
        if (qobject_cast<QListView *>(parent()) && editor->testAttribute(Qt::WA_MacShowFocusRect)) {
            const int margin =
                editor->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, editor);
            editor->setGeometry(editor->geometry().adjusted(margin, 0, -margin, 0));
        }
        if (qobject_cast<QTableView *>(parent()) &&
            editor->testAttribute(Qt::WA_MacShowFocusRect)) {
            // Cocoa paints QFocusFrame outside the editor. Keep its vertical
            // margin inside the row, including the first visible row.
            const int margin =
                editor->style()->pixelMetric(QStyle::PM_FocusFrameVMargin, nullptr, editor);
            editor->setGeometry(editor->geometry().adjusted(0, margin, 0, -margin));
        }
#endif
    }
    bool eventFilter(QObject *editor, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                emit commitData(static_cast<QWidget *>(editor));
                emit closeEditor(static_cast<QWidget *>(editor));
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(editor, event);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &original,
               const QModelIndex &index) const override {
        QStyleOptionViewItem option(original);
        const auto *list = qobject_cast<QListView *>(parent());
        if (index.data(QtPanelList::DropHighlightedRole).toBool())
            option.state |= QStyle::State_Selected;
        const QVariant foreground = index.data(Qt::ForegroundRole);
        if (foreground.isValid())
            for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
                option.palette.setColor(
                    group, QPalette::HighlightedText,
                    QtSemanticColors::readable(qvariant_cast<QBrush>(foreground).color(),
                                               option.palette.color(group, QPalette::Highlight)));
        auto *table = dynamic_cast<const FileTable *>(parent());
        const int primary = table ? table->columnForVisibleProperty(0) : -1;
        const bool cut = index.data(QtPanelList::CutRole).toBool();
        if (workspace_ && list) {
            PrepareContent(option, index);
            PaintWorkspaceIcon(*painter, option, index, *list, cut);
            return;
        }
        if (workspace_ && table) {
            initStyleOption(&option, index);
            const bool selected = option.state.testFlag(QStyle::State_Selected) &&
                                  (table->fullRowHighlight() || index.column() == qMax(0, primary));
            const QBrush marked = option.backgroundBrush;
            if (!selected && option.state.testFlag(QStyle::State_Selected))
                painter->fillRect(option.rect, option.palette.base());
            if (selected || marked.style() != Qt::NoBrush) {
                const QBrush fill = selected ? option.palette.highlight() : marked;
                painter->fillRect(option.rect, fill);
                option.backgroundBrush = QBrush();
                if (selected && marked.style() != Qt::NoBrush && index.column() == qMax(0, primary))
                    painter->fillRect(
                        QRect(option.rect.left(), option.rect.top(), 3, option.rect.height()),
                        QtSemanticColors::readable(marked.color(), fill.color()));
                option.state &= ~QStyle::State_MouseOver;
                if (foreground.isValid())
                    for (const auto group :
                         {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
                        option.palette.setColor(
                            group, QPalette::Text,
                            QtSemanticColors::readable(
                                qvariant_cast<QBrush>(foreground).color(),
                                selected ? option.palette.color(group, QPalette::Highlight)
                                         : marked.color()));
            }
            option.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
            if (selected && !foreground.isValid())
                UseSelectedText(option);
            option.rect.adjust(index.column() == qMax(0, primary) ? 12 : 8, 0, -8, 0);
            DrawItemContent(*painter, option, cut);
            return;
        }
        if (!table || table->fullRowHighlight() || index.column() == (primary < 0 ? 0 : primary)) {
            // Preserve the same native icon margin as the workspace delegate.
            if (list && list->viewMode() == QListView::IconMode) {
                const int margin =
                    list->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, list);
                option.rect.adjust(margin, 0, -margin, 0);
            }
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        if (option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(option.rect, option.palette.base());
        QStyleOptionViewItem narrowed(option);
        narrowed.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, narrowed, index);
    }
};
} // namespace

QAbstractItemDelegate *CreateFileRowDelegate(QWidget *parent, bool workspace) {
    return new FileRowDelegate(parent, workspace);
}

bool FileRowContainsPoint(const QAbstractItemDelegate &delegate, QStyleOptionViewItem option,
                          const QModelIndex &index, QPoint point) {
    if (const auto *row = dynamic_cast<const FileRowDelegate *>(&delegate))
        return row->ContainsPoint(option, index, point);
    return option.rect.contains(point);
}
