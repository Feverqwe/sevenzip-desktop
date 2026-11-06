#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

QtSourceSummary::QtSourceSummary(const QIcon &icon, const QString &name, const QString &path,
                                 QWidget *parent)
    : QWidget(parent), icon_(icon), symbol_(new QLabel(this)),
      name_(new QtWrappingLabel(name, this)), path_(new QtWrappingLabel(path, this)),
      detail_(new QtWrappingLabel(this)) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);
    symbol_->setPixmap(icon.pixmap(24, 24));
    symbol_->setFixedSize(24, 24);
    row->addWidget(symbol_, 0, Qt::AlignTop);
    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(4);
    for (auto *label : {name_, path_, detail_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setMinimumWidth(0);
    }
    auto *heading = new QHBoxLayout;
    heading->addWidget(name_, 1);
    heading->addWidget(detail_);
    text->addLayout(heading);
    text->addWidget(path_);
    detail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    detail_->hide();
    QFont title = name_->font();
    title.setWeight(QFont::DemiBold);
    name_->setFont(title);
    row->addLayout(text, 1);
    setToolTip(path);
    setAccessibleName(name);
    setAccessibleDescription(path);
    SetSelected(false);
}

void QtSourceSummary::SetSelected(bool selected) {
    name_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::Text);
    path_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
    detail_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
}

void QtSourceSummary::SetSource(const QString &name, const QString &path) {
    name_->setText(name);
    path_->setText(path);
    setToolTip(path);
    setAccessibleName(name);
    setAccessibleDescription(detail_->text().isEmpty() ? path : path + '\n' + detail_->text());
}

void QtSourceSummary::SetDetail(const QString &detail) {
    detail_->setText(detail);
    detail_->setVisible(!detail.isEmpty());
    setAccessibleDescription(detail.isEmpty() ? path_->text() : path_->text() + '\n' + detail);
}

void QtSourceSummary::SetSelectable(bool selectable) {
    for (auto *label : {name_, path_, detail_})
        label->setTextInteractionFlags(selectable ? Qt::TextSelectableByMouse |
                                                        Qt::TextSelectableByKeyboard
                                                  : Qt::NoTextInteraction);
}

void QtSourceSummary::SetIdentityPresentation() {
    identity_ = true;
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(16);
    RefreshIdentity();
}

void QtSourceSummary::RefreshIdentity() {
    const int extent = identity_ ? 48 : 24;
    symbol_->setFixedSize(extent, extent);
    symbol_->setPixmap(icon_.pixmap(extent, extent));
    if (identity_) {
        QFont title = font();
        if (title.pointSizeF() > 0)
            title.setPointSizeF(title.pointSizeF() * 1.6);
        else
            title.setPixelSize(qMax(1, qRound(title.pixelSize() * 1.6)));
        title.setWeight(QFont::DemiBold);
        name_->setFont(title);
    }
}

void QtSourceSummary::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::FontChange)
        RefreshIdentity();
}

int QtSourceSummary::heightForWidth(int width) const {
    return layout()->totalHeightForWidth(width);
}
