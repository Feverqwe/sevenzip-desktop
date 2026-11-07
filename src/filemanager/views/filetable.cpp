#include "filemanager/views/filetable.h"
#include "filemanager/sorting/filenamesort.h"
#include "filemanager/views/qtfileselectiongesture.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "platform/locale/qtregionalsettings.h"

#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QStyleOptionViewItem>
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
    QtRegionalSettings::Observe(*this, [this] { model_->invalidateRegionalText(); });
}

void FileTable::invalidateLanguageText() { model_->invalidateLanguageText(); }

void FileTable::changeEvent(QEvent *event) {
    QTableView::changeEvent(event);
    if (event->type() == QEvent::LocaleChange)
        model_->invalidateRegionalText();
}

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

void FileTable::InstallSelectionGesture(std::function<bool()> activate) {
    QtInstallFileSelectionGesture(
        *this,
        {[this](QRect area, QItemSelectionModel::SelectionFlags flags) {
             // QTableView requires both corners inside real cells, including
             // when the gesture starts below the final row.
             const QRect contents(-horizontalOffset(), -verticalOffset(),
                                  horizontalHeader()->length(), verticalHeader()->length());
             area = area.intersected(contents);
             if (area.isEmpty())
                 selectionModel()->select(QItemSelection(), flags);
             else
                 QTableView::setSelection(area, flags);
         },
         [this](QPoint point) {
             const auto index = indexAt(point);
             if (!index.isValid())
                 return false;
             QStyleOptionViewItem option;
             initViewItemOption(&option);
             option.rect = visualRect(index);
             return FileRowContainsPoint(*itemDelegateForIndex(index), option, index, point);
         },
         [this] { return QPoint(horizontalOffset(), verticalOffset()); }},
        std::move(activate));
}

void FileIconList::InstallSelectionGesture(std::function<bool()> activate) {
    QtInstallFileSelectionGesture(
        *this,
        {[this](const QRect &area, QItemSelectionModel::SelectionFlags flags) {
             const auto previous = state();
             setState(QAbstractItemView::DragSelectingState);
             QListView::setSelection(area, flags);
             setState(previous);
         },
         [this](QPoint point) {
             const auto index = indexAt(point);
             if (!index.isValid())
                 return false;
             QStyleOptionViewItem option;
             initViewItemOption(&option);
             option.rect = visualRect(index);
             return FileRowContainsPoint(*itemDelegateForIndex(index), option, index, point);
         },
         [this] { return QPoint(horizontalOffset(), verticalOffset()); }},
        std::move(activate));
}
void FileIconList::handleDefaultKeyPress(QKeyEvent *event) { QListView::keyPressEvent(event); }
void FileIconList::startDrag(Qt::DropActions actions) {
    if (dragStarter)
        dragStarter();
    else
        QListView::startDrag(actions);
}

void FileIconList::ConfigureLargeIcons() {
    // Qt's Static movement resets dragEnabled; retain the provider's policy.
    const bool drag = dragEnabled();
    largeIconsConfigured_ = true;
    setViewMode(QListView::IconMode);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setViewportMargins(0, QtWorkspaceTheme::metric(*this, 20), 0,
                       QtWorkspaceTheme::metric(*this, 28));
    const int icon = qMax(style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this),
                          QtWorkspaceTheme::metric(*this,
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
    setGridSize(QSize(
        qMax(width, QtWorkspaceTheme::metric(*this, 112)),
        qMax(icon + 2 * metrics.lineSpacing() + 3 * margin, QtWorkspaceTheme::metric(*this, 116))));
    setWordWrap(true);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setDragEnabled(drag);
}

void FileIconList::changeEvent(QEvent *event) {
    QListView::changeEvent(event);
    if (largeIconsConfigured_ &&
        (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange))
        ConfigureLargeIcons();
}
