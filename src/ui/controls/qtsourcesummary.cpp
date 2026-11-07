#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>

QtSourceSummary::QtSourceSummary(const QIcon &icon, const QString &name, const QString &path,
                                 QWidget *parent)
    : QWidget(parent), icon_(icon), symbol_(new QLabel(this)),
      name_(new QtWrappingLabel(name, this)), description_(new QtWrappingLabel(this)),
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
    for (auto *label : {name_, description_, detail_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        // The composite's height-for-width owns text height. A child's minimum
        // at its previous width must not pin a newly measured row to that height.
        auto policy = label->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        policy.setVerticalPolicy(QSizePolicy::Ignored);
        policy.setHeightForWidth(true);
        label->setSizePolicy(policy);
        label->setMinimumWidth(0);
    }
    auto *heading = new QHBoxLayout;
    heading->addWidget(name_, 1);
    heading->addWidget(detail_);
    text->addLayout(heading);
    text->addWidget(description_);
    description_->hide();
    auto detailPolicy = detail_->sizePolicy();
    detailPolicy.setHorizontalPolicy(QSizePolicy::Preferred);
    detail_->setSizePolicy(detailPolicy);
    detail_->hide();
    QFont title = name_->font();
    title.setWeight(QFont::DemiBold);
    name_->setFont(title);
    row->addLayout(text, 1);
    SetSource(name, path);
    SetSelected(false);
}

void QtSourceSummary::SetSelected(bool selected) {
    setBackgroundRole(selected ? QPalette::Highlight : QPalette::Base);
    name_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::Text);
    description_->setForegroundRole(selected ? QPalette::HighlightedText
                                             : QPalette::PlaceholderText);
    detail_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
}

void QtSourceSummary::SetSource(const QString &name, const QString &path) {
    name_->setText(name);
    sourcePath_ = path;
    setToolTip(path);
    setAccessibleName(name);
    RefreshAccessibleDescription();
}

void QtSourceSummary::SetDescription(const QString &description) {
    description_->setText(description);
    description_->setVisible(!description.isEmpty());
    RefreshAccessibleDescription();
}

void QtSourceSummary::SetDetail(const QString &detail, const QString &toolTip) {
    detail_->setText(detail);
    detail_->setVisible(!detail.isEmpty());
    detail_->setToolTip(toolTip);
    RefreshAccessibleDescription();
}

void QtSourceSummary::RefreshAccessibleDescription() {
    QStringList parts;
    for (const auto &text : {description_->text(), sourcePath_, detail_->text()})
        if (!text.isEmpty())
            parts.append(text);
    setAccessibleDescription(parts.join('\n'));
}

void QtSourceSummary::SetSelectable(bool selectable) {
    for (auto *label : {name_, description_, detail_})
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
