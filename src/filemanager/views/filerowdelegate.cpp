#include "filemanager/views/filetable.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

namespace {
class FileRowDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
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
        if (list && list->viewMode() == QListView::IconMode)
            option->displayAlignment = Qt::AlignHCenter | Qt::AlignTop;
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
        if (list && list->viewMode() == QListView::IconMode) {
            // Native styles can extend the label's focus/text area beyond
            // the content rect. Reserve that margin inside the icon cell.
            const int margin =
                list->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, list);
            option.rect.adjust(margin, 0, -margin, 0);
        }
        if (index.data(Qt::UserRole + 19).toBool())
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
        if (!table || table->fullRowHighlight() || index.column() == (primary < 0 ? 0 : primary)) {
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

QAbstractItemDelegate *CreateFileRowDelegate(QWidget *parent) {
    return new FileRowDelegate(parent);
}
