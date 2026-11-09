#include "filemanager/support/window_state_support.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include <QFocusEvent>

class PanelFocusTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void keyboardInputDuringRefresh_data() {
        QTest::addColumn<QString>("shortcutAction");
        QTest::newRow("list-navigation") << QString();
        QTest::newRow("modifier-only") << QString("modifier");
        QTest::newRow("switch-to-address") << QString("focusPathAction");
        QTest::newRow("switch-to-icons") << QString("viewMode0Action");
    }

    void keyboardInputDuringRefresh() {
        QFETCH(QString, shortcutAction);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *view = window.findChild<FileTable *>("archiveTable");
        QVERIFY(view);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        for (int i = 0; i < 301; ++i) {
            QFile file(fixture.filePath(QString("item-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(view, action(window, "refreshAction")->shortcut());
        QVERIFY(view->rowCount() < 301);
        QVERIFY(view->hasFocus());
        QVERIFY(!action(window, "openItemAction")->isEnabled());
        if (shortcutAction == "modifier") {
            QTest::keyClick(window.focusWidget(), Qt::Key_Control);
            QTest::keyClick(window.focusWidget(), Qt::Key_Shift);
        } else if (!shortcutAction.isEmpty())
            QTest::keySequence(window.focusWidget(),
                               action(window, qPrintable(shortcutAction))->shortcut());
        else
            QTest::keyClick(window.focusWidget(), Qt::Key_Down);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 301);
        if (shortcutAction == "focusPathAction")
            QVERIFY(window.findChild<QLineEdit *>("archivePath")->hasFocus());
        else {
            auto *focusedView =
                shortcutAction == "viewMode0Action"
                    ? static_cast<QAbstractItemView *>(window.findChild<QListView *>("iconList"))
                    : view;
            QVERIFY(focusedView && focusedView->isVisible() && focusedView->hasFocus());
            QTest::keyClick(focusedView, Qt::Key_Home);
            QTest::keyClick(focusedView, Qt::Key_Down);
            QVERIFY(focusedView->currentIndex().row() > 0);
        }
    }

    void changingViewPreservesTheActivePane_data() {
        QTest::addColumn<bool>("addressFocused");
        QTest::newRow("list") << false;
        QTest::newRow("address") << true;
    }

    void changingViewPreservesTheActivePane() {
        QFETCH(bool, addressFocused);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath("left/selected/inside")));
        QVERIFY(QDir().mkpath(fixture.filePath("right/peer")));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.filePath("left"));
        QTRY_VERIFY(filesystemRow(window, "selected") >= 0);
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *peerAddress = right->findChild<QLineEdit *>("archivePath");
        auto *peerFiles = right->findChild<FileTable *>("archiveTable");
        QVERIFY(address && peerAddress && peerFiles);
        PanelKeys::focusAddress(peerAddress);
        QTRY_VERIFY(peerAddress->hasFocus());
        peerAddress->setText(fixture.filePath("right"));
        QTest::keyClick(peerAddress, Qt::Key_Return);
        QTRY_COMPARE(peerAddress->text(), QFileInfo(fixture.filePath("right")).canonicalFilePath());
        QTRY_VERIFY(peerFiles->isEnabled());
        QTRY_COMPARE(peerFiles->model()->index(0, 0).data().toString(), QString("peer"));
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());

        auto *stack = left->findChild<QStackedWidget *>("fileViewStack");
        auto *files = left->findChild<FileTable *>("archiveTable");
        auto *icons = left->findChild<QListView *>("iconList");
        QVERIFY(stack && files && icons);
        const auto rowInLeftPane = [files](const QString &name) {
            for (int row = 0; row < files->rowCount(); ++row)
                if (files->model()->index(row, 0).data().toString() == name)
                    return row;
            return -1;
        };
        QTRY_VERIFY(files->isEnabled() && rowInLeftPane("selected") >= 0);
        action(window, "viewMode3Action")->trigger();
        files->selectRow(rowInLeftPane("selected"));
        if (addressFocused)
            PanelKeys::focusAddress(address);
        else
            files->setFocus();
        QTRY_VERIFY(addressFocused ? address->hasFocus() : files->hasFocus());
        QWidget *const initialFocus = window.focusWidget();
        QVERIFY(initialFocus);

        for (const int mode : {0, 3}) {
            action(window, mode == 0 ? "viewMode0Action" : "viewMode3Action")->trigger();
            auto *visible = qobject_cast<QAbstractItemView *>(stack->currentWidget());
            QCOMPARE(visible, mode == 0 ? static_cast<QAbstractItemView *>(icons) : files);
            QTRY_COMPARE(window.focusWidget(), addressFocused ? initialFocus : visible);
            QCOMPARE(visible->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(visible->selectionModel()->selectedRows().front().data().toString(),
                     QString("selected"));
        }
        if (addressFocused) {
            files->setFocus();
        } else {
            QVERIFY(QDir().mkpath(fixture.filePath("left/added")));
            QTest::keySequence(window.focusWidget(), action(window, "refreshAction")->shortcut());
            QTRY_VERIFY(rowInLeftPane("added") >= 0);
            QTRY_VERIFY(files->isEnabled() && files->hasFocus());
            QCOMPARE(files->currentIndex().siblingAtColumn(0).data().toString(),
                     QString("selected"));
        }
        QTest::keySequence(window.focusWidget(), action(window, "openItemAction")->shortcut());
        QTRY_VERIFY(rowInLeftPane("inside") >= 0);
        QCOMPARE(peerAddress->text(), QFileInfo(fixture.filePath("right")).canonicalFilePath());
    }

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
