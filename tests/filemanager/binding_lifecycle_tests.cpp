#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "support/progress_dialog.h"
#include <QElapsedTimer>
#include <QScopeGuard>

class LifecycleBackend : public ControlledBackend {
  public:
    void hash(const QStringList &paths, const QString &) override {
        hashPaths = paths;
        ++hashCalls;
    }
    QStringList hashPaths;
    int hashCalls = 0;
    void cancel() override {
        temporaryExistedOnCancel = QFileInfo::exists(observedTemporary);
        ControlledBackend::cancel();
    }
    QString observedTemporary;
    bool temporaryExistedOnCancel = false;
};

class BindingLifecycleTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static void calculateHash(MainWindow &window) {
        QTimer closeResult;
        connect(&closeResult, &QTimer::timeout, &window, [] {
            TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget());
            dismissWarnings();
        });
        closeResult.start(5);
        action(window, "hashSHA256Action")->trigger();
    }

    static void navigate(MainWindow &window, const QString &path) {
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(path);
        emit address->returnPressed();
    }
    static void selectAndForge(MainWindow &window) {
        auto *view = table(window);
        QCOMPARE(view->rowCount(), 1);
        view->selectRow(0);
        SetPanelData(view->model()->index(0, 0), QString("forged/previous-owner.txt"),
                     Qt::UserRole);
        SetPanelData(view->model()->index(0, 0), true, Qt::UserRole + 1);
        SetPanelData(view->model()->index(0, 1), "forged/previous-owner.txt", Qt::EditRole);
    }
    static void verifyLoadingCommands(MainWindow &window) {
        QCOMPARE(table(window)->rowCount(), 0);
        for (const char *name : {"viewFileAction", "editFileAction", "selectedPropertiesAction",
                                 "hashSHA256Action", "renameAction", "copyAction"})
            QVERIFY(!action(window, name)->isEnabled());
    }

  private slots:
    void closingWindowWaitsForSourceHashBeforeReleasingOwner() {
        LifecycleBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, "close hash lifetime.zip",
                          {{QStringLiteral("имя й с пробелом.txt"), 6, 3}});
        table(window)->selectRow(0);
        bool closeWasDeferred = false;
        backend.queueSourceExtractAction([&] {
            backend.observedTemporary = QFileInfo(backend.lastExtractDestination).absolutePath();
            closeWasDeferred = !window.close();
            backend.cancel();
        });
        calculateHash(window);
        QVERIFY(closeWasDeferred);
        QCOMPARE(backend.cancelCalls, 1);
        QVERIFY(backend.temporaryExistedOnCancel);
        QVERIFY(!backend.busy());
        QVERIFY(!QFileInfo::exists(backend.observedTemporary));
        QVERIFY(window.close());
        QCOMPARE(table(window)->rowCount(), 0);
        backend.completeList({{"late event.txt", 9, 5}});
        QCoreApplication::processEvents();
        QCOMPARE(backend.hashCalls, 0);
        QCOMPARE(backend.extractCalls, 1);
        QCOMPARE(table(window)->rowCount(), 0);
        QVERIFY(!window.isEnabled());
    }

    void transportCanStopImmediatelyAfterStart() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        ArchiveBackend backend(nullptr, QCoreApplication::applicationFilePath());
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        backend.list(temporary.filePath("starting-process.zip"));
        QElapsedTimer elapsed;
        elapsed.start();
        backend.cancelAndWait();
        QVERIFY(elapsed.elapsed() < 5000);
        QVERIFY(!backend.busy());
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }

    void destroyingWindowAfterSourceHashJoinsReleasesItsSpool() {
        LifecycleBackend backend;
        auto window = std::make_unique<MainWindow>(&backend);
        action(*window, "flatViewAction")->setChecked(false);
        openListedArchive(*window, backend, "owned-lifetime.zip",
                          {{QStringLiteral("имя й с пробелом.txt"), 6, 3}});
        table(*window)->selectRow(0);
        backend.queueSourceExtractAction([&] {
            backend.observedTemporary = QFileInfo(backend.lastExtractDestination).absolutePath();
            backend.cancel();
        });
        calculateHash(*window);
        QVERIFY(!backend.busy());
        QVERIFY(!QFileInfo::exists(backend.observedTemporary));
        const QPointer<MainWindow> destroyed = window.get();
        window.reset();
        QVERIFY(destroyed.isNull());
        QCOMPARE(backend.cancelCalls, 1);
        QVERIFY(backend.temporaryExistedOnCancel);
        QVERIFY(!backend.busy());
        QVERIFY(!QFileInfo::exists(backend.observedTemporary));
        backend.completeList({{"late event.txt", 9, 5}});
        QCoreApplication::processEvents();
        QCOMPARE(backend.extractCalls, 1);
    }

    void consumersRebindAfterReplacementAndFailedListing_data() {
        QTest::addColumn<bool>("flat");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("cancel");
        for (bool flat : {false, true})
            for (int mode : {0, 3})
                for (bool cancel : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3")
                                                 .arg(flat ? "flat" : "normal")
                                                 .arg(mode)
                                                 .arg(cancel ? "cancel" : "failure")))
                        << flat << mode << cancel;
    }
    void consumersRebindAfterReplacementAndFailedListing() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        QFETCH(bool, cancel);
        QTemporaryDir disk;
        QTemporaryDir destination;
        QVERIFY(disk.isValid() && destination.isValid());
        const QDir root(QFileInfo(disk.path()).canonicalFilePath());
        const QString diskPath = root.filePath(QStringLiteral("disk й с пробелом.txt"));
        QFile file(diskPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        LifecycleBackend backend;
        MainWindow window(&backend);
        action(window, "flatViewAction")->setChecked(false);
        navigate(window, root.path());
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        const QString oldPath = QStringLiteral("old й с пробелом.txt");
        QVERIFY(backend.prepareArchiveFixture("first-owner.zip"));
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.completeList({{oldPath, 6, 3}}); });
            verifyLoadingCommands(window);
        });
        window.openArchive("first-owner.zip");
        action(window, "flatViewAction")->setChecked(flat);
        selectAndForge(window);
        calculateHash(window);
        QCOMPARE(backend.lastSelected, QStringList{oldPath});
        const QString oldTemporary = backend.lastExtractDestination;
        QVERIFY(!QFileInfo::exists(oldTemporary));
        QVERIFY(action(window, "hashSHA256Action")->isEnabled());
        QVERIFY(action(window, "viewFileAction")->isEnabled());
        window.extractTo(destination.path(), true);
        QCOMPARE(backend.lastSelected, QStringList{oldPath});
        backend.cancel();

        const QString newPath = QStringLiteral("new й с пробелом.txt");
        QVERIFY(backend.prepareArchiveFixture("second-owner.zip"));
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.completeList({{newPath, 19, 7}}); });
            verifyLoadingCommands(window);
        });
        window.openArchive("second-owner.zip");
        QCOMPARE(table(window)->model()->index(0, 2).data(Qt::UserRole + 2).toULongLong(), 19ull);
        selectAndForge(window);
        window.extractTo(destination.path(), true);
        QCOMPARE(backend.lastSelected, QStringList{newPath});
        backend.cancel();
        calculateHash(window);
        QCOMPARE(backend.lastSelected, QStringList{newPath});
        const QString newTemporary = backend.lastExtractDestination;
        QVERIFY(!QFileInfo::exists(newTemporary));

        QVERIFY(backend.prepareArchiveFixture("failed-owner.zip"));
        QTimer warningResponder;
        connect(&warningResponder, &QTimer::timeout, &window, [] { dismissWarnings(); });
        warningResponder.start(1);
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] {
                if (cancel)
                    backend.cancel();
                else
                    backend.fail(QStringLiteral("Fixture listing failed."));
            });
            verifyLoadingCommands(window);
        });
        window.openArchive("failed-owner.zip");
        warningResponder.stop();
        QVERIFY(window.findChild<QLineEdit *>("archivePath")->text().endsWith("second-owner.zip"));
        QCOMPARE(rowWithPath(window, newPath), 0);
        navigate(window, root.path());
        QTRY_COMPARE(window.findChild<QLineEdit *>("archivePath")->text(), root.path());
        action(window, "flatViewAction")->setChecked(flat);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QTRY_VERIFY(table(window)->isEnabled());
        selectAndForge(window);
        const int requests = backend.extractCalls;
        backend.GuiRequests.clear();
        action(window, "hashSHA256Action")->trigger();
        QCOMPARE(backend.GuiRequests.size(), 1);
        const auto &hash = backend.GuiRequests.first();
        QCOMPARE(hash.Operation, QtOperationJob::Command::Hash);
        QCOMPARE(hash.HashMethod, QString("SHA256"));
        QStringList sources;
        for (const auto &path : hash.Paths)
            sources.append(QDir::cleanPath(QDir(hash.HashBasePrefix).absoluteFilePath(path)));
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources.first().normalized(QString::NormalizationForm_C),
                 diskPath.normalized(QString::NormalizationForm_C));
        QCOMPARE(backend.hashCalls, 0); // The filesystem request uses the rebound source path.
        QCOMPARE(backend.extractCalls, requests);
        backend.cancel();
        action(window, "flatViewAction")->setChecked(false);
    }
};

QTEST_MAIN(BindingLifecycleTests)
#include "binding_lifecycle_tests.moc"
