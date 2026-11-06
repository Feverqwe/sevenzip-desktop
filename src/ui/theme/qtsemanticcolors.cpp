#include "ui/theme/qtsemanticcolors.h"
#include <QEvent>
#include <QTimer>
#include <QWidget>
#include <cmath>

namespace {
double luminance(const QColor &color) {
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
}
double contrast(const QColor &first, const QColor &second) {
    const double a = luminance(first), b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}
QColor mix(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgb(QColor::fromRgbF(base.redF() * (1 - amount) + tint.redF() * amount,
                                            base.greenF() * (1 - amount) + tint.greenF() * amount,
                                            base.blueF() * (1 - amount) + tint.blueF() * amount)
                               .rgb());
}
QPalette::ColorRole textRole(QPalette::ColorRole background) {
    return background == QPalette::Window ? QPalette::WindowText : QPalette::Text;
}
enum class Style { Surface, Link };
class PaletteStyle final : public QObject {
    QWidget &widget_;
    Style kind_;
    int radius_;
    bool updatePending_ = false;
    void scheduleUpdate() {
        if (updatePending_)
            return;
        updatePending_ = true;
        QTimer::singleShot(0, this, [this] {
            updatePending_ = false;
            update();
        });
    }
    void update() {
        QPalette palette =
            widget_.parentWidget() ? widget_.parentWidget()->palette() : widget_.palette();
        palette.setCurrentColorGroup(!widget_.isEnabled()       ? QPalette::Disabled
                                     : widget_.isActiveWindow() ? QPalette::Active
                                                                : QPalette::Inactive);
        const QString rule =
            kind_ == Style::Surface
                ? QString("background-color: %1; border-radius: %2px;")
                      .arg(QtSemanticColors::surface(palette, QPalette::Window).name())
                      .arg(radius_)
                : QString("color: %1;")
                      .arg(QtSemanticColors::readable(palette.color(QPalette::Link),
                                                      palette.color(QPalette::Window))
                               .name());
        QString style = QString("%1#%2 { %3 }")
                            .arg(widget_.metaObject()->className(), widget_.objectName(), rule);
        // Qt stylesheets do not inherit a parent's foreground like ordinary
        // widget palettes. Keep panel labels on the same palette as its surface.
        if (kind_ == Style::Surface)
            style += QString(" %1#%2 QLabel { color: %3; }")
                         .arg(widget_.metaObject()->className(), widget_.objectName(),
                              palette.color(QPalette::WindowText).name());
        if (widget_.styleSheet() != style)
            widget_.setStyleSheet(style);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::WindowActivate ||
            event->type() == QEvent::WindowDeactivate || event->type() == QEvent::EnabledChange)
            update();
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ParentChange)
            scheduleUpdate(); // Ancestor palettes can propagate after the window event.
        return false;
    }

  public:
    PaletteStyle(QWidget &widget, Style kind, int radius = 0)
        : QObject(&widget), widget_(widget), kind_(kind), radius_(radius) {
        widget.installEventFilter(this);
        widget.parentWidget()->installEventFilter(this);
        widget.window()->installEventFilter(this);
        update();
    }
};
} // namespace

namespace QtSemanticColors {
QColor readable(const QColor &preferred, const QColor &background) {
    if (contrast(preferred, background) >= 4.5)
        return preferred;
    const QColor end =
        contrast(Qt::white, background) > contrast(Qt::black, background) ? Qt::white : Qt::black;
    // Find the smallest tonal adjustment that keeps normal text readable.
    double low = 0, high = 1;
    QColor result = end;
    for (int step = 0; step < 24; ++step) {
        const double amount = (low + high) / 2;
        const QColor candidate = mix(preferred, end, amount);
        if (contrast(candidate, background) >= 4.5) {
            result = candidate;
            high = amount;
        } else
            low = amount;
    }
    return result;
}
QColor surface(const QPalette &palette, QPalette::ColorRole background) {
    const QColor base = palette.color(background), text = palette.color(textRole(background));
    double amount = 0.08;
    QColor result = mix(base, text, amount);
    while (contrast(text, result) < 4.5 && amount > 0.001) {
        amount /= 2;
        result = mix(base, text, amount);
    }
    return result;
}
QColor border(const QPalette &palette, QPalette::ColorRole background) {
    return mix(palette.color(background), palette.color(textRole(background)), 0.22);
}
QColor error(const QColor &background) { return readable(QColor(192, 0, 0), background); }
QColor success(const QColor &background) { return readable(QColor(34, 139, 34), background); }
QColor marked(const QPalette &palette) {
    const QColor base = palette.color(QPalette::Base), text = palette.color(QPalette::Text);
    double amount = 0.18;
    QColor result = mix(base, palette.color(QPalette::Highlight), amount);
    while (contrast(text, result) < 4.5 && amount > 0.001) {
        amount /= 2;
        result = mix(base, palette.color(QPalette::Highlight), amount);
    }
    return result;
}
void styleSurface(QWidget &widget, int radius) { new PaletteStyle(widget, Style::Surface, radius); }
void styleLink(QWidget &widget) { new PaletteStyle(widget, Style::Link); }
} // namespace QtSemanticColors
