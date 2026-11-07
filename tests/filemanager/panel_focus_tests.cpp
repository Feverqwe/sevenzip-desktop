#include "filemanager/support/window_state_support.h"
#include "support/panel_keys.h"
#include <QFocusEvent>

class PanelFocusTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void independentPanelOwners() {
        CApp first;
        CApp second;
        first.Panels[0]._lastFocusedIsList = false;
        first.LastFocusedPanel = 1;
        QCOMPARE(&first.GetFocusedPanel(), &first.Panels[1]);
        QVERIFY(first.Panels[1]._lastFocusedIsList);
        QVERIFY(second.GetFocusedPanel()._lastFocusedIsList);
        QCOMPARE(second.GetFocusedPanelIndex(), 0u);
        first.NumPanels = 1;
        QVERIFY(!first.IsPanelVisible(0));
        QVERIFY(first.IsPanelVisible(1));
        first.NumPanels = 2;
        QVERIFY(first.IsPanelVisible(0));
    }

    void windowFocusReturnsToRememberedControl_data() {
        QTest::addColumn<int>("mode");
        for (int mode : {0, 3})
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }

    void windowFocusReturnsToRememberedControl() {
        QFETCH(int, mode);
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        window.show();
        window.activateWindow();
        QApplication::processEvents();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *stack = window.findChild<QStackedWidget *>("fileViewStack");
        QWidget *view = stack->currentWidget();
        QPushButton focusSink("sink", &window);
        focusSink.show();
        const QString path = address->text();
        for (QWidget *target : {static_cast<QWidget *>(address), view}) {
            if (target == address)
                PanelKeys::focusAddress(address);
            else
                target->setFocus();
            QTRY_VERIFY_WITH_TIMEOUT(target->hasFocus(), 1000);
            focusSink.setFocus();
            QTRY_VERIFY(focusSink.hasFocus());
            QFocusEvent focus(QEvent::FocusIn);
            QApplication::sendEvent(&window, &focus);
            QTRY_VERIFY_WITH_TIMEOUT(target->hasFocus(), 1000);
            QCOMPARE(address->text(), path);
        }
    }

    void presentationDestructionAndRebinding() {
        CApp app;
        {
            QWidget oldList;
            QLineEdit oldAddress;
            app.Panels[0].AttachPresentation(&oldList, &oldAddress);
            QFocusEvent focus(QEvent::FocusIn);
            QApplication::sendEvent(&oldAddress, &focus);
            QVERIFY(!app.Panels[0]._lastFocusedIsList);
        }
        app.SetFocusToLastItem(); // deleted widgets must be safe.
        QWidget newList;
        QLineEdit newAddress;
        app.Panels[0].AttachPresentation(&newList, &newAddress);
        QVERIFY(!app.Panels[0]._lastFocusedIsList);
        QFocusEvent focus(QEvent::FocusIn);
        QApplication::sendEvent(&newList, &focus);
        QVERIFY(app.Panels[0]._lastFocusedIsList);
    }
};

QTEST_MAIN(PanelFocusTests)
#include "panel_focus_tests.moc"
