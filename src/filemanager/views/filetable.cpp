#include "filemanager/views/filetable.h"
#include "filemanager/sorting/filenamesort.h"
#include "filemanager/window/qtworkspacetheme.h"

#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QUrl>

namespace {
class FileColumnHeader final : public QHeaderView {
  public:
    using QHeaderView::QHeaderView;
    bool dispatchingClick = false;
    std::function<void()> sortClick;

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        {
            const QScopedValueRollback<bool> click(dispatchingClick,
                                                   event->button() == Qt::LeftButton);
            QHeaderView::mousePressEvent(event);
        }
        finishSortClick();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        {
            const QScopedValueRollback<bool> click(dispatchingClick,
                                                   event->button() == Qt::LeftButton);
            QHeaderView::mouseReleaseEvent(event);
        }
        finishSortClick();
    }

  private:
    void finishSortClick() {
        if (!sortClick)
            return;
        auto action = std::move(sortClick);
        sortClick = {};
        action();
    }
};

} // namespace

FileTable::FileTable(int columns, QWidget *parent)
    : QTableView(parent), model_(new FilePanelModel(*this)) {
    setModel(model_);
    QStringList labels;
    for (int i = 0; i < columns; ++i)
        labels << QString();
    model_->setColumns(labels);
    setItemDelegate(CreateFileRowDelegate(this));
    setHorizontalHeader(new FileColumnHeader(Qt::Horizontal, this));
    horizontalHeader()->setSectionsClickable(true);
}

void FileTable::invalidateLanguageText() { model_->invalidateLanguageText(); }

QString FileTable::typeLabel(const QString &name, bool) const {
    return GetExtensionPtr(QStringView(name)).toString();
}

bool FileTable::deferSourceColumnClick(std::function<void()> action) {
    auto *header = dynamic_cast<FileColumnHeader *>(horizontalHeader());
    if (!header || !header->dispatchingClick)
        return false;
    // Finish Qt's automatic sort before projecting the source click result.
    header->sortClick = std::move(action);
    return true;
}

void FileTable::setFullRowHighlight(bool enabled) {
    if (fullRowHighlight_ == enabled)
        return;
    fullRowHighlight_ = enabled;
    viewport()->update();
}

void FileTable::startDrag(Qt::DropActions actions) {
    if (dragStarter)
        dragStarter();
    else
        QTableView::startDrag(actions);
}
void FileTable::handleDefaultKeyPress(QKeyEvent *event) { QTableView::keyPressEvent(event); }
void FileIconList::handleDefaultKeyPress(QKeyEvent *event) { QListView::keyPressEvent(event); }
void FileIconList::startDrag(Qt::DropActions actions) {
    if (dragStarter)
        dragStarter();
    else
        QListView::startDrag(actions);
}

void FileIconList::SetListViewMode(int mode) {
    // Qt's Static movement resets dragEnabled; retain the provider's policy.
    const bool drag = dragEnabled();
    mode_ = mode;
    setViewMode(mode == 0 ? QListView::IconMode : QListView::ListMode);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setViewportMargins(mode == 0 ? QMargins(0, QtWorkspaceTheme::metric(*this, 20), 0,
                                            QtWorkspaceTheme::metric(*this, 28))
                                 : QMargins());
    int icon = style()->pixelMetric(mode == 0 ? QStyle::PM_LargeIconSize : QStyle::PM_SmallIconSize,
                                    nullptr, this);
    if (mode == 0)
        icon = qMax(icon, QtWorkspaceTheme::metric(*this,
#ifdef Q_OS_MACOS
                                                   72
#else
                                                   64
#endif
                                                   ));
    setIconSize(QSize(icon, icon));
    const QFontMetrics metrics = fontMetrics();
    const int margin = qMax(2, style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this));
    // Native LVS_ICON wraps labels. Keep room for two lines using the active
    // platform's icon/font metrics, rather than a fixed pixel-height cell.
    const int width = qMax(icon + 2 * margin, 12 * metrics.averageCharWidth() + 2 * margin);
    setGridSize(mode == 0   ? QSize(qMax(width, QtWorkspaceTheme::metric(*this, 112)),
                                    qMax(icon + 2 * metrics.lineSpacing() + 3 * margin,
                                         QtWorkspaceTheme::metric(*this, 116)))
                : mode == 1 ? QSize(qMax(180, width), qMax(icon, metrics.height()) + 2 * margin)
                            : QSize());
    setWordWrap(mode == 0);
    setFlow(mode == 2 ? QListView::TopToBottom : QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setDragEnabled(drag);
}

void FileIconList::changeEvent(QEvent *event) {
    QListView::changeEvent(event);
    if (mode_ >= 0 && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange))
        SetListViewMode(mode_);
}
