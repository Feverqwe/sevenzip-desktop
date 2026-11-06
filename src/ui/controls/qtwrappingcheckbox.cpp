#include "ui/controls/qtwrappingcheckbox.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMouseEvent>

QtWrappingCheckBox::QtWrappingCheckBox(QWidget *parent)
    : QWidget(parent), check_(new QCheckBox(this)), label_(new QtWrappingLabel(this)) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    label_->setWordWrap(true);
    label_->setTextFormat(Qt::PlainText);
    label_->setBuddy(check_);
    label_->installEventFilter(this);
    row->addWidget(check_, 0, Qt::AlignTop);
    row->addWidget(label_, 1);
    setFocusProxy(check_);
}

int QtWrappingCheckBox::heightForWidth(int width) const {
    return layout()->totalHeightForWidth(width);
}

bool QtWrappingCheckBox::eventFilter(QObject *watched, QEvent *event) {
    if (watched == label_ && event->type() == QEvent::MouseButtonPress) {
        pressed_ = static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        if (pressed_)
            return true;
    }
    if (watched == label_ && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const bool clicked = pressed_ && mouse->button() == Qt::LeftButton;
        pressed_ = false;
        if (clicked && label_->rect().contains(mouse->position().toPoint()) &&
            check_->isEnabled()) {
            check_->setFocus(Qt::MouseFocusReason);
            check_->click();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
