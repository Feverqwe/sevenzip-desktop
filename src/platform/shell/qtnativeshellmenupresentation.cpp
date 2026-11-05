#include "platform/shell/qtnativeshellmenupresentation.h"
#ifdef _WIN32
#include "Windows/Menu.h"
#include "settings/ViewSettings.h"
#include <QAbstractButton>
#include <QEnterEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QScopeGuard>
#include <QWidgetAction>
#include <QtMath>

namespace {
class NativeMenuKeys final : public QObject {
    std::function<bool(QKeyEvent &)> _handle;

  public:
    NativeMenuKeys(QMenu &menu, std::function<bool(QKeyEvent &)> handle)
        : QObject(&menu), _handle(std::move(handle)) {
        menu.installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        return event->type() == QEvent::KeyPress && _handle(*static_cast<QKeyEvent *>(event));
    }
};

class NativeOwnerDrawRow final : public QAbstractButton {
    QSize _size;
    std::function<QImage(QSize, bool, bool)> _draw;
    QPointer<QMenu> _menu;
    QPointer<QAction> _action;

  public:
    NativeOwnerDrawRow(QSize size, std::function<QImage(QSize, bool, bool)> draw, QMenu &menu,
                       QAction &action)
        : QAbstractButton(&menu), _size(size), _draw(std::move(draw)), _menu(&menu),
          _action(&action) {
        setObjectName(QStringLiteral("nativeOwnerDrawRow"));
        setAccessibleName(action.text());
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        connect(&menu, &QMenu::hovered, this, [this] { update(); });
        connect(this, &QAbstractButton::clicked, this, [this] {
            const QPointer<QAction> action = _action;
            if (_menu && action && action->menu()) {
                action->menu()->popup(_menu->mapToGlobal(_menu->actionGeometry(action).topRight()));
                return;
            }
            if (_menu)
                _menu->hide();
            if (action)
                action->trigger();
        });
    }
    QSize sizeHint() const override { return _size; }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.drawImage(QPoint(),
                          _draw(size(), _menu && _menu->activeAction() == _action, !isEnabled()));
    }
    void enterEvent(QEnterEvent *event) override {
        if (_menu && _action && isEnabled())
            _menu->setActiveAction(_action);
        update();
        QAbstractButton::enterEvent(event);
    }
};

HBITMAP ItemBitmap(HMENU menu, unsigned position, const NWindows::CMenuItem &item) {
    MENUITEMINFOW bitmap{};
    bitmap.cbSize = sizeof(bitmap);
    bitmap.fMask = MIIM_BITMAP;
    HBITMAP handle = nullptr;
    if (GetMenuItemInfoW(menu, position, TRUE, &bitmap))
        handle = bitmap.hbmpItem;
    if (!handle)
        handle = item.fState & MFS_CHECKED ? item.hbmpChecked : item.hbmpUnchecked;
    return handle;
}
QIcon ItemIcon(HBITMAP handle) {
    // Special HBMMENU handles ask the native menu window to draw a system
    // glyph/callback. They are not GDI objects and cannot be converted as bitmaps.
    BITMAP description{};
    if (!handle || GetObjectW(handle, sizeof(description), &description) != sizeof(description))
        return {};
    auto image = QImage::fromHBITMAP(handle);
    if (description.bmBitsPixel == 32 && description.bmBits && !image.isNull()) {
        QImage native(image.size(), QImage::Format_ARGB32_Premultiplied);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = native.width();
        info.bmiHeader.biHeight = -native.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(nullptr);
        const int rows = dc ? GetDIBits(dc, handle, 0, UINT(native.height()), native.bits(), &info,
                                        DIB_RGB_COLORS)
                            : 0;
        if (dc)
            ReleaseDC(nullptr, dc);
        if (rows != native.height())
            return QIcon(QPixmap::fromImage(image));
        bool alpha = false;
        for (int y = 0; y < native.height() && !alpha; ++y) {
            const auto *pixels = reinterpret_cast<const QRgb *>(native.constScanLine(y));
            for (int x = 0; x < native.width(); ++x)
                alpha |= qAlpha(pixels[x]) != 0;
        }
        // Legacy GDI bitmaps leave every alpha byte zero. Shell menu DIBs with
        // meaningful alpha are premultiplied and must keep transparent edges.
        if (alpha)
            image = std::move(native);
    }
    return QIcon(QPixmap::fromImage(image));
}
} // namespace

struct QtNativeShellMenuPresentation::State : std::enable_shared_from_this<State> {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CMyComPtr<IContextMenu> context;
    CMyComPtr<IContextMenu2> messages;
    CMyComPtr<IContextMenu3> messages3;
    NWindows::CMenu nativeMenu;
    unsigned commandStart = 0;
    ~State() {
        nativeMenu.Destroy();
        messages3.Release();
        messages.Release();
        context.Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }
    HRESULT Message(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *output = nullptr) {
        if (messages3) {
            LRESULT result = 0;
            const HRESULT status = messages3->HandleMenuMsg2(message, wParam, lParam, &result);
            if (output)
                *output = result;
            return status;
        }
        return messages ? messages->HandleMenuMsg(message, wParam, lParam) : E_NOINTERFACE;
    }
    bool MenuChar(QMenu &target, HMENU menu, QKeyEvent &event) {
        if (!messages3 || event.text().size() != 1 || !event.text().at(0).isPrint() ||
            event.modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
            return false;
        LRESULT result = 0;
        if (FAILED(Message(WM_MENUCHAR, MAKEWPARAM(event.text().at(0).unicode(), MF_POPUP),
                           LPARAM(menu), &result)))
            return false;
        if (HIWORD(result) == MNC_CLOSE) {
            target.hide();
            return true;
        }
        if (HIWORD(result) != MNC_SELECT && HIWORD(result) != MNC_EXECUTE)
            return false;
        for (auto *action : target.actions()) {
            if (action->property("nativeShellMenuPosition").toUInt() != LOWORD(result))
                continue;
            if (!action->isEnabled() || action->isSeparator())
                return false;
            target.setActiveAction(action);
            if (HIWORD(result) == MNC_EXECUTE) {
                if (action->menu()) {
                    action->menu()->popup(
                        target.mapToGlobal(target.actionGeometry(action).topRight()));
                    return true;
                }
                const QPointer<QAction> retained(action);
                target.hide();
                if (retained)
                    retained->trigger();
            }
            return true;
        }
        return false;
    }
    QImage Draw(HWND window, HMENU menu, const NWindows::CMenuItem &item, QSize size, bool selected,
                bool disabled) {
        HDC screen = GetDC(window);
        if (!screen)
            return {};
        const auto releaseScreen = qScopeGuard([&] { ReleaseDC(window, screen); });
        HDC dc = CreateCompatibleDC(screen);
        if (!dc)
            return {};
        const auto releaseDc = qScopeGuard([&] { DeleteDC(dc); });
        HBITMAP bitmap = CreateCompatibleBitmap(screen, size.width(), size.height());
        if (!bitmap)
            return {};
        const auto releaseBitmap = qScopeGuard([&] { DeleteObject(bitmap); });
        const HGDIOBJ previous = SelectObject(dc, bitmap);
        auto restoreBitmap = qScopeGuard([&] { SelectObject(dc, previous); });
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_MENU;
        draw.itemID = item.wID;
        draw.itemAction = ODA_DRAWENTIRE;
        draw.itemState = (selected ? ODS_SELECTED : 0) | (disabled ? ODS_DISABLED : 0) |
                         (item.fState & MFS_CHECKED ? ODS_CHECKED : 0) |
                         (item.fState & MFS_DEFAULT ? ODS_DEFAULT : 0);
        draw.hwndItem = reinterpret_cast<HWND>(menu);
        draw.hDC = dc;
        draw.rcItem = {0, 0, size.width(), size.height()};
        draw.itemData = item.dwItemData;
        FillRect(dc, &draw.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        HRESULT rendered;
        {
            HFONT font =
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)
                    ? CreateFontIndirectW(&metrics.lfMenuFont)
                    : nullptr;
            const HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;
            const auto releaseFont = qScopeGuard([&] {
                if (font) {
                    SelectObject(dc, previousFont);
                    DeleteObject(font);
                }
            });
            rendered = Message(WM_DRAWITEM, 0, LPARAM(&draw));
        }
        // GetDIBits-based conversion requires the bitmap to be deselected.
        SelectObject(dc, previous);
        restoreBitmap.dismiss();
        return SUCCEEDED(rendered) ? QImage::fromHBITMAP(bitmap) : QImage();
    }
    QSize Measure(const NWindows::CMenuItem &item) {
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemID = item.wID;
        measure.itemData = item.dwItemData;
        if (FAILED(Message(WM_MEASUREITEM, 0, LPARAM(&measure))) || !measure.itemWidth ||
            !measure.itemHeight || measure.itemWidth > 16384 || measure.itemHeight > 16384)
            return {};
        return QSize(int(measure.itemWidth), int(measure.itemHeight));
    }
    QIcon Icon(HMENU menu, unsigned position, const NWindows::CMenuItem &item, QMenu &target) {
        const HBITMAP bitmap = ItemBitmap(menu, position, item);
        if (bitmap != HBMMENU_CALLBACK)
            return ItemIcon(bitmap);
        // Callback bitmaps use the same native ABI as owner-drawn rows, but
        // Windows/Qt still own the separate text column.
        const QSize size = Measure(item);
        if (size.isEmpty())
            return {};
        const HWND window = reinterpret_cast<HWND>(target.winId());
        QIcon icon;
        for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected}) {
            auto image =
                Draw(window, menu, item, size, mode == QIcon::Active || mode == QIcon::Selected,
                     mode == QIcon::Disabled);
            image.setDevicePixelRatio(target.devicePixelRatioF());
            icon.addPixmap(QPixmap::fromImage(image), mode);
        }
        return icon;
    }
    QAction *OwnerDraw(HMENU menu, const NWindows::CMenuItem &item, QMenu &target) {
        const QSize measured = Measure(item);
        if (measured.isEmpty())
            return nullptr;
        auto *action = new QWidgetAction(&target);
        action->setText(ToQString(item.StringValue));
        const HWND window = reinterpret_cast<HWND>(target.winId());
        const qreal ratio = target.devicePixelRatioF();
        const QSize logical(qCeil(measured.width() / ratio), qCeil(measured.height() / ratio));
        auto state = shared_from_this();
        auto *widget = new NativeOwnerDrawRow(
            logical,
            [state, item, menu, window, ratio](QSize size, bool selected, bool disabled) {
                auto image =
                    state->Draw(window, menu, item,
                                QSize(qCeil(size.width() * ratio), qCeil(size.height() * ratio)),
                                selected, disabled);
                image.setDevicePixelRatio(ratio);
                return image;
            },
            target, *action);
        action->setDefaultWidget(widget);
        target.addAction(action);
        return action;
    }
    QSet<unsigned> Render(HMENU handle, QMenu &target,
                          const std::function<void(unsigned)> &dispatch) {
        QSet<unsigned> commands;
        bool ownerDrawKeys = false;
        NWindows::CMenu menu;
        menu.Attach(handle);
        for (int position = 0; position < menu.GetItemCount(); ++position) {
            NWindows::CMenuItem item;
            item.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU |
                         MIIM_CHECKMARKS | MIIM_DATA;
            if (!menu.GetItem(unsigned(position), true, item))
                continue;
            if (item.IsSeparator()) {
                target.addSeparator();
                continue;
            }
            const bool ownerDraw = (item.fType & MFT_OWNERDRAW) != 0;
            ownerDrawKeys |= ownerDraw;
            auto *child = item.hSubMenu ? new QMenu(ToQString(item.StringValue), &target) : nullptr;
            QAction *action = ownerDraw ? OwnerDraw(handle, item, target) : nullptr;
            const bool enabled =
                (!ownerDraw || action) && !(item.fState & (MFS_DISABLED | MFS_GRAYED));
            if (child) {
                if (action)
                    action->setMenu(child);
                else {
                    target.addMenu(child);
                    action = child->menuAction();
                }
                Message(WM_INITMENUPOPUP, WPARAM(item.hSubMenu), MAKELPARAM(position, false));
                const auto childCommands = Render(item.hSubMenu, *child, dispatch);
                if (enabled)
                    commands.unite(childCommands);
            } else {
                if (!action)
                    action = target.addAction(ToQString(item.StringValue));
                if (item.wID >= commandStart && item.wID <= 0x7fff) {
                    wchar_t verb[256] = {};
                    if (SUCCEEDED(context->GetCommandString(item.wID - commandStart, GCS_VERBW,
                                                            nullptr, reinterpret_cast<LPSTR>(verb),
                                                            256)))
                        action->setProperty("shellVerb", QString::fromWCharArray(verb));
                    if (enabled) {
                        commands.insert(item.wID);
                        QObject::connect(action, &QAction::triggered, &target,
                                         [dispatch, id = item.wID] { dispatch(id); });
                    }
                }
            }
            action->setIcon(Icon(handle, unsigned(position), item, target));
            action->setEnabled(enabled);
            action->setProperty("shellCommandID", item.wID);
            action->setProperty("nativeShellMenu", true);
            action->setProperty("nativeShellMenuPosition", position);
            if (item.fState & MFS_CHECKED) {
                action->setCheckable(true);
                action->setChecked(true);
            }
            if (item.fState & MFS_DEFAULT)
                target.setDefaultAction(action);
        }
        if (ownerDrawKeys && messages3) {
            const auto state = shared_from_this();
            new NativeMenuKeys(target, [state, handle, &target](QKeyEvent &event) {
                return state->MenuChar(target, handle, event);
            });
        }
        return commands;
    }
};

QtNativeShellMenuPresentation::QtNativeShellMenuPresentation(HMENU menu,
                                                             const CMyComPtr<IContextMenu> &context,
                                                             unsigned commandStart)
    : _state(std::make_shared<State>()) {
    _state->context = context;
    _state->commandStart = commandStart;
    context.QueryInterface(IID_IContextMenu2, &_state->messages);
    context.QueryInterface(IID_IContextMenu3, &_state->messages3);
    _state->nativeMenu.Attach(menu);
}
QtNativeShellMenuPresentation::~QtNativeShellMenuPresentation() = default;
QSet<unsigned>
QtNativeShellMenuPresentation::Render(QMenu &target,
                                      const std::function<void(unsigned)> &dispatch) {
    return _state->Render(_state->nativeMenu, target, dispatch);
}
#endif
