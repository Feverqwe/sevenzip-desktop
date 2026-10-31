#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "support/window_activation.h"
#include <QApplication>
#include <QLineEdit>
#include <QtTest>
#include <memory>

class AddressLifetimeTests final : public QObject {
    Q_OBJECT
  private slots:
    void expiredOwnerCannotNavigate() {
        auto panel = std::make_unique<CPanel>();
        int navigated = 0;
        QtPanelAddressCombo combo(
            *panel,
            [&](const QString &, bool) {
                ++navigated;
                return E_ABORT;
            },
            nullptr);
        emit combo.lineEdit()->returnPressed();
        QCOMPARE(navigated, 1);
        panel.reset();
        emit combo.lineEdit()->returnPressed();
        emit combo.activated(0);
        QCOMPARE(combo.BindToPathAndRefresh(UString(L"unused"), false), E_ABORT);
        QCOMPARE(navigated, 1);
    }
    void ownerDestructionCancelsPendingFocus() {
        QWidget window;
        auto panel = std::make_unique<CPanel>();
        int focused = 0;
        auto *combo = new QtPanelAddressCombo(
            *panel, [](const QString &, bool) { return E_ABORT; }, &window, [&] { ++focused; });
        combo->resize(300, 40);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        combo->lineEdit()->setFocus();
        QTRY_VERIFY(combo->lineEdit()->hasFocus());
        combo->SetFocusToList();
        QTRY_COMPARE(focused, 1);
        combo->SetFocusToList();
        panel.reset();
        QCoreApplication::sendPostedEvents();
        QApplication::processEvents();
        QCOMPARE(focused, 1);
    }
    void controlDestructionCancelsPendingFocus() {
        CPanel panel;
        QWidget window;
        int focused = 0;
        auto combo = std::make_unique<QtPanelAddressCombo>(
            panel, [](const QString &, bool) { return E_ABORT; }, &window, [&] { ++focused; });
        combo->resize(300, 40);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        combo->lineEdit()->setFocus();
        QTRY_VERIFY(combo->lineEdit()->hasFocus());
        combo->SetFocusToList();
        combo.reset();
        QCoreApplication::sendPostedEvents();
        QApplication::processEvents();
        QCOMPARE(focused, 0);
    }
};

QTEST_MAIN(AddressLifetimeTests)
#include "address_lifetime_tests.moc"
