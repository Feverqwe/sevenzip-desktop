#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"

class PanelAddressKeysTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void sourceEditKeys_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("mode");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {QString("-"), QString("ru")})
                for (int mode : {1, 3})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(mode)))
                        << pane << language << mode;
    }

    void sourceEditKeys() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(int, mode);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile protectedFile(fixture.filePath(QStringLiteral("protected файл δ.txt")));
        QVERIFY(protectedFile.open(QIODevice::WriteOnly));
        QCOMPARE(protectedFile.write("address key control"), qint64(19));
        protectedFile.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        for (unsigned owner : {1 - pane, pane}) {
            auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(owner));
            auto *address = panel->findChild<QLineEdit *>("archivePath");
            address->setFocus();
            address->setText(fixture.path());
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(panel->findChild<FileTable *>("archiveTable")->rowCount() > 0);
        }
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *address = panel->findChild<QLineEdit *>("archivePath");
        auto *stack = panel->findChild<QStackedWidget *>("fileViewStack");
        const QString draft = QStringLiteral("unsubmitted путь with space");
        address->setFocus();
        QTRY_VERIFY(address->hasFocus());
        address->setText(draft);
        QTest::keyClick(address, Qt::Key_F9);
        QVERIFY(!peer->isVisible());
        QVERIFY(panel->isVisible());
        QTRY_VERIFY(address->hasFocus());
        QCOMPARE(address->text(), draft);
        QTest::keyClick(address, Qt::Key_F9);
        QVERIFY(peer->isVisible());
        QTRY_VERIFY(address->hasFocus());
        QCOMPARE(address->text(), draft);
        QTest::keyClick(address, Qt::Key_Tab);
        QTRY_VERIFY(stack->currentWidget()->hasFocus());
        QCOMPARE(address->text(), draft);
        address->setFocus();
        QTRY_VERIFY(address->hasFocus());
        QTest::keyClick(address, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_VERIFY(stack->currentWidget()->hasFocus());
        QCOMPARE(address->text(), draft);
        address->setFocus();
        QTRY_VERIFY(address->hasFocus());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*address, window));
            QVERIFY(UiInspection::fullyVisible(*stack->currentWidget(), window));
            QVERIFY(UiInspection::writeReport(window, QString("panel-address-keys-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
        const auto modifiers = Qt::ControlModifier | (pane ? Qt::ShiftModifier : Qt::NoModifier);
        QTest::keyClick(address, Qt::Key_W, modifiers);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(protectedFile.open(QIODevice::ReadOnly));
        QCOMPARE(protectedFile.readAll(), QByteArray("address key control"));
    }
};

QTEST_MAIN(PanelAddressKeysTests)
#include "panel_address_keys_tests.moc"
