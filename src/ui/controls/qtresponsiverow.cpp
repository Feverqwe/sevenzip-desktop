#include "ui/controls/qtresponsiverow.h"
#include <QLayout>
#include <QLayoutItem>

namespace {
class ResponsiveLayout final : public QLayout {
    QList<QLayoutItem *> items_;
    mutable int measuredWidth_ = -1;
    mutable int measuredHeight_ = 0;

    QList<QLayoutItem *> visibleItems() const {
        QList<QLayoutItem *> result;
        for (auto *item : items_)
            if (!item->isEmpty())
                result.append(item);
        return result;
    }
    bool columnsFit(const QList<QLayoutItem *> &items, int width) const {
        int columnWidth = 0;
        for (auto *item : items)
            columnWidth = qMax(columnWidth, item->minimumSize().width());
        const int required =
            int(items.size()) * columnWidth + qMax(0, int(items.size()) - 1) * spacing();
        return required <= width;
    }
    static int itemHeight(QLayoutItem &item, int width) {
        const int preferred =
            item.hasHeightForWidth() ? item.heightForWidth(width) : item.sizeHint().height();
        return qMax(preferred, item.minimumSize().height());
    }

  public:
    explicit ResponsiveLayout(QWidget *parent) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(8);
    }
    ~ResponsiveLayout() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override {
        items_.append(item);
        invalidate();
    }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int index) const override {
        return index >= 0 && index < items_.size() ? items_[index] : nullptr;
    }
    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= items_.size())
            return nullptr;
        auto *item = items_.takeAt(index);
        invalidate();
        return item;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    QSize minimumSize() const override {
        QSize result(0, 0);
        for (auto *item : visibleItems())
            result = result.expandedTo(item->minimumSize());
        return result;
    }
    QSize sizeHint() const override {
        const auto items = visibleItems();
        QSize result(0, 0);
        for (auto *item : items) {
            const QSize hint = item->sizeHint().expandedTo(item->minimumSize());
            result.rwidth() += hint.width();
            result.setHeight(qMax(result.height(), hint.height()));
        }
        result.rwidth() += qMax(0, int(items.size()) - 1) * spacing();
        return result;
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (measuredWidth_ == width)
            return measuredHeight_;
        const auto items = visibleItems();
        const int count = int(items.size());
        if (!count)
            return 0;
        const bool columns = columnsFit(items, width);
        const int childWidth = columns ? (width - (count - 1) * spacing()) / count : width;
        int height = 0;
        for (auto *item : items) {
            const int needed = itemHeight(*item, childWidth);
            height = columns ? qMax(height, needed) : height + needed;
        }
        measuredWidth_ = width;
        measuredHeight_ = height + (columns ? 0 : (count - 1) * spacing());
        return measuredHeight_;
    }
    void invalidate() override {
        measuredWidth_ = -1;
        QLayout::invalidate();
    }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        const auto items = visibleItems();
        const int count = int(items.size());
        if (!count)
            return;
        const QRect area = contentsRect();
        const bool columns = columnsFit(items, area.width());
        const int childWidth =
            columns ? (area.width() - (count - 1) * spacing()) / count : area.width();
        int x = area.left(), y = area.top();
        const int rowHeight = heightForWidth(area.width());
        for (int i = 0; i < count; ++i) {
            const int height = columns ? rowHeight : itemHeight(*items[i], childWidth);
            const int width = columns && i == count - 1 ? area.right() - x + 1 : childWidth;
            items[i]->setGeometry(QRect(x, y, width, height));
            if (columns)
                x += childWidth + spacing();
            else
                y += height + spacing();
        }
    }
};
} // namespace

QtResponsiveRow::QtResponsiveRow(const QList<QWidget *> &items, QWidget *parent) : QWidget(parent) {
    auto *row = new ResponsiveLayout(this);
    for (auto *item : items) {
        row->addWidget(item);
        item->show();
    }
}
