#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogsurface.h"
#include <QAbstractTextDocumentLayout>
#include <QDialog>
#include <QEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextDocument>
#include <QTextOption>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>
#include <memory>

namespace {
bool WrapPlainText(const QLabel &label) {
    return label.wordWrap() && label.textFormat() == Qt::PlainText && !label.text().isEmpty() &&
           !label.buddy() && label.frameWidth() == 0 && label.margin() == 0 &&
           label.indent() <= 0 &&
           !(label.textInteractionFlags() &
             (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard));
}

std::unique_ptr<QTextDocument> WrappedDocument(const QLabel &label, int width) {
    auto document = std::make_unique<QTextDocument>();
    document->documentLayout()->setPaintDevice(const_cast<QLabel *>(&label));
    document->setDocumentMargin(0);
    document->setDefaultFont(label.font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    auto direction = Qt::LeftToRight;
    if (label.text().isRightToLeft())
        direction = Qt::RightToLeft;
    option.setTextDirection(direction);
    option.setAlignment(QStyle::visualAlignment(direction, label.alignment()) &
                        Qt::AlignHorizontal_Mask);
    document->setDefaultTextOption(option);
    document->setPlainText(label.text());
    document->setTextWidth(qMax(1, width));
    return document;
}

class DialogBody final : public QWidget {
  public:
    using QWidget::QWidget;
    int heightForWidth(int width) const override {
        const int minimum = layout() ? layout()->minimumHeightForWidth(width) : -1;
        return minimum >= 0 ? minimum : QWidget::heightForWidth(width);
    }
};

class DialogContentScroll final : public QScrollArea {
    void updateScrollbarSpace() {
        auto *bar = verticalScrollBar();
        int overlap = style()->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, bar);
        if (style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, bar))
            overlap = qMax(overlap, style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, bar));
        const int space = bar->maximum() > bar->minimum() ? qMax(0, overlap) : 0;
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const QMargins margins(rtl ? space : 0, 0, rtl ? 0 : space, 0);
        if (viewportMargins() != margins)
            setViewportMargins(margins);
    }
    void changeEvent(QEvent *event) override {
        QScrollArea::changeEvent(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange ||
            event->type() == QEvent::LayoutDirectionChange)
            updateScrollbarSpace();
    }

  public:
    explicit DialogContentScroll(QWidget *parent) : QScrollArea(parent) {
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this] { updateScrollbarSpace(); });
    }
    QSize minimumSizeHint() const override {
        QSize hint = QScrollArea::minimumSizeHint();
        if (widget())
            hint.setWidth(qMax(hint.width(), widget()->minimumSizeHint().width() +
                                                 frameWidth() * 2 +
                                                 style()->pixelMetric(QStyle::PM_ScrollBarExtent)));
        return hint;
    }
    QSize sizeHint() const override {
        if (!widget())
            return QScrollArea::sizeHint();
        return (widget()->minimumSizeHint() + QSize(frameWidth() * 2, frameWidth() * 2))
            .expandedTo(minimumSizeHint());
    }
};
} // namespace

QVBoxLayout *QtCreateDialogLayout(QWidget &window, QWidget &footer, QtDialogContent mode) {
    QtApplyDialogSurface(window);
    auto *frame = new QVBoxLayout(&window);
    frame->setContentsMargins(16, 16, 16, 16);
    frame->setSpacing(16);
    auto *content = new QVBoxLayout;
    content->setSpacing(8);
    if (mode == QtDialogContent::Scrollable) {
        auto *body = new DialogBody(&window);
        content->setContentsMargins(0, 0, 0, 0);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSpacing(0);
        bodyLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        bodyLayout->addLayout(content);
        bodyLayout->addStretch(1);
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

void QtFitScrollableDialog(QWidget &window) {
    auto *scroll = window.findChild<QScrollArea *>("dialogContentScroll");
    if (!scroll || !scroll->widget())
        return;
    window.layout()->activate();
    scroll->widget()->resize(scroll->viewport()->width(), scroll->widget()->height());
    scroll->widget()->layout()->invalidate();
    scroll->widget()->layout()->activate();
    const int bodyHeight = scroll->widget()->heightForWidth(scroll->viewport()->width());
    window.resize(window.width(), qMax(window.minimumSizeHint().height(),
                                       bodyHeight + window.height() - scroll->height()));
    QtNormalizeDialogSize(window, true);
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
    if (WrapPlainText(*this))
        hint.setWidth(qMin(hint.width(), fontMetrics().maxWidth() + contentsMargins().left() +
                                             contentsMargins().right()));
    if (wordWrap() && testAttribute(Qt::WA_Resized))
        hint.setHeight(qMax(hint.height(), heightForWidth(width())));
    return hint;
}

int QtWrappingLabel::heightForWidth(int width) const {
    if (!WrapPlainText(*this))
        return QLabel::heightForWidth(width);
    const auto margins = contentsMargins();
    return qCeil(
               WrappedDocument(*this, width - margins.left() - margins.right())->size().height()) +
           margins.top() + margins.bottom();
}

void QtWrappingLabel::paintEvent(QPaintEvent *event) {
    if (!WrapPlainText(*this)) {
        QLabel::paintEvent(event);
        return;
    }
    QFrame::paintEvent(event);
    auto document = WrappedDocument(*this, contentsRect().width());
    const qreal height = document->size().height();
    qreal top = contentsRect().top();
    if (alignment().testFlag(Qt::AlignVCenter))
        top += (contentsRect().height() - height) / 2;
    else if (alignment().testFlag(Qt::AlignBottom))
        top += contentsRect().height() - height;
    QPainter painter(this);
    painter.translate(contentsRect().left(), top);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.palette.setColor(
        QPalette::Text,
        palette().color(isEnabled() ? palette().currentColorGroup() : QPalette::Disabled,
                        foregroundRole()));
    document->documentLayout()->draw(&painter, context);
}

void QtWrappingLabel::resizeEvent(QResizeEvent *event) {
    QLabel::resizeEvent(event);
    if (event->oldSize().width() != event->size().width())
        updateGeometry();
}
