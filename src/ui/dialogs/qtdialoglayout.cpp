#include "ui/dialogs/qtdialoglayout.h"
#include <QDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace {
class DialogBody final : public QWidget {
  public:
    using QWidget::QWidget;
    int heightForWidth(int width) const override {
        const int minimum = layout() ? layout()->minimumHeightForWidth(width) : -1;
        return minimum >= 0 ? minimum : QWidget::heightForWidth(width);
    }
};

class DialogContentScroll final : public QScrollArea {
  public:
    using QScrollArea::QScrollArea;
    QSize sizeHint() const override {
        if (!widget())
            return QScrollArea::sizeHint();
        return (widget()->minimumSizeHint() + QSize(frameWidth() * 2, frameWidth() * 2))
            .expandedTo(minimumSizeHint());
    }
};
} // namespace

QVBoxLayout *QtCreateDialogLayout(QWidget &window, QWidget &footer, QtDialogContent mode) {
    auto *frame = new QVBoxLayout(&window);
    frame->setContentsMargins(16, 16, 16, 16);
    frame->setSpacing(16);
    auto *content = new QVBoxLayout;
    content->setSpacing(8);
    if (mode == QtDialogContent::Scrollable) {
        auto *body = new DialogBody(&window);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSizeConstraint(QLayout::SetMinAndMaxSize);
        body->setLayout(content);
        auto *scroll = new DialogContentScroll(&window);
        scroll->setObjectName("dialogContentScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFocusPolicy(Qt::NoFocus);
        scroll->setWidget(body);
        frame->addWidget(scroll, 1);
    } else {
        frame->addLayout(content, 1);
    }
    frame->addWidget(&footer);
    return content;
}

void QtSizeIconButton(QPushButton &button) {
    const QSize native = button.sizeHint();
    button.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button.setFixedWidth(qMax(native.width(), native.height()));
}

void QtUseContextDialogModality(QDialog &dialog) {
#ifdef Q_OS_MACOS
    if (dialog.parentWidget())
        dialog.setWindowModality(Qt::WindowModal);
#else
    Q_UNUSED(dialog);
#endif
}

QSize QtWrappingLabel::minimumSizeHint() const {
    QSize hint = QLabel::minimumSizeHint();
    if (wordWrap() && testAttribute(Qt::WA_Resized))
        hint.setHeight(qMax(hint.height(), heightForWidth(width())));
    return hint;
}

void QtWrappingLabel::resizeEvent(QResizeEvent *event) {
    QLabel::resizeEvent(event);
    if (event->oldSize().width() != event->size().width())
        updateGeometry();
}
