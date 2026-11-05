#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/LangUtils.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QCursor>
#include <QKeyEvent>
#include <QScopeGuard>
#include <QTabBar>
#include <QWindow>
#include <utility>

class DisabledObserver final : public QObject {
  public:
    std::function<void()> disabled;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::EnabledChange &&
            !static_cast<QWidget *>(object)->isEnabled() && disabled)
            std::exchange(disabled, {})();
        return false;
    }
};

class WorkspaceTabsTests final : public QObject {
    Q_OBJECT
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(object))
                return owner;
        return nullptr;
    }
    static QTabBar *populate(MainWindow &window, const QString &root) {
        auto *owner = workspace(window);
        if (!owner)
            return nullptr;
        for (int index = 0; index < 9; ++index) {
            const QString path = root + QString("/Project %1 — папка").arg(index);
            if (!QDir().mkpath(path))
                return nullptr;
            if (!index)
                window.openPath(path);
            else
                owner->NewTab(0, path);
        }
        return window.findChild<QTabBar *>("panelTabs0");
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void denseGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void denseGeometry() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *bar = populate(window, fixture.path());
        QVERIFY(bar);
        window.findChild<QAction *>("twoPanelsAction")->trigger();
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QTRY_VERIFY(bar->tabButton(bar->currentIndex(), QTabBar::RightSide)->isVisible());
            QApplication::processEvents();
            const QRect active = bar->tabRect(bar->currentIndex());
            QVERIFY(bar->rect().contains(active));
            auto *close = bar->tabButton(bar->currentIndex(), QTabBar::RightSide);
            QVERIFY(active.contains(close->geometry()));
            QVERIFY(active.width() - close->width() >= 16);
            bool compact = false;
            for (int index = 0; index < bar->count(); ++index) {
                QVERIFY(!bar->tabText(index).isEmpty() && !bar->tabToolTip(index).isEmpty());
                if (index != bar->currentIndex() && bar->tabRect(index).width() < 100) {
                    compact = true;
                    const auto *hit = QApplication::widgetAt(QCursor::pos());
                    const bool hovered = hit && (hit == bar || bar->isAncestorOf(hit)) &&
                                         bar->tabAt(bar->mapFromGlobal(QCursor::pos())) == index;
                    QCOMPARE(bar->tabButton(index, QTabBar::RightSide)->isVisible(), hovered);
                }
            }
            QVERIFY(compact);
            QVERIFY(UiInspection::writeReport(
                window, QString("workspace-tabs-%1-%2-dense").arg(language).arg(size.width())));
        }
    }
    void keyboardRetainsStripFocusAndMiddleClickClosesItsTarget() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 400);
        window.show();
        auto *bar = populate(window, fixture.path());
        QVERIFY(bar);
        QVERIFY(WindowActivation::waitForActive(window));
        for (int index = 0; index < 1024; ++index) {
            QFile file(fixture.filePath(QString("Project 1 — папка/entry-%1.txt").arg(index)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto saved = workspace(window)->Save();
        bool restored = false;
        workspace(window)->Restore(saved, [&] { restored = true; });
        QTRY_VERIFY(restored);
        bar = window.findChild<QTabBar *>("panelTabs0");
        QVERIFY(bar);
        bar->setFocus();
        const auto activeFolder = [&] {
            auto *table = window.findChild<QSplitter *>("filePanelSplitter")
                              ->widget(0)
                              ->findChild<FileTable *>("archiveTable");
            return QFileInfo(
                       QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix())))
                .fileName();
        };
        for (const auto [key, index] : {std::pair{Qt::Key_Home, 0},
                                        {Qt::Key_Right, 1},
                                        {Qt::Key_End, 8},
                                        {Qt::Key_Left, 7}}) {
            QTest::keyClick(bar, key);
            QTRY_COMPARE(bar->currentIndex(), index);
            QTRY_VERIFY2(bar->hasFocus(), qPrintable(QString("focus before tab %1").arg(index)));
            QTRY_COMPARE(activeFolder(), QString("Project %1 — папка").arg(index));
            QTRY_VERIFY(bar->isEnabled());
            QVERIFY(bar->isVisible());
            QTRY_VERIFY2(
                bar->hasFocus(),
                qPrintable(
                    QString("focus after tab %1, widget=%2")
                        .arg(index)
                        .arg(window.focusWidget() ? window.focusWidget()->objectName() : "none")));
            QTRY_VERIFY2(bar->rect().contains(bar->tabRect(index)),
                         qPrintable(QString("tab %1: x=%2 width=%3 in bar width=%4")
                                        .arg(index)
                                        .arg(bar->tabRect(index).x())
                                        .arg(bar->tabRect(index).width())
                                        .arg(bar->width())));
        }
        QTest::keyClick(bar, Qt::Key_Home);
        QTRY_COMPARE(bar->currentIndex(), 0);
        QTRY_COMPARE(activeFolder(), QStringLiteral("Project 0 — папка"));
        QTRY_VERIFY(bar->isEnabled());
        QTRY_VERIFY(bar->rect().contains(bar->tabRect(1)));
        const QString target = bar->tabData(1).toString();
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(1).center());
        QTRY_COMPARE(bar->count(), 8);
        for (int index = 0; index < bar->count(); ++index)
            QVERIFY(bar->tabData(index).toString() != target);
        QCOMPARE(bar->currentIndex(), 0);
    }
    void hoverTracksStationaryPointerAcrossLayout() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 400);
        window.show();
        auto *bar = populate(window, fixture.path());
        QVERIFY(bar);
        QVERIFY(WindowActivation::waitForActive(window));
        bar->setFocus();
        QTest::keyClick(bar, Qt::Key_Home);
        const auto activeFolder = [&] {
            auto *table = window.findChild<QSplitter *>("filePanelSplitter")
                              ->widget(0)
                              ->findChild<FileTable *>("archiveTable");
            return QFileInfo(
                       QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix())))
                .fileName();
        };
        QTRY_COMPARE(activeFolder(), QStringLiteral("Project 0 — папка"));
        QTRY_VERIFY(bar->isEnabled() && bar->isVisible());
        QTRY_VERIFY(bar->rect().contains(bar->tabRect(1)));
        const QPoint cursor = QCursor::pos();
        const auto restoreCursor = qScopeGuard([&] { QCursor::setPos(cursor); });
        const QPoint requested = bar->mapToGlobal(bar->tabRect(1).center());
        QCursor::setPos(requested);
        if (QGuiApplication::platformName() == QStringLiteral("cocoa") &&
            QCursor::pos() != requested) {
            QTest::qWait(50);
            if (QCursor::pos() != requested)
                QSKIP("Native cursor warp is unavailable; physical Cocoa hover is unverified");
        }
        QTest::mouseMove(window.windowHandle(), bar->mapTo(&window, bar->tabRect(1).center()));
        QTRY_VERIFY2(
            bar->tabButton(1, QTabBar::RightSide)->isVisible(),
            qPrintable(
                QString("cursor=%1,%2 local=%3,%4 hit=%5 active=%6")
                    .arg(QCursor::pos().x())
                    .arg(QCursor::pos().y())
                    .arg(bar->mapFromGlobal(QCursor::pos()).x())
                    .arg(bar->mapFromGlobal(QCursor::pos()).y())
                    .arg(QApplication::widgetAt(QCursor::pos())
                             ? QApplication::widgetAt(QCursor::pos())->metaObject()->className()
                             : "none")
                    .arg(window.isActiveWindow())));
        const QPoint stationary = QCursor::pos();
        QTest::keyClick(bar, Qt::Key_End);
        QTRY_COMPARE(activeFolder(), QStringLiteral("Project 8 — папка"));
        QTRY_VERIFY(bar->isEnabled() && bar->isVisible() && bar->hasFocus());
        QCOMPARE(QCursor::pos(), stationary);
        const auto hovered = [&] { return bar->tabAt(bar->mapFromGlobal(QCursor::pos())); };
        QTRY_VERIFY(hovered() >= 0);
        QTRY_VERIFY(bar->tabButton(hovered(), QTabBar::RightSide)->isVisible());
        for (int index = 0; index < bar->count(); ++index)
            if (index != hovered() && index != bar->currentIndex() &&
                bar->tabRect(index).width() < 100)
                QTRY_VERIFY(bar->tabButton(index, QTabBar::RightSide)->isHidden());
    }
    void deferredScanFocusHonorsNewPathCommand_data() {
        QTest::addColumn<bool>("newCommand");
        QTest::newRow("return-to-strip") << false;
        QTest::newRow("new-path-command") << true;
    }
    void deferredScanFocusHonorsNewPathCommand() {
        QFETCH(bool, newCommand);
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *bar = populate(window, fixture.path());
        QVERIFY(bar);
        for (int index = 0; index < 1024; ++index) {
            QFile file(fixture.filePath(QString("Project 1 — папка/entry-%1.txt").arg(index)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto saved = workspace(window)->Save();
        bool restored = false;
        workspace(window)->Restore(saved, [&] { restored = true; });
        QTRY_VERIFY(restored);
        QVERIFY(WindowActivation::waitForActive(window));
        bar->setFocus();
        QTest::keyClick(bar, Qt::Key_Home);
        const auto activeTable = [&] {
            return window.findChild<QSplitter *>("filePanelSplitter")
                ->widget(0)
                ->findChild<FileTable *>("archiveTable");
        };
        QTRY_COMPARE(
            QDir::cleanPath(ToQString(activeTable()->sourcePanel->GetCurrentFolderPrefix())),
            QFileInfo(fixture.filePath("Project 0 — папка")).canonicalFilePath());
        QTRY_VERIFY(bar->isEnabled() && bar->hasFocus());
        bool scanObserved = false, commandIssued = false;
        QPointer<QLineEdit> editor;
        DisabledObserver observer;
        observer.disabled = [&] {
            scanObserved = true;
            if (newCommand)
                QTimer::singleShot(0, &window, [&] {
                    // A consumed QAction shortcut need not deliver a KeyPress.
                    QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_L, Qt::ControlModifier);
                    QApplication::sendEvent(&window, &shortcut);
                    window.findChild<QAction *>("focusPathAction")->trigger();
                    editor = window.findChild<QSplitter *>("filePanelSplitter")
                                 ->widget(0)
                                 ->findChild<QLineEdit *>("archivePath");
                    QVERIFY(editor && editor->hasFocus());
                    editor->setText("draft location");
                    commandIssued = true;
                });
        };
        bar->parentWidget()->installEventFilter(&observer);
        QTest::keyClick(bar, Qt::Key_Right);
        QTRY_VERIFY(scanObserved);
        QTRY_VERIFY(bar->isEnabled() && activeTable()->rowCount() >= 1024);
        if (newCommand) {
            QTRY_VERIFY(commandIssued);
            // Observe the settled focus rather than an immediate pre-publication state.
            QTest::qWait(100);
            QVERIFY(editor && editor->hasFocus());
            QCOMPARE(editor->text(), QStringLiteral("draft location"));
            QVERIFY(!bar->hasFocus());
        } else {
            QTRY_VERIFY(bar->hasFocus());
        }
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspaceTabsTests)
#include "workspace_tabs_tests.moc"
