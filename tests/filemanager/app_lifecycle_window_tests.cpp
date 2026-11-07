#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/support/window_state_support.h"
#include <QEvent>
#include <QScopeGuard>

namespace {
class ArchiveOwnerPresentation final : public IQtAppPresentation {
  public:
    int creates = 0;
    void Initialize(CApp &) override {}
    bool CreatePanel(CPanel &panel, unsigned) override {
        ++creates;
        panel.LoadFullPath();
        return true;
    }
    CPanelCallback *PanelCallback(unsigned) override { return nullptr; }
    void SetListSettings(unsigned, const CFmSettings &) override {}
    void ShowPanel(unsigned, bool) override {}
};
} // namespace

class AppLifecycleWindowTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void languageEventAfterCloseRetainsQuiescentWindow() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath("item.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("retained"), qint64(8));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QVERIFY(window.close());
        const int reads = backend.listCalls;
        QEvent language(QEvent::LanguageChange);
        QApplication::sendEvent(&window, &language);
        QVERIFY(!window.isVisible());
        QVERIFY(!backend.busy());
        QCOMPARE(backend.listCalls, reads);
    }
    void exitRemainsAvailableAfterRefresh() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath("имя с пробелом.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 1);
        auto *exit = action(window, "exitAction");
        QTRY_VERIFY(exit->isEnabled());
        exit->trigger();
        QVERIFY(!window.isVisible());
    }

    void hiddenCloseEndsApplicationLoop_data() {
        QTest::addColumn<bool>("peerVisible");
        QTest::addColumn<bool>("automaticQuit");
        QTest::addColumn<bool>("destroyWindow");
        for (bool destroy : {false, true}) {
            const QString suffix = destroy ? "-destroy" : "-close";
            QTest::newRow(qPrintable("hidden-only" + suffix)) << false << true << destroy;
            QTest::newRow(qPrintable("visible-peer" + suffix)) << true << true << destroy;
            QTest::newRow(qPrintable("automatic-quit-disabled" + suffix))
                << false << false << destroy;
        }
    }

    void hiddenCloseEndsApplicationLoop() {
        QFETCH(bool, peerVisible);
        QFETCH(bool, automaticQuit);
        QFETCH(bool, destroyWindow);
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        const QByteArray oldSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] { qputenv("SEVENZIP_SETTINGS_PATH", oldSettings); });
        qputenv("SEVENZIP_SETTINGS_PATH", settings.filePath("hidden-close.ini").toUtf8());
        const bool oldQuit = qApp->quitOnLastWindowClosed();
        const auto restoreQuit = qScopeGuard([&] { qApp->setQuitOnLastWindowClosed(oldQuit); });
        qApp->setQuitOnLastWindowClosed(automaticQuit);
        ControlledBackend backend;
        auto window = std::make_unique<MainWindow>(&backend);
        QWidget peer;
        if (peerVisible)
            peer.show();
        bool closed = false;
        bool peerRetained = false;
        bool timedOut = false;
        QTimer::singleShot(0, window.get(), [&] {
            if (destroyWindow) {
                window.reset();
                closed = true;
            } else
                closed = window->close();
        });
        QTimer peerClose;
        peerClose.setSingleShot(true);
        connect(&peerClose, &QTimer::timeout, [&] {
            peerRetained = peer.isVisible();
            peer.close();
        });
        if (peerVisible)
            peerClose.start(100);
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, [&] {
            timedOut = true;
            qApp->quit();
        });
        deadline.start(automaticQuit ? 2000 : 100);
        QCOMPARE(qApp->exec(), 0);
        QVERIFY(closed);
        QCOMPARE(timedOut, !automaticQuit);
        QCOMPARE(peerRetained, peerVisible);
    }

    void archiveStartupUsesJoinedSourceOwner_data() {
        QTest::addColumn<int>("result");
        QTest::newRow("success") << int(S_OK);
        QTest::newRow("failure") << int(E_FAIL);
        QTest::newRow("cancel") << int(E_ABORT);
    }

    void archiveStartupUsesJoinedSourceOwner() {
        QFETCH(int, result);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("архив startup.zip");
        ControlledBackend backend;
        QVERIFY(backend.prepareArchiveFixture(archive));
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        ArchiveOwnerPresentation presentation;
        CApp app;
        app.NumPanels = 1;
        for (auto &panel : app.Panels)
            panel.AttachFolderManager(manager, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        backend.queueListAction([&] {
            if (result == S_OK)
                backend.completeList({ArchiveEntry{"folder/имя с пробелом.txt", 1, 1}});
            else if (result == E_ABORT)
                backend.cancel();
            else
                backend.fail(QStringLiteral("startup failure"));
        });
        COpenResult opened;
        int widths[2] = {300, 300};
        QCOMPARE(app.Create(nullptr, ToUString(archive), UString(), widths, true, opened,
                            presentation, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")),
                 HRESULT(result));
        QVERIFY(!backend.busy());
        QCOMPARE(backend.listCalls, 1);
        QCOMPARE(presentation.creates, result == S_OK ? 1 : 0);
        QCOMPARE(app.Panels[0].PanelCreated, result == S_OK);
        QCOMPARE(opened.ArchiveIsOpened, result == S_OK);
        if (result == S_OK) {
            QVERIFY(app.Panels[0].IsArcFolder());
            QCOMPARE(app.Panels[0].GetParentFolders().Size(), 1u);
        }
        app.ReleaseApp();
        QVERIFY(!app.Panels[0].GetFolder());
        QVERIFY(app.Panels[0].GetParentFolders().IsEmpty());
    }

    void init() {
        QVERIFY(QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")) ||
                !QFileInfo::exists(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")));
    }

    void legacyPreferencesMigrateAndArchiveSaveUsesOuterParent() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        {
            QSettings key(settings, QSettings::IniFormat);
            key.setValue("view/mode", 2);
            key.setValue("view/flatArchive", true);
        }
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        {
            MainWindow window(&backend);
            QVERIFY(action(window, "viewMode3Action")->isChecked());
            openListedArchive(window, backend, fixture.filePath("архив с пробелом.zip"),
                              {ArchiveEntry{"folder/item.txt", 1, 1}});
            QCOMPARE(table(window)->rowCount(), 1);
            bool childVisible = false;
            for (int row = 0; row < table(window)->rowCount(); ++row)
                childVisible |=
                    table(window)->model()->index(row, 0).data(Qt::UserRole).toString() ==
                    QStringLiteral("folder/item.txt");
            QVERIFY(!childVisible);
            QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QString("folder"));
        }
        QSettings key(settings, QSettings::IniFormat);
        QCOMPARE(key.value("ListMode").toUInt() & 0xFF, 3u);
        QVERIFY(!key.contains("FM/FlatViewArc0"));
        QCOMPARE(QDir::cleanPath(key.value("PanelPath0").toString()), fixture.path());
    }

    void originalPerPanelPreferencesWinOverLegacyKeys() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        {
            QSettings key(settings, QSettings::IniFormat);
            key.setValue("view/mode", 2);
            key.setValue("view/flatArchive", true);
        }
        CListMode modes;
        modes.Panels[0] = 1;
        modes.Panels[1] = 0;
        modes.Save(settings);
        QSettings(settings, QSettings::IniFormat).setValue("FM/FlatViewArc0", true);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        {
            MainWindow window(&backend);
            QVERIFY(action(window, "viewMode3Action")->isChecked());
            openListedArchive(window, backend, fixture.filePath("archive.zip"),
                              {ArchiveEntry{"folder/item.txt", 1, 1}});
            QCOMPARE(table(window)->rowCount(), 1);
            action(window, "viewMode0Action")->trigger();
        }
        modes.Read(settings);
        QCOMPARE(modes.Panels[0], 0u);
        QCOMPARE(modes.Panels[1], 0u);
    }

    void copyModalRetainsApplicationLifetime() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile source(fixture.filePath("source.txt"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("control"), qint64(7));
        source.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        bool guarded = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("copyDialog");
            QVERIFY(dialog && dialog->isVisible());
            guarded = !window.close();
            dialog->reject();
        });
        action(window, "copyAction")->trigger();
        QVERIFY(guarded);
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(QFileInfo::exists(source.fileName()));
        QTRY_VERIFY(action(window, "exitAction")->isEnabled());
        QVERIFY(window.close());
    }
    void closeSavesGeometryAndIgnoresQueuedCompletion() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.setGeometry(40, 60, 720, 480);
        window.show();
        QCoreApplication::processEvents();
        const QRect geometry = window.geometry();
        // A worker can finish after close was requested. Its deferred signal
        // must neither refresh the released folder nor display an error dialog.
        QMetaObject::invokeMethod(
            &backend, [&backend] { emit backend.finished(false, QStringLiteral("late failure")); },
            Qt::QueuedConnection);
        QVERIFY(window.close());
        const int rows = table(window)->rowCount();
        QCOMPARE(rows, 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents();
        QCOMPARE(table(window)->rowCount(), rows);
        QVERIFY(!window.isEnabled());
        QVERIFY(!window.acceptDrops());
        const int calls = backend.listCalls;
        window.openArchive(QStringLiteral("closed.zip"));
        QCOMPARE(backend.listCalls, calls);
        QVERIFY(window.close());
        CWindowInfo info{};
        bool position = false, panels = false;
        info.Read(position, panels, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QVERIFY(position);
        QVERIFY(panels);
        QCOMPARE(info.rect.left, geometry.x());
        QCOMPARE(info.rect.top, geometry.y());
        QCOMPARE(info.rect.right, geometry.x() + geometry.width());
        QCOMPARE(info.rect.bottom, geometry.y() + geometry.height());
        QCOMPARE(info.numPanels, 1u);
    }

    void destructionWithModalChildSavesWindowInfo() {
        ControlledBackend backend;
        auto window = std::make_unique<MainWindow>(&backend);
        window->setGeometry(40, 60, 720, 480);
        window->show();
        QCoreApplication::processEvents();
        const QRect geometry = window->geometry();
        auto *dialog = new QDialog(window.get());
        dialog->setWindowModality(Qt::WindowModal);
        dialog->show();
        QTRY_COMPARE(QApplication::activeModalWidget(), dialog);
        QVERIFY(window->isVisible());
        window.reset();
        QVERIFY(!backend.busy());
        CWindowInfo info{};
        bool position = false, panels = false;
        info.Read(position, panels, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QVERIFY(position);
        QVERIFY(panels);
        QCOMPARE(info.rect.left, geometry.x());
        QCOMPARE(info.rect.top, geometry.y());
        QCOMPARE(info.rect.right, geometry.x() + geometry.width());
        QCOMPARE(info.rect.bottom, geometry.y() + geometry.height());
    }

    void sourceModalOpenCannotReleaseItsCaller() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        const QString archive = fixture.filePath("close during open.zip");
        QVERIFY(backend.prepareArchiveFixture(archive));
        bool attempted = false;
        backend.queueListAction([&] {
            attempted = true;
            QVERIFY(!window.close());
            auto *exit = action(window, "exitAction");
            exit->setEnabled(true);
            exit->trigger();
            QVERIFY(window.isVisible());
            QVERIFY(window.isEnabled());
            backend.completeList({ArchiveEntry{"имя с пробелом.txt", 1, 1}});
        });
        window.openArchive(archive);
        QVERIFY(attempted);
        QCOMPARE(table(window)->rowCount(), 1);
        QTRY_VERIFY(action(window, "exitAction")->isEnabled());
        action(window, "exitAction")->trigger();
        QVERIFY(!window.isVisible());
        QVERIFY(!backend.busy());
    }
};

QTEST_MAIN(AppLifecycleWindowTests)
#include "app_lifecycle_window_tests.moc"
