#include "filemanager/panels/Panel.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "fileops/scanner/filesystemscanner.h"
#include "localization/LangUtils.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScrollBar>

class FilesystemReloadTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
    static bool writeControl(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("control") == 7;
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void navigationDuringReloadStartsAtTop() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString oldPath = fixture.filePath("old folder");
        const QString newPath = fixture.filePath("new folder");
        QVERIFY(QDir().mkpath(oldPath) && QDir().mkpath(newPath));
        constexpr int count = 512;
        for (int i = 0; i < count; ++i) {
            const QString name = QStringLiteral("file %1.txt").arg(i);
            QVERIFY(writeControl(QDir(oldPath).filePath(name)));
            QVERIFY(writeControl(QDir(newPath).filePath(name)));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(oldPath);
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() >= count);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        QVERIFY(view->verticalScrollBar()->value() > 0);
        bool scheduled = false;
        bool navigated = false;
        connect(view->model(), &QAbstractItemModel::rowsInserted, &window, [&] {
            if (!scheduled && !view->isEnabled() && view->rowCount() > 1) {
                scheduled = true;
                QTimer::singleShot(0, &window, [&] {
                    navigated = true;
                    window.openPath(newPath);
                });
            }
        });
        view->sourcePanel->OnReload();
        QTRY_VERIFY(navigated);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() >= count);
        QCOMPARE(QDir::cleanPath(window.findChild<QLineEdit *>("archivePath")->text()),
                 QFileInfo(newPath).canonicalFilePath());
        QCOMPARE(view->verticalScrollBar()->value(), 0);
    }
    void missingFolderClearsNativeItems_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("alternative");
        QTest::addColumn<bool>("timer");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool alternative : {false, true})
                    for (bool timer : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(index)
                                                     .arg(language)
                                                     .arg(alternative)
                                                     .arg(timer)))
                            << index << language << alternative << timer;
    }
    void missingFolderClearsNativeItems() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, alternative);
        QFETCH(bool, timer);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("missing 文件"));
        const QString moved = fixture.filePath("retained");
        QVERIFY(QDir().mkpath(path));
        QVERIFY(writeControl(QDir(path).filePath(QStringLiteral("control δ.txt"))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(view->rowCount(), 1);
        view->setFocus();
        action(window, "selectAllAction")->trigger();
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_OperSmart(indices);
        QCOMPARE(indices.Size(), 1u);
        QVERIFY(QDir().rename(path, moved));
        if (timer) {
            panel.Set_AutoRefresh_Mode(true);
            panel.MarkPortableFolderChanged();
            panel.OnTimer();
            panel.Set_AutoRefresh_Mode(false);
        } else
            action(window, "refreshAction")->trigger();
        QCOMPARE(view->rowCount(), 0);
        UInt32 count = 42;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 0u);
        panel.Get_ItemIndices_OperSmart(indices);
        QVERIFY(indices.IsEmpty());
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(QDir().rename(moved, path));
        panel.OnReload();
        QCOMPARE(view->rowCount(), 1);
        QFile control(QDir(path).filePath(QStringLiteral("control δ.txt")));
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("control"));
    }
    void workerFailureRetainsSourceResult_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("flat");
        QTest::addColumn<bool>("reload");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool flat : {false, true})
                    for (bool reload : {false, true})
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(index).arg(language).arg(flat).arg(reload)))
                            << index << language << flat << reload;
    }
    void workerFailureRetainsSourceResult() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, flat);
        QFETCH(bool, reload);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString smallPath = fixture.filePath("small");
        const QString large = fixture.filePath("large");
        QVERIFY(QDir().mkpath(smallPath) && QDir().mkpath(large));
        QVERIFY(writeControl(QDir(smallPath).filePath(QStringLiteral("seed 文件.txt"))));
        for (int i = 0; i < 301; ++i)
            QVERIFY(
                writeControl(QDir(large).filePath(QStringLiteral("control 文件 %1.txt").arg(i))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        address->setFocus();
        address->setText(smallPath);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 1);
        if (reload) {
            address->setText(large);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(view->rowCount(), 301);
            QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        }
        const auto scanners = window.findChildren<FilesystemScanner *>();
        QCOMPARE(scanners.size(), 2);
        auto *scanner = scanners[sourceIndex];
        QSignalSpy failed(scanner, &FilesystemScanner::failed);
        QSignalSpy completed(scanner, &FilesystemScanner::completed);
        auto &panel = *view->sourcePanel;
        CMyComPtr<IFolderFolder> retained;
        bool restarted = false;
        const auto restart = connect(scanner, &FilesystemScanner::advanced, &window, [&](quint64) {
            if (!restarted) {
                restarted = true;
                retained = panel.GetFolder();
                scanner->start(fixture.filePath(QStringLiteral("missing 文件")), flat);
            }
        });
        QString shown;
        QTimer driver;
        widgetDialogs_.connectResponder(driver, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    shown = box->text();
                    box->accept();
                }
        });
        driver.start(0);
        if (reload)
            action(window, "refreshAction")->trigger();
        else {
            address->setText(large);
            QTest::keyClick(address, Qt::Key_Return);
        }
        QTRY_COMPARE(failed.size(), 1);
        driver.stop();
        QVERIFY(restarted);
        const HRESULT result = HRESULT(failed.first().first().toInt());
        QVERIFY(result != S_OK);
        QCOMPARE(shown, ToQString(HResultToMessage(result)));
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)retained);
        QVERIFY(panel.IsFSFolder());
        QCOMPARE(view->rowCount(), 0);
        CSelectedState selection;
        QCOMPARE(panel.RestoreLoadedSelection(selection), result);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(completed.isEmpty());
        QVERIFY(!scanner->isRunning());
        disconnect(restart);
        panel.OnReload();
        QTRY_COMPARE(view->rowCount(), 301);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(completed.size(), 1);
        QCOMPARE(failed.size(), 1);
        QFile control(QDir(large).filePath(QStringLiteral("control 文件 0.txt")));
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("control"));
        QVERIFY(panel.ShouldProcessNotifications());
    }
};

QTEST_MAIN(FilesystemReloadTests)
#include "filesystem_reload_tests.moc"
