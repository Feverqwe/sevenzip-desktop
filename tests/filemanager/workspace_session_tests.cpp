#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/scanner/filesystemscanner.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QJsonDocument>
#include <QMessageBox>
#include <QScopeGuard>
#include <QSplitterHandle>

class WorkspaceSessionTests final : public QObject {
    Q_OBJECT

    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *service = dynamic_cast<QtWindowWorkspace *>(object))
                return service;
        return nullptr;
    }
    static FileTable *active(MainWindow &window) {
        for (auto *table : window.findChildren<FileTable *>())
            if (table->isVisible())
                return table;
        return nullptr;
    }
    static QList<MainWindow *> windows() {
        QList<MainWindow *> result;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<MainWindow *>(widget); window && window->isVisible())
                result.append(window);
        return result;
    }
    static QString location(MainWindow &window) {
        const auto *table = active(window);
        return table ? QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix()))
                     : QString();
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void quitWithProtectedPeer_data() {
        QTest::addColumn<bool>("protectFirst");
        QTest::newRow("first-owner") << true;
        QTest::newRow("second-owner") << false;
    }
    void quitWithProtectedPeer() {
        QFETCH(bool, protectFirst);
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(fixture.path());
        second.openPath(fixture.path());
        MainWindow &owner = protectFirst ? first : second;
        ControlledBackend &backend = protectFirst ? firstBackend : secondBackend;
        const QString archive = fixture.filePath("protected source.zip");
        QVERIFY(backend.prepareArchiveFixture(archive));
        bool attempted = false;
        backend.queueListAction([&] {
            attempted = true;
            application.Quit();
            QVERIFY(first.isVisible() && first.isEnabled());
            QVERIFY(second.isVisible() && second.isEnabled());
            backend.completeList({ArchiveEntry{"payload.txt", 1, 1}});
        });
        owner.openArchive(archive);
        QVERIFY(attempted);
        QTRY_VERIFY(active(owner) && active(owner)->rowCount() == 1 && active(owner)->isEnabled());
        application.Quit();
        QVERIFY(!first.isVisible() && !second.isVisible());
        const QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"),
                                 QSettings::IniFormat);
        QCOMPARE(QJsonDocument::fromJson(settings.value("workspace/session").toByteArray())
                     .array()
                     .size(),
                 2);
    }
    void quitPreservesOpenForm_data() { quitWithProtectedPeer_data(); }
    void quitPreservesOpenForm() {
        QFETCH(bool, protectFirst);
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(fixture.path());
        second.openPath(fixture.path());
        QDialog form(protectFirst ? &first : &second);
        QLineEdit input(&form);
        input.setText(QStringLiteral("Unapplied ввод"));
        form.setWindowModality(Qt::WindowModal);
        form.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &form);
        QVERIFY(!(protectFirst ? first : second).close());
        application.Quit();
        QVERIFY(first.isVisible() && first.isEnabled());
        QVERIFY(second.isVisible() && second.isEnabled());
        QVERIFY(form.isVisible());
        QCOMPARE(input.text(), QStringLiteral("Unapplied ввод"));
        QCOMPARE(location(first), fixture.path());
        QCOMPARE(location(second), fixture.path());
        form.reject();
        QTRY_VERIFY(!QApplication::activeModalWidget());
        // A separate result window is not a file context and must not receive
        // a close request from the workspace's Quit commit.
        QDialog result;
        result.show();
        application.Quit();
        QVERIFY(!first.isVisible() && !second.isVisible());
        QVERIFY(result.isVisible());
        const QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"),
                                 QSettings::IniFormat);
        QCOMPARE(QJsonDocument::fromJson(settings.value("workspace/session").toByteArray())
                     .array()
                     .size(),
                 2);
        ControlledBackend reopenedBackend;
        MainWindow reopened(&reopenedBackend);
        reopened.show();
        reopened.openPath(fixture.path());
        application.Quit();
        QVERIFY(!reopened.isVisible());
        QVERIFY(result.isVisible());
    }
    void nativeRootSessionSurvivesLanguageChange_data() {
        QTest::addColumn<QString>("identity");
        QTest::addColumn<bool>("legacy");
        QStringList roots{QString("computer"), QString("locations")};
#ifndef Q_OS_WIN
        roots.append(QStringLiteral("network"));
#endif
        for (const auto &identity : roots) {
            QTest::newRow(qPrintable(identity + "-current")) << identity << false;
            QTest::newRow(qPrintable(identity + "-legacy")) << identity << true;
        }
    }
    void nativeRootSessionSurvivesLanguageChange() {
        QFETCH(QString, identity);
        QFETCH(bool, legacy);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(L"-", settings);
        ReloadLang(settings);
        const auto resetLanguage = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        if (identity == "computer")
            window.findChild<QAction *>("openRootAction")->trigger();
        else {
            QtNativePlaces places;
            const QString path = places.FindIdentity(identity).data(QtPlaceRoles::Path).toString();
            auto *address =
                dynamic_cast<QtPanelAddressCombo *>(window.findChild<QComboBox *>("pathCombo"));
            QVERIFY(address);
            QCOMPARE(address->BindToPathAndRefresh(ToUString(path), true), S_OK);
        }
        QTRY_VERIFY(active(window));
        QCOMPARE(QtNativePlaces::RootIdentity(*active(window)->sourcePanel), identity);
        auto saved = workspace(window)->Save();
        if (legacy) {
            auto panels = saved.value("panels").toArray();
            panels[0] = QJsonObject{
                {"tabs", QJsonArray{QJsonObject{{"path", fixture.path()}}, panels.at(0)}},
                {"active", 1}};
            saved.insert("panels", panels);
        }
        window.openPath(fixture.path());
        QTRY_COMPARE(location(window), fixture.path());
        SaveRegLang(L"ru", settings);
        ReloadLang(settings);
        bool restored = false;
        workspace(window)->Restore(saved, [&] { restored = true; });
        QTRY_VERIFY(restored);
        QTRY_COMPARE(QtNativePlaces::RootIdentity(*active(window)->sourcePanel), identity);
        QVERIFY(!window.findChild<QAction *>("createFolderAction")->isEnabled());
    }
    void newWindowKeepsNativeRootAcrossDeferredLanguageChange() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(L"-", settings);
        ReloadLang(settings);
        const auto resetLanguage = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow source(&backend);
        source.show();
        source.findChild<QAction *>("openRootAction")->trigger();
        QTRY_VERIFY(active(source));
        QCOMPARE(QtNativePlaces::RootIdentity(*active(source)->sourcePanel), QString("computer"));
        source.findChild<QAction *>("newWindowAction")->trigger();
        SaveRegLang(L"ru", settings);
        ReloadLang(settings);
        QPointer<MainWindow> copy;
        const auto close = qScopeGuard([&] {
            if (copy)
                copy->close();
        });
        QTRY_COMPARE(windows().size(), 2);
        for (auto *window : windows())
            if (window != &source)
                copy = window;
        QVERIFY(copy);
        QTRY_VERIFY(active(*copy) && active(*copy)->isEnabled());
        QTRY_COMPARE(QtNativePlaces::RootIdentity(*active(*copy)->sourcePanel),
                     QString("computer"));
        QVERIFY(!copy->findChild<QAction *>("createFolderAction")->isEnabled());
    }

    void unavailableNativePlaceDoesNotOpenItsOldPathAsAFolder() {
        const QString identity = QStringLiteral("volumes");
        QtNativePlaces nativePlaces;
        if (nativePlaces.FindIdentity(identity).isValid())
            QSKIP("This platform provides the saved device namespace");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString stalePath = fixture.filePath("Stale system path");
        QVERIFY(QDir().mkpath(stalePath));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        const QJsonObject panel{{"path", stalePath}, {"nativePlace", identity}};
        const QJsonObject saved{{"panels", QJsonArray{panel}}, {"count", 1}};
        bool restored = false;
        workspace(window)->Restore(saved, [&] { restored = true; });
        QTRY_VERIFY(restored);
        auto *retry = window.findChild<QPushButton *>("navigationRetry0");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(retry && address);
        QVERIFY(retry->isVisible());
        QVERIFY(!active(window)->isEnabled());
        QCOMPARE(active(window)->rowCount(), 0);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), stalePath);
        retry->click();
        QVERIFY(retry->isVisible());
        QVERIFY(!active(window)->isEnabled());
        QCOMPARE(active(window)->rowCount(), 0);
        auto *parent = window.findChild<QPushButton *>("navigationParent0");
        QVERIFY(parent);
        parent->click();
        QTRY_VERIFY(active(window)->isEnabled() && !retry->isVisible());
        QCOMPARE(QtNativePlaces::RootIdentity(*active(window)->sourcePanel),
                 QStringLiteral("locations"));
        QVERIFY(!window.findChild<QAction *>("createFolderAction")->isEnabled());
        window.openPath(stalePath);
        QTRY_VERIFY(active(window)->isEnabled());
        QCOMPARE(location(window), stalePath);
    }

    void closingDuringRestoreDoesNotBlockPeerPersistence() {
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString saved = fixture.filePath("Saved папка");
        const QString other = fixture.filePath("Other folder");
        const QString peerPath = fixture.filePath("Peer final");
        for (const auto &path : {saved, other, peerPath})
            QVERIFY(QDir().mkpath(path));
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, peerBackend;
        MainWindow first(&firstBackend);
        first.show();
        first.openPath(saved);
        QTRY_COMPARE(location(first), saved);
        application.Save();
        first.openPath(other);
        QTRY_COMPARE(location(first), other);
        MainWindow peer(&peerBackend);
        peer.show();
        peer.openPath(other);
        QTRY_COMPARE(location(peer), other);
        bool closeScheduled = false;
        QTimer closeRestored;
        closeRestored.setInterval(1);
        connect(&closeRestored, &QTimer::timeout, &first, [&] {
            if (location(first) == saved) {
                closeScheduled = true;
                closeRestored.stop();
                first.close();
            }
        });
        closeRestored.start();
        application.Restore(first);
        QTRY_VERIFY(closeScheduled);
        QTRY_VERIFY(!first.isVisible());
        QVERIFY(peer.isVisible());
        peer.openPath(peerPath);
        QTRY_COMPARE(location(peer), peerPath);
        const auto persistedPath = [&] {
            application.Save();
            const QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"),
                                     QSettings::IniFormat);
            const auto windows =
                QJsonDocument::fromJson(settings.value("workspace/session").toByteArray()).array();
            if (windows.size() != 1)
                return QString();
            return windows[0]
                .toObject()
                .value("panels")
                .toArray()[0]
                .toObject()
                .value("path")
                .toString();
        };
        QTRY_COMPARE(QDir::cleanPath(persistedPath()), peerPath);
    }
    void restoredFolderKeepsRetryTargetAfterBackgroundReadFailure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString origin = fixture.filePath("Original");
        const QString target = fixture.filePath("Saved папка");
        QVERIFY(QDir().mkpath(origin) && QDir().mkpath(target));
        for (int i = 0; i < 301; ++i) {
            QFile file(QDir(target).filePath(QString::number(i) + ".txt"));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(origin);
        QTRY_COMPARE(location(window), origin);
        QTRY_VERIFY(active(window)->isEnabled());
        auto *scanner = window.findChild<FilesystemScanner *>();
        auto *retry = window.findChild<QPushButton *>("navigationRetry0");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(scanner && retry && address);
        connect(
            scanner, &FilesystemScanner::advanced, &window,
            [&] { scanner->start(fixture.filePath("Missing"), false); }, Qt::SingleShotConnection);
        QSignalSpy failed(scanner, &FilesystemScanner::failed);
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                message->accept();
        });
        dismiss.start(1);
        const QJsonObject saved{{"panels", QJsonArray{QJsonObject{{"path", target}}}},
                                {"count", 1}};
        bool restored = false;
        workspace(window)->Restore(saved, [&] { restored = true; });
        QTRY_VERIFY(restored);
        QCOMPARE(failed.size(), 1);
        QVERIFY(retry->isVisible());
        QVERIFY(!active(window)->isEnabled());
        QCOMPARE(active(window)->rowCount(), 0);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), target);
        retry->click();
        QTRY_VERIFY(active(window)->isEnabled() && !retry->isVisible());
        QCOMPARE(location(window), target);
        QVERIFY(active(window)->rowCount() >= 301);
        QVERIFY(!window.findChild<QAction *>("backAction")->isEnabled());
    }

    void sessionRestoresCurrentLocationOfEachWindow() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString a = fixture.filePath("A folder");
        const QString b = fixture.filePath("B folder");
        const QString c = fixture.filePath("C folder");
        for (const auto &path : {a, b, c})
            QVERIFY(QDir().mkpath(path));
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        {
            QtWorkspaceApplication application(*qApp);
            ControlledBackend firstBackend, secondBackend;
            MainWindow first(&firstBackend), second(&secondBackend);
            first.show();
            second.show();
            first.openPath(a);
            QTRY_VERIFY(active(first) && active(first)->isEnabled());
            first.openPath(b);
            second.openPath(c);
            application.Quit();
            QVERIFY(!first.isVisible() && !second.isVisible());
        }
        {
            QtWorkspaceApplication application(*qApp);
            ControlledBackend backend;
            MainWindow first(&backend);
            first.show();
            application.Restore(first);
            QTRY_COMPARE_WITH_TIMEOUT(windows().size(), 2, 10000);
            QTRY_COMPARE_WITH_TIMEOUT(location(first), b, 10000);
            MainWindow *second = nullptr;
            for (auto *window : windows())
                if (window != &first)
                    second = window;
            QVERIFY(second);
            QTRY_COMPARE_WITH_TIMEOUT(location(*second), c, 10000);
            first.openPath(a);
            QTRY_COMPARE(location(first), a);
            QCOMPARE(location(*second), c);
            application.Quit();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }

    void restoringCompactWindowKeepsItsCustomFileAreaSplit() {
        QJsonObject saved;
        QList<int> original;
        {
            ControlledBackend backend;
            MainWindow first(&backend);
            first.resize(640, 400);
            first.show();
            first.findChild<QAction *>("twoPanelsAction")->trigger();
            auto *splitter = first.findChild<QSplitter *>("filePanelSplitter");
            QTRY_VERIFY(splitter && splitter->widget(1)->isVisible());
            auto *handle = splitter->handle(1);
            QTest::keyClick(handle, Qt::Key_Right, Qt::ShiftModifier);
            original = splitter->sizes();
            saved = workspace(first)->Save();
        }
        ControlledBackend backend;
        MainWindow restored(&backend);
        restored.resize(980, 620);
        restored.show();
        bool done = false;
        workspace(restored)->Restore(saved, [&] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        auto *splitter = restored.findChild<QSplitter *>("filePanelSplitter");
        QTRY_COMPARE(splitter->sizes(), original);
        restored.resize(980, 620);
        QApplication::processEvents();
        restored.resize(640, 400);
        QApplication::processEvents();
        const auto resized = splitter->sizes();
        QVERIFY(qAbs(resized.value(0) - original.value(0)) <= 1);
    }

    void missingSavedLocationDoesNotExposeFallbackFolderCommands_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void missingSavedLocationDoesNotExposeFallbackFolderCommands() {
        QFETCH(QString, language);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restoreSettings = qScopeGuard([&] {
            if (previousSettings.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previousSettings);
        });
        const QString settings = fixture.filePath("session.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const QString gone = fixture.filePath("Removed folder");
        const QString usable = fixture.filePath("Available folder");
        QtWorkspaceApplication application(*qApp);
        QVERIFY(QDir().mkpath(gone));
        QVERIFY(QDir().mkpath(usable));
        QJsonObject saved;
        {
            ControlledBackend backend;
            MainWindow window(&backend);
            window.show();
            window.openPath(gone);
            QTRY_VERIFY(active(window)->isEnabled());
            saved = workspace(window)->Save();
        }
        QVERIFY(QDir().rmdir(gone));
        ControlledBackend backend;
        MainWindow restored(&backend);
        restored.show();
        bool done = false;
        workspace(restored)->Restore(saved, [&] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(active(restored));
        QVERIFY(!active(restored)->isEnabled());
        QVERIFY(!restored.findChild<QAction *>("createFolderAction")->isEnabled());
        QVERIFY(restored.findChild<QLabel *>("selectedItemsStatus")->text().isEmpty());
        QCOMPARE(QDir::cleanPath(restored.findChild<QLineEdit *>("archivePath")->text()), gone);
        QLabel *message = nullptr;
        for (auto *label : restored.findChildren<QLabel *>())
            if (QDir::fromNativeSeparators(label->text()).contains(gone))
                message = label;
        QVERIFY(message);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            restored.resize(size);
            QTRY_VERIFY(UiInspection::fullyVisible(*message, restored));
            QVERIFY(UiInspection::labelFits(*message));
            for (auto *button : message->parentWidget()->findChildren<QPushButton *>())
                QVERIFY(UiInspection::fullyVisible(*button, restored));
            QVERIFY(UiInspection::writeReport(
                restored, QString("workspace-unavailable-%1-%2").arg(language).arg(size.width())));
        }
        restored.findChild<QAction *>("newWindowAction")->trigger();
        QPointer<MainWindow> copy;
        const auto closeCopy = qScopeGuard([&] {
            if (copy)
                copy->close();
        });
        QTRY_VERIFY(windows().size() == 2);
        for (auto *window : windows())
            if (window != &restored)
                copy = window;
        QVERIFY(copy);
        QTRY_COMPARE(QDir::cleanPath(copy->findChild<QLineEdit *>("archivePath")->text()), gone);
        QTRY_VERIFY(active(*copy) && !active(*copy)->isEnabled());
        restored.openPath(usable);
        QTRY_VERIFY(active(restored)->isEnabled());
        QCOMPARE(location(restored), usable);
        QVERIFY(restored.findChild<QAction *>("createFolderAction")->isEnabled());
        QVERIFY(!restored.findChild<QLabel *>("selectedItemsStatus")->text().isEmpty());
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspaceSessionTests)
#include "workspace_session_tests.moc"
