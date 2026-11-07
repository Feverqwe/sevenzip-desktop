#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

#include <QCheckBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QThread>

namespace {
struct CurrentDirectoryGuard {
    const QString previous = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(previous); }
};

bool writeFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("file") == 4;
}

bool setListOptions(MainWindow &window, QAction *settingsAction, bool enabled) {
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
        for (const char *name : {"showDotsOption", "fullRowOption", "showSystemMenuOption"}) {
            auto *option = dialog->findChild<QCheckBox *>(name);
            if (!option)
                return;
            option->setChecked(enabled);
        }
        dialog->accept();
        applied = dialog->result() == QDialog::Accepted;
    });
    finishOptions.start(5);
    settingsAction->trigger();
    return applied;
}

void moveOver(QAbstractItemView &view, const QModelIndex &index) {
    auto *window = view.window();
    QTest::mouseMove(window->windowHandle(),
                     view.viewport()->mapTo(window, QPoint(1, view.viewport()->height() - 2)));
    QTest::mouseMove(window->windowHandle(),
                     view.viewport()->mapTo(window, view.visualRect(index).center()));
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
        const QRect selectedSizeCell = files->visualRect(files->model()->index(1, 2));
        const QPoint selectedPixel(selectedSizeCell.left() + 5, selectedSizeCell.center().y());
        const QColor fullRowColor = files->viewport()->grab().toImage().pixelColor(selectedPixel);
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
#ifdef Q_OS_WIN
                    found |= item->objectName() == QStringLiteral("systemContextGroup") &&
                             item->isEnabled() && item->menu() &&
                             !item->menu()->actions().isEmpty();
#else
                found |= item->objectName() == QStringLiteral("systemContextGroup") &&
                         !item->isEnabled();
#endif
                    if (item->menu())
                        self(self, *item->menu());
                }
            };
            inspect(inspect, *menu);
            QVERIFY(found);
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
        files->selectRow(0);
        const QRect nameOnlySizeCell = files->visualRect(files->model()->index(0, 2));
        const QPoint nameOnlyPixel(nameOnlySizeCell.left() + 5, nameOnlySizeCell.center().y());
        const QColor nameOnlyColor = files->viewport()->grab().toImage().pixelColor(nameOnlyPixel);
        QVERIFY(fullRowColor != nameOnlyColor);
        if (qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1)
            QVERIFY(files->viewport()->grab().save(
                screenshotPath(QStringLiteral("list-settings-name-only.png"))));
    }

    void hoverSelectsWithoutOpeningAndIgnoresScroll() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        for (int i = 0; i < 60; ++i)
            QVERIFY(
                writeFile(temp.filePath(QStringLiteral("file-%1.txt").arg(i, 3, 10, QChar('0')))));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/SingleClick", true);
        settings.sync();
        MainWindow window;
        window.resize(640, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *files = table(window);
        auto *icons = window.findChild<QListView *>("iconList");
        QCOMPARE(icons->accessibleName(), QStringLiteral("File list"));
        const QString path = window.findChild<QLineEdit *>("archivePath")->text();
        for (int mode : {3, 0}) {
            action(window, mode == 3 ? "viewMode3Action" : "viewMode0Action")->trigger();
            QAbstractItemView *view = mode == 3 ? static_cast<QAbstractItemView *>(files)
                                                : static_cast<QAbstractItemView *>(icons);
            const QModelIndex target = files->model()->index(4, 0);
            view->scrollTo(target);
            moveOver(*view, target);
            QTRY_COMPARE_WITH_TIMEOUT(files->currentRow(), 4, 1200);
            QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), path);
            const QModelIndex delayed = files->model()->index(12, 0);
            view->scrollTo(delayed);
            moveOver(*view, delayed);
            view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
            QTest::qWait(550);
            QCOMPARE(files->currentRow(), 4);

            for (const auto gesture : {Qt::ControlModifier, Qt::ShiftModifier}) {
                view->scrollTo(delayed);
                const QPoint position = view->visualRect(delayed).center();
                QMouseEvent move(QEvent::MouseMove, QPointF(position),
                                 QPointF(view->viewport()->mapToGlobal(position)), Qt::NoButton,
                                 Qt::NoButton, gesture);
                QApplication::sendEvent(view->viewport(), &move);
                QTest::qWait(450);
                QCOMPARE(files->currentRow(), 4);
            }
            view->scrollTo(delayed);
            const QPoint dragPosition = view->visualRect(delayed).center();
            QMouseEvent dragging(QEvent::MouseMove, QPointF(dragPosition),
                                 QPointF(view->viewport()->mapToGlobal(dragPosition)), Qt::NoButton,
                                 Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &dragging);
            files->setCurrentCell(4, 0);
            QTest::qWait(450);
            QCOMPARE(files->currentRow(), 4);
            view->scrollTo(delayed);
            moveOver(*view, delayed);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(view->viewport(), &leave);
            QTest::qWait(450);
            QCOMPARE(files->currentRow(), 4);
        }

        const QString focusedName = files->model()->index(files->currentRow(), 0).data().toString();
        const QModelIndex removed = files->model()->index(12, 0);
        icons->scrollTo(removed);
        moveOver(*icons, removed);
        QVERIFY(QFile::remove(files->model()->index(12, 1).data().toString()));
        action(window, "refreshAction")->trigger();
        QCOMPARE(files->rowCount(), 59);
        QTest::qWait(500);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(), focusedName);
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
