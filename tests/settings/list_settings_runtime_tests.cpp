#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

#include <QCheckBox>
#include <QClipboard>
#include <QMouseEvent>
#include <QScrollBar>
#include <QThread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
struct CurrentDirectoryGuard {
    const QString previous = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(previous); }
};

bool writeFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("file") == 4;
}

bool setListOptions(MainWindow &window, QAction *settingsAction, bool enabled,
                    const char *name = "showDotsOption") {
    const int outerLoop = QThread::currentThread()->loopLevel();
    QTimer finishOptions;
    bool applied = false;
    QObject::connect(&finishOptions, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("settingsDialog");
        // Native page discovery can process events during construction. Wait
        // until the owned dialog is visible and its modal loop is running.
        if (!dialog || !dialog->isVisible() || QThread::currentThread()->loopLevel() <= outerLoop)
            return;
        finishOptions.stop();
        const auto closeOnFailure = qScopeGuard([dialog] {
            if (dialog->isVisible())
                dialog->reject();
        });
        auto *option = dialog->findChild<QCheckBox *>(name);
        if (!option)
            return;
        option->setChecked(enabled);
        dialog->accept();
        applied = dialog->result() == QDialog::Accepted;
    });
    finishOptions.start(5);
    settingsAction->trigger();
    return applied;
}

QString screenshotPath(const QString &name) {
    const QDir report(qEnvironmentVariable("SEVENZIP_UI_REPORT_DIR"));
    const QString folder = report.filePath(QStringLiteral("../ui-screenshots"));
    QDir().mkpath(folder);
    return QDir(folder).filePath(name);
}
} // namespace

class ListSettingsRuntimeTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void hiddenFoldersCanBeToggled() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString hidden = fixture.filePath(".private");
        QVERIFY(QDir().mkdir(hidden));
#ifdef Q_OS_WIN
        QVERIFY(
            SetFileAttributesW(reinterpret_cast<LPCWSTR>(hidden.utf16()), FILE_ATTRIBUTE_HIDDEN));
#endif
        QVERIFY(writeFile(QDir(hidden).filePath("child.txt")));
        QVERIFY(writeFile(fixture.filePath("visible.txt")));
        QtWorkspaceApplication application(*qApp);
        struct BusyBackend final : ArchiveBackend {
            bool held = false;
            bool busy() const override { return held || ArchiveBackend::busy(); }
        } backend;
        MainWindow observer(&backend);
        observer.show();
        observer.openPath(fixture.path());
        QTRY_VERIFY(filesystemRow(observer, ".private") >= 0);
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        QTRY_VERIFY(filesystemRow(window, ".private") >= 0);
        auto *twoPanels = action(window, "twoPanelsAction");
        QVERIFY(!twoPanels->isChecked());
        twoPanels->trigger();
        auto *peer =
            window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(peer->isVisible() && peer->isEnabled());
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());
        window.openPath(fixture.path());
        QTRY_COMPARE(peer->rowCount(), 2);
        auto *left =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        left->setFocus();
        QTRY_VERIFY(left->hasFocus() && twoPanels->isEnabled());
        twoPanels->trigger();
        QTRY_VERIFY(!peer->isVisible());
        auto *settings = action(window, "settingsAction");
        backend.held = true;
        QVERIFY(setListOptions(window, settings, false, "showHiddenFoldersOption"));
        QTRY_COMPARE(left->rowCount(), 1);
        QVERIFY(filesystemRow(observer, ".private") >= 0);
        QVERIFY(WindowActivation::waitForActive(window));
        backend.held = false;
        QTRY_COMPARE(filesystemRow(observer, ".private"), -1);
        QVERIFY(window.isActiveWindow());
        QCOMPARE(left->model()->index(0, 0).data().toString(), QString("visible.txt"));
        QTRY_VERIFY(left->isEnabled());
        left->setFocus();
        QTRY_VERIFY(left->hasFocus());
        action(window, "selectAllAction")->trigger();
        auto *summary = window.findChild<QLabel *>("selectedItemsStatus");
        QVERIFY(summary);
        QTRY_COMPARE(summary->text(), QString("Selected: 1 / 1"));
        auto *copy = action(window, "copyFilesAction");
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(),
                 QList<QUrl>{QUrl::fromLocalFile(fixture.filePath("visible.txt"))});
        QApplication::clipboard()->clear();
        QTRY_VERIFY(twoPanels->isEnabled());
        twoPanels->trigger();
        QTRY_VERIFY(peer->isVisible() && peer->isEnabled());
        QCOMPARE(peer->rowCount(), 1);
        QCOMPARE(peer->model()->index(0, 0).data().toString(), QString("visible.txt"));
        left->setFocus();
        QTRY_VERIFY(left->hasFocus());
        window.openPath(hidden);
        QTRY_COMPARE(left->model()->index(0, 0).data().toString(), QString("child.txt"));
        window.openPath(fixture.path());
        QTRY_COMPARE(left->model()->index(0, 0).data().toString(), QString("visible.txt"));
        QCOMPARE(left->rowCount(), 1);
        QVERIFY(setListOptions(window, settings, true, "showHiddenFoldersOption"));
        QTRY_COMPARE(left->rowCount(), 2);
        QTRY_VERIFY(filesystemRow(observer, ".private") >= 0);
        QTRY_COMPARE(peer->rowCount(), 2);
    }

    void optionsParentRowAndContextMenu() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QVERIFY(writeFile(temp.filePath(QStringLiteral("child.txt"))));
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *files = static_cast<FileTable *>(table(window));
        QCOMPARE(files->rowCount(), 1);

        QVERIFY(setListOptions(window, action(window, "settingsAction"), true));
        QCOMPARE(files->rowCount(), 2);
        QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        auto *summary = window.findChild<QLabel *>("selectedItemsStatus");
        QVERIFY(summary);
        action(window, "selectAllAction")->trigger();
        QTRY_COMPARE(summary->text(), QString("Selected: 1 / 1"));
        QCOMPARE(files->model()->index(0, 0).data(Qt::ToolTipRole).toString(),
                 QStringLiteral("Parent folder"));
        QCOMPARE(files->accessibleName(), QStringLiteral("File list"));
        const QPersistentModelIndex parent = files->model()->index(0, 0);
        const QString languageSettings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString("ru"), languageSettings);
        ReloadLang(languageSettings);
        QtNotifyLanguageChanged();
        QCOMPARE(files->model()->index(0, 0), parent);
        QCOMPARE(files->rowCount(), 2);
        QCOMPARE(parent.data(Qt::ToolTipRole).toString(), QStringLiteral("Родительская папка"));
        QCOMPARE(files->accessibleName(), QStringLiteral("Список файлов"));
        SaveRegLang(UString("-"), languageSettings);
        ReloadLang(languageSettings);
        QtNotifyLanguageChanged();
        QCOMPARE(parent.data(Qt::ToolTipRole).toString(), QString("Parent folder"));
        QCOMPARE(files->accessibleName(), QString("File list"));
        QCOMPARE(files->model()->index(1, 0).data(Qt::ToolTipRole).toString().section('\n', 0, 0),
                 QFileInfo(temp.filePath(QStringLiteral("child.txt"))).canonicalFilePath());
        files->sortItems(0, Qt::DescendingOrder);
        QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        files->sortItems(0, Qt::AscendingOrder);

        files->selectRow(1);
        if (qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1)
            QVERIFY(files->viewport()->grab().save(
                screenshotPath(QStringLiteral("list-settings-full-row.png"))));
        const int outerLoopLevel = QThread::currentThread()->loopLevel();
        bool menuInspected = false;
        QTimer inspectMenu;
        connect(&inspectMenu, &QTimer::timeout, &window, [&] {
            auto *menu = window.findChild<QMenu *>("panelContextMenu");
            // Native popup creation can dispatch timers before QMenu::exec
            // enters its nested loop. Closing then cannot finish that loop.
            if (!menu || !menu->isVisible() ||
                QThread::currentThread()->loopLevel() <= outerLoopLevel)
                return;
            inspectMenu.stop();
            menuInspected = true;
            const auto close = qScopeGuard([&] { menu->close(); });
            bool found = false;
            const auto inspect = [&](auto &&self, QMenu &current) -> void {
                for (QAction *item : current.actions()) {
                    found |= item->objectName() == QStringLiteral("systemContextGroup") &&
                             item->isEnabled() && item->menu() &&
                             !item->menu()->actions().isEmpty();
                    if (item->menu())
                        self(self, *item->menu());
                }
            };
            inspect(inspect, *menu);
#ifdef Q_OS_WIN
            QVERIFY(found);
#else
            QVERIFY(!found);
#endif
            menu->close();
        });
        inspectMenu.start(5);
        QVERIFY(QMetaObject::invokeMethod(
            files, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, files->visualRect(files->model()->index(1, 0)).center())));
        QVERIFY(menuInspected);
        QVERIFY(action(window, "settingsAction")->isEnabled());

        MainWindow reopened;
        QCOMPARE(table(reopened)->rowCount(), 2);
        reopened.show();
        for (int width : {980, 640}) {
            reopened.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*table(reopened), reopened));
            QVERIFY(
                UiInspection::writeReport(reopened, QStringLiteral("list-settings-%1").arg(width)));
        }
        reopened.close();
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QVERIFY(setListOptions(window, action(window, "settingsAction"), false));
        QCOMPARE(files->rowCount(), 1);
    }

    void parentItemInArchiveAndVirtualRoot() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/ShowDots", true);
        settings.sync();
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, temp.filePath("sample.zip"),
                          {ArchiveEntry{QStringLiteral("папка/имя.txt"), 4, 4}});
        QTRY_VERIFY_WITH_TIMEOUT(table(window)->rowCount() >= 2, 1500);
        for (int mode : {0, 3}) {
            action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
            QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        }
        openRow(window, 0);
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLineEdit *>("archivePath")->text(),
                                  QFileInfo(temp.path()).canonicalFilePath(), 1500);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        auto *root = action(window, "openRootAction");
        QTRY_VERIFY(root->isEnabled());
        root->trigger();
        QTRY_VERIFY(table(window)->isEnabled());
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        auto *parent = action(window, "parentFolderAction");
        QTRY_VERIFY(parent->isEnabled());
        parent->trigger();
        QTRY_VERIFY(table(window)->isEnabled() && filesystemRow(window, QStringLiteral("..")) < 0);
    }
};

QTEST_MAIN(ListSettingsRuntimeTests)
#include "list_settings_runtime_tests.moc"
