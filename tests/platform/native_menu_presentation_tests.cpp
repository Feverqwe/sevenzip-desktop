#include "platform/shell/qtnativeshellmenupresentation.h"
#include "support/native_menu_provider.h"
#include <QAbstractButton>
#include <QMenu>
#include <QWidgetAction>
#include <QtMath>
#include <QtTest>

using NativeMenuFixture::BitmapProvider;
using NativeMenuFixture::ProviderReport;

class NativeMenuPresentationTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeBitmapAlphaAndLegacyOpaquePixels_data() {
        QTest::addColumn<quint32>("pixel");
        QTest::addColumn<QColor>("color");
        QTest::newRow("opaque") << quint32(0xffdc280a) << QColor(220, 40, 10);
        QTest::newRow("premultiplied-alpha") << quint32(0x80800000) << QColor(255, 0, 0, 128);
        QTest::newRow("legacy-gdi-zero-alpha") << quint32(0x00dc280a) << QColor(220, 40, 10);
    }
    void nativeBitmapAlphaAndLegacyOpaquePixels() {
        QFETCH(quint32, pixel);
        QFETCH(QColor, color);
        ProviderReport report;
        auto *provider = new BitmapProvider(report, false, pixel);
        CMyComPtr<IContextMenu> context = provider;
        QVERIFY(provider->bitmap);
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        QVERIFY(AppendMenuW(native, MF_STRING, 100, L"Native alpha 空"));
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = provider->bitmap;
        QVERIFY(SetMenuItemInfoW(native, 0, TRUE, &item));
        QMenu target;
        QtNativeShellMenuPresentation presentation(native, context, 100);
        presentation.Render(target, [](unsigned) {});
        QCOMPARE(target.actions()[0]->icon().pixmap(QSize(2, 2)).toImage().pixelColor(0, 0), color);
    }
    void nativeBitmapOrientationAndTransparentEdge_data() {
        QTest::addColumn<bool>("bottomUp");
        QTest::newRow("top-down") << false;
        QTest::newRow("bottom-up") << true;
    }
    void nativeBitmapOrientationAndTransparentEdge() {
        QFETCH(bool, bottomUp);
        ProviderReport report;
        auto *provider = new BitmapProvider(report, false, 0, bottomUp);
        CMyComPtr<IContextMenu> context = provider;
        DIBSECTION section{};
        QCOMPARE(GetObjectW(provider->bitmap, sizeof(section), &section), int(sizeof(section)));
        auto *pixels = static_cast<DWORD *>(section.dsBm.bmBits);
        QVERIFY(pixels);
        const int first = bottomUp ? 2 : 0, second = bottomUp ? 0 : 2;
        pixels[first] = 0x80000080;
        pixels[first + 1] = 0;
        pixels[second] = 0x80800000;
        pixels[second + 1] = 0xff00ff00;
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        QVERIFY(AppendMenuW(native, MF_STRING, 100, L"Native orientation 空"));
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = provider->bitmap;
        QVERIFY(SetMenuItemInfoW(native, 0, TRUE, &item));
        QMenu target;
        QtNativeShellMenuPresentation presentation(native, context, 100);
        presentation.Render(target, [](unsigned) {});
        const auto image = target.actions()[0]->icon().pixmap(QSize(2, 2)).toImage();
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255, 128));
        QCOMPARE(image.pixelColor(1, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(0, 1), QColor(255, 0, 0, 128));
        QCOMPARE(image.pixelColor(1, 1), QColor(0, 255, 0));
    }
    void ownerDrawNativeMessagesStateDispatchAndLifetime_data() {
        QTest::addColumn<bool>("messages3");
        QTest::newRow("IContextMenu2") << false;
        QTest::newRow("IContextMenu3") << true;
    }
    void ownerDrawNativeMessagesStateDispatchAndLifetime() {
        QFETCH(bool, messages3);
        ProviderReport report;
        CMyComPtr<IContextMenu> context = new BitmapProvider(report, messages3);
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.fState = MFS_CHECKED | MFS_DEFAULT;
        item.wID = 104;
        item.dwItemData = 0x12345678;
        wchar_t label[] = L"Native owner draw 空";
        item.dwTypeData = label;
        QVERIFY(InsertMenuItemW(native, 0, TRUE, &item));
        auto menu = std::make_unique<QMenu>();
        QMenu &target = *menu;
        unsigned invoked = 0;
        {
            QtNativeShellMenuPresentation presentation(native, context, 100);
            QCOMPARE(presentation.Render(target, [&](unsigned id) { invoked = id; }),
                     QSet<unsigned>{104});
            QCOMPARE(report.itemData, ULONG_PTR(0x12345678));
            context.Release();
        }
        // Widgets retain the native provider/menu while they can request painting.
        QVERIFY(!report.destroyed);
        auto *action = qobject_cast<QWidgetAction *>(target.actions().first());
        QVERIFY(action);
        auto *row = qobject_cast<QAbstractButton *>(action->defaultWidget());
        QVERIFY(row);
        const qreal ratio = target.devicePixelRatioF();
        QCOMPARE(row->sizeHint(), QSize(qCeil(140 / ratio), qCeil(32 / ratio)));
        target.popup(QPoint(40, 40));
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        target.setActiveAction(action);
        const QImage image = row->grab().toImage();
        QVERIFY(report.drawn > 0);
        QVERIFY(report.drawState & ODS_CHECKED);
        QVERIFY(report.drawState & ODS_DEFAULT);
        QVERIFY(report.drawState & ODS_SELECTED);
        QCOMPARE(report.itemData, ULONG_PTR(0x12345678));
        QCOMPARE(image.pixelColor(0, 0), QColor(12, 80, 160));
        action->setEnabled(false);
        row->grab();
        QVERIFY(report.drawState & ODS_DISABLED);
        action->setEnabled(true);
        QTest::mouseClick(row, Qt::LeftButton);
        QCOMPARE(invoked, unsigned(104));
        QVERIFY(!target.isVisible());
        invoked = 0;
        target.popup(QPoint(40, 40));
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        target.setActiveAction(action);
        QTest::keyClick(&target, Qt::Key_Return);
        QCOMPARE(invoked, unsigned(104));
        QVERIFY(!target.isVisible());
        if (messages3) {
            invoked = 0;
            target.popup(QPoint(40, 40));
            QVERIFY(QTest::qWaitForWindowExposed(&target));
            QTest::keyClicks(&target, "z");
            QCOMPARE(report.menuChars, unsigned(1));
            QCOMPARE(invoked, unsigned(104));
            QVERIFY(!target.isVisible());
        }
        QCOMPARE(report.messages3 > 0, messages3);
        target.clear();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        // IContextMenu3 keyboard messages remain active for the menu's lifetime.
        QCOMPARE(report.destroyed, !messages3);
        QCOMPARE(bool(IsMenu(native)), messages3);
        menu.reset();
        QVERIFY(report.destroyed);
        QVERIFY(!IsMenu(native));
    }

    void failedOwnerDrawMeasurementDisablesUnrenderableCommand_data() {
        QTest::addColumn<bool>("submenu");
        QTest::newRow("command") << false;
        QTest::newRow("submenu") << true;
    }
    void failedOwnerDrawMeasurementDisablesUnrenderableCommand() {
        QFETCH(bool, submenu);
        ProviderReport report;
        report.refuseMeasure = true;
        CMyComPtr<IContextMenu> context = new BitmapProvider(report);
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        if (submenu) {
            const HMENU child = CreatePopupMenu();
            QVERIFY(child);
            QVERIFY(AppendMenuW(native, MF_OWNERDRAW | MF_POPUP, UINT_PTR(child), nullptr));
        } else
            QVERIFY(AppendMenuW(native, MF_OWNERDRAW, 104, nullptr));
        QMenu target;
        unsigned invoked = 0;
        QtNativeShellMenuPresentation presentation(native, context, 100);
        QVERIFY(presentation.Render(target, [&](unsigned id) { invoked = id; }).isEmpty());
        QCOMPARE(target.actions().size(), 1);
        QVERIFY(!target.actions()[0]->isEnabled());
        if (submenu) {
            QVERIFY(target.actions()[0]->menu());
            QCOMPARE(target.actions()[0]->menu()->actions().size(), 1);
        }
        target.actions()[0]->trigger();
        QCOMPARE(invoked, unsigned(0));
    }

    void callbackBitmapKeepsTextAndCopiesNativePixels() {
        ProviderReport report;
        report.callbackBitmap = true;
        CMyComPtr<IContextMenu> context = new BitmapProvider(report, true);
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        QVERIFY(AppendMenuW(native, MF_STRING, 104, L"Callback icon 空"));
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP | MIIM_DATA;
        item.hbmpItem = HBMMENU_CALLBACK;
        item.dwItemData = 0x87654321;
        QVERIFY(SetMenuItemInfoW(native, 0, TRUE, &item));
        QMenu target;
        {
            QtNativeShellMenuPresentation presentation(native, context, 100);
            QCOMPARE(presentation.Render(target, [](unsigned) {}), QSet<unsigned>{104});
            QCOMPARE(target.actions()[0]->text(), QStringLiteral("Callback icon 空"));
            QVERIFY(!qobject_cast<QWidgetAction *>(target.actions()[0]));
            QCOMPARE(report.itemData, ULONG_PTR(0x87654321));
            for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected})
                QCOMPARE(target.actions()[0]
                             ->icon()
                             .pixmap(QSize(2, 2), mode)
                             .toImage()
                             .pixelColor(0, 0),
                         QColor(12, 80, 160));
            context.Release();
        }
        QVERIFY(report.destroyed);
        QVERIFY(!IsMenu(native));
        QVERIFY(!target.actions()[0]->icon().isNull());
    }

    void ownerDrawSubmenuRetainsNativeHandlersAndDispatch_data() {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("enabled") << true;
        QTest::newRow("disabled") << false;
    }
    void ownerDrawSubmenuRetainsNativeHandlersAndDispatch() {
        QFETCH(bool, enabled);
        ProviderReport report;
        CMyComPtr<IContextMenu> context = new BitmapProvider(report, true);
        const HMENU native = CreatePopupMenu();
        const HMENU child = CreatePopupMenu();
        QVERIFY(native && child);
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_SUBMENU | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
        item.wID = 104;
        item.dwItemData = 0x12345678;
        item.hSubMenu = child;
        wchar_t label[] = L"Native submenu 空";
        item.dwTypeData = label;
        QVERIFY(InsertMenuItemW(native, 0, TRUE, &item));
        QMenu target;
        unsigned invoked = 0;
        QtNativeShellMenuPresentation presentation(native, context, 100);
        QCOMPARE(presentation.Render(target, [&](unsigned id) { invoked = id; }),
                 enabled ? QSet<unsigned>{103} : QSet<unsigned>{});
        auto *action = qobject_cast<QWidgetAction *>(target.actions().first());
        QVERIFY(action && action->menu());
        QCOMPARE(action->isEnabled(), enabled);
        QCOMPARE(action->menu()->actions()[0]->text(), QStringLiteral("Lazy data 空"));
        if (!enabled)
            return;
        auto *row = qobject_cast<QAbstractButton *>(action->defaultWidget());
        QVERIFY(row);
        target.popup(QPoint(40, 40));
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        QTest::mouseClick(row, Qt::LeftButton);
        QTRY_VERIFY(action->menu()->isVisible());
        QVERIFY(target.isVisible());
        QCOMPARE(invoked, unsigned(0));
        action->menu()->hide();
        QTest::keyClicks(&target, "z");
        QTRY_VERIFY(action->menu()->isVisible());
        QCOMPARE(invoked, unsigned(0));
        action->menu()->actions()[0]->trigger();
        QCOMPARE(invoked, unsigned(103));
    }

    void bitmapsStateLazyPopulationDispatchAndLifetime() {
        ProviderReport report;
        auto *provider = new BitmapProvider(report);
        CMyComPtr<IContextMenu> context = provider;
        QVERIFY(provider->bitmap);
        const HBITMAP bitmap = provider->bitmap;
        const HMENU native = CreatePopupMenu();
        QVERIFY(native);
        QVERIFY(AppendMenuW(native, MF_STRING | MF_CHECKED, 100, L"Bitmap данные 空"));
        QVERIFY(AppendMenuW(native, MF_STRING | MF_GRAYED, 101, L"Disabled bitmap"));
        QVERIFY(AppendMenuW(native, MF_STRING | MF_CHECKED, 102, L"Checked bitmap"));
        QVERIFY(AppendMenuW(native, MF_SEPARATOR, 0, nullptr));
        const HMENU child = CreatePopupMenu();
        QVERIFY(child);
        QVERIFY(AppendMenuW(native, MF_POPUP, UINT_PTR(child), L"Lazy group"));
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP | MIIM_STATE;
        item.hbmpItem = bitmap;
        item.fState = MFS_CHECKED | MFS_DEFAULT;
        QVERIFY(SetMenuItemInfoW(native, 0, TRUE, &item));
        item.fMask = MIIM_CHECKMARKS;
        item.hbmpItem = nullptr;
        item.hbmpUnchecked = bitmap;
        QVERIFY(SetMenuItemInfoW(native, 1, TRUE, &item));
        item.hbmpChecked = bitmap;
        item.hbmpUnchecked = nullptr;
        QVERIFY(SetMenuItemInfoW(native, 2, TRUE, &item));
        QMenu target;
        unsigned invoked = 0;
        {
            QtNativeShellMenuPresentation presentation(native, context, 100);
            const auto commands = presentation.Render(target, [&](unsigned id) { invoked = id; });
            QCOMPARE(commands, (QSet<unsigned>{100, 102, 103}));
            QCOMPARE(target.actions().size(), 5);
            QCOMPARE(target.actions()[0]->text(), QStringLiteral("Bitmap данные 空"));
            QCOMPARE(target.defaultAction(), target.actions()[0]);
            QVERIFY(target.actions()[0]->isChecked());
            QVERIFY(!target.actions()[1]->isEnabled());
            for (int i = 0; i < 3; ++i) {
                QVERIFY(!target.actions()[i]->icon().isNull());
                const QColor pixel =
                    target.actions()[i]->icon().pixmap(QSize(2, 2)).toImage().pixelColor(0, 0);
                QCOMPARE(pixel, QColor(220, 40, 10));
            }
            QCOMPARE(target.actions()[4]->menu()->actions()[0]->text(),
                     QStringLiteral("Lazy data 空"));
            target.actions()[4]->menu()->actions()[0]->trigger();
            QCOMPARE(invoked, unsigned(103));
            context.Release();
            QVERIFY(!report.destroyed);
        }
        QVERIFY(report.destroyed);
        QVERIFY(!IsMenu(native));
        // The Qt icon owns its copied pixels after native menu/provider release.
        QCOMPARE(target.actions()[0]->icon().pixmap(QSize(2, 2)).toImage().pixelColor(0, 0),
                 QColor(220, 40, 10));
    }
};

QTEST_MAIN(NativeMenuPresentationTests)
#include "native_menu_presentation_tests.moc"
