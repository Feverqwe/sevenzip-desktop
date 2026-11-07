#include "filemanager/support/window_state_support.h"
#include "platform/support/native_properties_fixture.h"

using namespace NativePropertiesFixture;

class NativePropertiesWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int filesystemRow(MainWindow &window, const QString &path) {
        auto *view = table(window);
        for (int row = 0; row < view->rowCount(); ++row)
            if (QDir::cleanPath(QDir::fromNativeSeparators(
                    view->model()->index(row, 1).data().toString())) == path)
                return row;
        return -1;
    }
  private slots:
    void fileAndFolderUseNativeSheet_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("directory");
        for (int mode : {0, 3})
            for (bool directory : {false, true})
                QTest::newRow(qPrintable(QString("mode%1-dir%2").arg(mode).arg(directory)))
                    << mode << directory;
    }

    void fileAndFolderUseNativeSheet() {
        QFETCH(int, mode);
        QFETCH(bool, directory);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        // Unique disposable title identifies only this fixture's native sheet.
        const QString name =
            QFileInfo(fixture.path()).fileName() + QStringLiteral(" файл 密碼 space");
        const QString path = fixture.filePath(name);
        if (directory)
            QVERIFY(QDir().mkdir(path));
        else {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("fixture"), qint64(7));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *view = table(window);
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, path) >= 0, 5000);
        const int row = filesystemRow(window, path);
        QVERIFY(row >= 0);
        view->clearSelection();
        view->selectionModel()->select(view->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        // The user invokes Properties from an exposed File Manager window.
        // A synchronous fixture can reach this action before Qt has exposed it.
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(IsWindowVisible(reinterpret_cast<HWND>(window.winId())));
        std::atomic<bool> stop{false};
        auto observer = std::async(std::launch::async, ObserveAndClose, name,
                                   reinterpret_cast<HWND>(window.winId()), std::cref(stop));
        const auto finish = qScopeGuard([&] { stop = true; });
        QTimer portableResponder;
        portableResponder.setInterval(10);
        bool portableShown = false;
        connect(&portableResponder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                portableShown = true;
                stop = true;
                dialog->reject();
            }
        });
        portableResponder.start();
        action(window, directory ? "selectedPropertiesAction" : "archiveInfoAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            observer.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 15000);
        const ObservedPropertySheet sheet = observer.get();
        QVERIFY(!portableShown);
        QVERIFY2(sheet.window && sheet.tabs && sheet.visible, qPrintable(sheet.diagnostic));
        QTRY_VERIFY(!IsWindow(sheet.window));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(QFileInfo::exists(path));
        if (!directory) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("fixture"));
        }
    }

    void missingSelectionDoesNotOpenPortableFallback() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("missing файл 密碼.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "autoRefreshAction")->setChecked(false);
        auto *view = table(window);
        QTRY_VERIFY_WITH_TIMEOUT(view->isEnabled() && filesystemRow(window, path) >= 0, 5000);
        view->selectRow(filesystemRow(window, path));
        auto *properties = action(window, "selectedPropertiesAction");
        QTRY_VERIFY(properties->isEnabled());
        QVERIFY(QFile::remove(path));
        bool portableShown = false;
        QTimer responder;
        responder.setInterval(10);
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                portableShown = true;
                dialog->reject();
            }
        });
        responder.start();
        properties->trigger();
        QTest::qWait(20);
        QVERIFY(!portableShown);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(!QFileInfo::exists(path));
    }
};

QTEST_MAIN(NativePropertiesWindowTests)
#include "native_properties_window_tests.moc"
