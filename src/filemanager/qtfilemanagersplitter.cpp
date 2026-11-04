// Splitter policy adapted from 7-Zip CPP/7zip/UI/FileManager/FM.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt handle and child-layout integration is local.
#include "filemanager/qtfilemanagersplitter.h"
#include "Common/Defs.h"
#include "localization/qtlangcontrols.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QSplitterHandle>
#include <QStyleOptionFocusRect>
#include <QStylePainter>

static const int kPanelSizeMin = 120;

void CSplitterPos::SetRatioFromPos(int width) {
    _ratio = (_pos + kSplitterWidth / 2) * kSplitterRateMax / MyMax(width, 1);
}

void CSplitterPos::SetRatio(int width, int aRatio) {
    _ratio = aRatio;
    SetPosFromRatio(width);
}

void CSplitterPos::SetPosPure(int width, int pos) {
    int posMax = width - kSplitterWidth;
    if (posMax < kPanelSizeMin * 2)
        pos = posMax / 2;
    else {
        if (pos > posMax - kPanelSizeMin)
            pos = posMax - kPanelSizeMin;
        else if (pos < kPanelSizeMin)
            pos = kPanelSizeMin;
    }
    _pos = pos;
}

void CSplitterPos::SetPos(int width, int pos) {
    _fullWidth = width;
    SetPosPure(width, pos);
    SetRatioFromPos(width);
}

void CSplitterPos::SetPosFromRatio(int width) {
    int fullWidth = width;
    if (_fullWidth != fullWidth && fullWidth != 0) {
        _fullWidth = fullWidth;
        SetPosPure(width, width * _ratio / kSplitterRateMax - kSplitterWidth / 2);
    }
}

namespace {
class PanelSplitterHandle final : public QSplitterHandle {
    QPointer<QWidget> previousFocus_;

  public:
    explicit PanelSplitterHandle(QSplitter *parent) : QSplitterHandle(Qt::Horizontal, parent) {
        setFocusPolicy(Qt::StrongFocus);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
            if (now == this && old)
                previousFocus_ = old;
        });
        QtBindLocalText(this, "PanelSplitter",
                        QT_TRANSLATE_NOOP("PanelSplitter", "Resize file panels"), true);
        QtBindLocalToolTip(
            this, "PanelSplitter",
            QT_TRANSLATE_NOOP("PanelSplitter",
                              "Drag or use Left/Right to resize panels. Double-click or press "
                              "Return for equal widths."));
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        // StrongFocus is needed for native macOS tab traversal. Pointer
        // resizing still returns focus to the list that was active before it.
        if (hasFocus() && previousFocus_)
            previousFocus_->setFocus(Qt::MouseFocusReason);
        QSplitterHandle::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *event) override {
        QSplitterHandle::paintEvent(event);
        if (hasFocus()) {
            QStyleOptionFocusRect option;
            option.initFrom(this);
            option.rect = rect();
            QStylePainter painter(this);
            painter.drawPrimitive(QStyle::PE_FrameFocusRect, option);
        }
    }
};
} // namespace

QSplitterHandle *QtFileManagerSplitter::createHandle() {
    auto *handle = new PanelSplitterHandle(this);
    handle->installEventFilter(this);
    return handle;
}

bool QtFileManagerSplitter::eventFilter(QObject *object, QEvent *event) {
    if (object == handle(1) && initialized_ && HasTwoPanels()) {
        bool reset = false;
        int delta = 0;
        if (event->type() == QEvent::MouseButtonDblClick) {
            reset = static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        } else if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier) {
                const int step = key->modifiers() == Qt::ShiftModifier ? 32 : 8;
                if (key->key() == Qt::Key_Left)
                    delta = -step;
                else if (key->key() == Qt::Key_Right)
                    delta = step;
                else
                    reset = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
                if (layoutDirection() == Qt::RightToLeft)
                    delta = -delta;
            }
        }
        if (reset || delta) {
            splitter_.SetPos(width(),
                             reset ? (width() - handleWidth()) / 2 : sizes().value(0) + delta);
            UpdateSourceLayout();
            event->accept();
            return true;
        }
    }
    return QSplitter::eventFilter(object, event);
}

QtFileManagerSplitter::QtFileManagerSplitter(QWidget *parent) : QSplitter(Qt::Horizontal, parent) {
    setHandleWidth(kSplitterWidth);
    setChildrenCollapsible(false);
    connect(this, &QSplitter::splitterMoved, this, [this](int position, int) {
        if (initialized_ && HasTwoPanels()) {
            splitter_.SetPos(width(), position);
            UpdateSourceLayout();
        }
    });
}

bool QtFileManagerSplitter::HasTwoPanels() const {
    return count() == 2 && !widget(0)->isHidden() && !widget(1)->isHidden();
}

void QtFileManagerSplitter::Initialize(bool panelsDefined, unsigned splitterPos) {
    if (panelsDefined)
        splitter_.SetPos(width(), static_cast<int>(splitterPos));
    else
        splitter_.SetRatio(width(), kSplitterRateMax / 2);
    initialized_ = true;
    UpdateSourceLayout();
}

void QtFileManagerSplitter::UpdateSourceLayout() {
    if (!initialized_)
        return;
    splitter_.SetPosFromRatio(width());
    const int position = splitter_.GetPos();
    const bool twoPanels = HasTwoPanels();
    const bool changed =
        appliedWidth_ != width() || appliedPosition_ != position || appliedTwoPanels_ != twoPanels;
    appliedWidth_ = width();
    appliedPosition_ = position;
    appliedTwoPanels_ = twoPanels;
    if (twoPanels && changed) {
        setSizes({position, width() - handleWidth() - position});
        appliedLeftSize_ = sizes().value(0);
    }
}

void QtFileManagerSplitter::RememberPosition() {
    // A content minimum can temporarily clamp the requested split. Only an
    // explicit external size change replaces the preferred source ratio.
    if (initialized_ && HasTwoPanels() && sizes().value(0) != appliedLeftSize_)
        splitter_.SetPos(width(), sizes().value(0));
}

int QtFileManagerSplitter::SourcePosition() const {
    return HasTwoPanels() ? sizes().value(0) : splitter_.GetPos();
}

void QtFileManagerSplitter::resizeEvent(QResizeEvent *event) {
    QSplitter::resizeEvent(event);
    UpdateSourceLayout();
}
