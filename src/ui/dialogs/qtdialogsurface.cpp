#include "ui/dialogs/qtdialogsurface.h"
#include "ui/theme/qtuitheme.h"
#include <QApplication>
#include <QEvent>
#include <QStyleHints>
#include <QWidget>

namespace {
class DialogSurface final : public QObject {
    QWidget &window_;
    void Apply() {
        const auto colors = QtUiTheme::colors();
        auto palette = QApplication::palette();
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            // Resolve native roles explicitly against the application, rather
            // than inheriting a file window's selection palette through parent.
            for (int index = 0; index < QPalette::NColorRoles; ++index) {
                const auto role = QPalette::ColorRole(index);
                if (role != QPalette::NoRole)
                    palette.setBrush(group, role, palette.brush(group, role));
            }
            palette.setColor(group, QPalette::Window, colors.window);
            palette.setColor(group, QPalette::Base, colors.surface);
            palette.setColor(group, QPalette::AlternateBase, colors.window);
            const auto text = group == QPalette::Disabled ? colors.subtle : colors.text;
            palette.setColor(group, QPalette::WindowText, text);
            palette.setColor(group, QPalette::Text, text);
            palette.setColor(group, QPalette::ButtonText, text);
        }
        if (window_.palette() != palette)
            window_.setPalette(palette);
        window_.setAutoFillBackground(true);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::StyleChange)
            Apply();
        return false;
    }

  public:
    explicit DialogSurface(QWidget &window) : QObject(&window), window_(window) {
        window.installEventFilter(this);
        connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this] { Apply(); });
        Apply();
    }
};
} // namespace

void QtApplyDialogSurface(QWidget &window) { new DialogSurface(window); }
