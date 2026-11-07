#include "7zip/UI/FileManager/resource.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtstatuslabel.h"
#include "ui/support/layout_inspection.h"

#include <QMenuBar>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QSizeGrip>
#include <QSplitterHandle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWindow>

class MainShellTests final : public QObject {
    Q_OBJECT

  private slots:
    void fileMenuRetiresProviderCommandsDuringSourceReplacement() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        const QString archive = fixture.filePath("pending archive.zip");
        QVERIFY(backend.prepareArchiveFixture(archive));
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(files->isEnabled() && files->rowCount() > 0);
        files->selectRow(0);
        auto *file = window.findChild<QMenu *>("fileMenu");
        const std::function<int(QMenu &)> providers = [&](QMenu &menu) {
            int count = 0;
            for (auto *action : menu.actions()) {
                count += action->property("shellCommandID").isValid();
                if (action->menu())
                    count += providers(*action->menu());
            }
            return count;
        };
        file->popup(window.mapToGlobal(QPoint(20, 40)));
        QApplication::processEvents();
        QVERIFY(providers(*file) > 0);
        file->hide();
        bool guarded = false, retired = false;
        backend.queueListAction([&] {
            guarded = !files->sourcePanel->ShouldProcessNotifications();
            file->popup(window.mapToGlobal(QPoint(20, 40)));
            QApplication::processEvents();
            retired = providers(*file) == 0;
            file->hide();
            backend.completeList({ArchiveEntry{QStringLiteral("new.txt"), 1, 1}});
        });
        window.openArchive(archive);
        QVERIFY(guarded);
        QVERIFY(retired);
        QTRY_VERIFY(files->isEnabled());
        QCOMPARE(files->rowCount(), 1);
        file->popup(window.mapToGlobal(QPoint(20, 40)));
        QApplication::processEvents();
        bool openAsReturned = false;
        for (auto *action : file->actions())
            openAsReturned |=
                action->isVisible() && action->menu() &&
                action->text() == QCoreApplication::translate("CommandMenus", "Open as");
        QVERIFY(openAsReturned);
        file->hide();
    }
    void commandIconsFollowPaletteAndScale() {
        const auto original = QApplication::palette();
        const auto restore = qScopeGuard([&] { QApplication::setPalette(original); });
        MainWindow window;
        const char *commands[] = {"toolbarAddAction", "extractArchiveAction", "testArchiveAction",
                                  "copyAction",       "moveAction",           "deleteAction",
                                  "archiveInfoAction"};
        for (const QColor color : {QColor("#23485a"), QColor("#e4d9ca")}) {
            auto palette = original;
            palette.setColor(QPalette::Active, QPalette::ButtonText, color);
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#83919c"));
            QApplication::setPalette(palette);
            for (const char *command : commands)
                for (const auto mode : {QIcon::Normal, QIcon::Disabled})
                    for (const qreal dpr : {1.0, 2.0}) {
                        const QIcon icon = window.findChild<QAction *>(command)->icon();
                        const auto pixmap = icon.pixmap(QSize(24, 24), dpr, mode);
                        QCOMPARE(pixmap.deviceIndependentSize(), QSizeF(24, 24));
                        const auto image = pixmap.toImage();
                        const QColor expected =
                            mode == QIcon::Disabled
                                ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                                : color;
                        QRect ink;
                        for (int y = 0; y < image.height(); ++y)
                            for (int x = 0; x < image.width(); ++x) {
                                const auto pixel = image.pixelColor(x, y);
                                if (pixel.alpha() < 128)
                                    continue;
                                ink |= QRect(x, y, 1, 1);
                                QVERIFY(qAbs(pixel.red() - expected.red()) <= 1);
                                QVERIFY(qAbs(pixel.green() - expected.green()) <= 1);
                                QVERIFY(qAbs(pixel.blue() - expected.blue()) <= 1);
                            }
                        QVERIFY(ink.width() >= 10 * dpr);
                        QVERIFY(ink.height() >= 6 * dpr);
                        QVERIFY(image.rect().adjusted(1, 1, -1, -1).contains(ink));
                    }
        }
    }
    void elidedStatusUsesInactiveAndDisabledColors() {
        QWidget window;
        window.resize(240, 120);
        QtStatusLabel label(&window);
        label.setText(QString(30, QLatin1Char('M')));
        QFont font = label.font();
        font.setPointSize(24);
        label.setFont(font);
        label.setGeometry(10, 10, 100, 50);
        QPalette colors = label.palette();
        colors.setColor(QPalette::Window, Qt::white);
        colors.setColor(QPalette::Active, QPalette::WindowText, QColor(170, 40, 40));
        colors.setColor(QPalette::Inactive, QPalette::WindowText, QColor(40, 50, 170));
        colors.setColor(QPalette::Disabled, QPalette::WindowText, QColor(40, 140, 50));
        label.setPalette(colors);
        label.setAutoFillBackground(true);
        window.show();
        QWidget other;
        other.show();
        other.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QVERIFY(!window.isActiveWindow());
        for (bool enabled : {true, false}) {
            label.setEnabled(enabled);
            const QImage image = label.grab().toImage();
            bool expectedText = false;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    expectedText |=
                        enabled
                            ? pixel.blue() > pixel.red() + 50 && pixel.blue() > pixel.green() + 50
                            : pixel.green() > pixel.red() + 50 && pixel.green() > pixel.blue() + 50;
                }
            QVERIFY(expectedText);
        }
    }
    void fileManagerWindowUsesSourceApplicationIcon() {
        ControlledBackend backend;
        MainWindow window(&backend);
        const QIcon original(QStringLiteral(":/upstream/7zip/UI/FileManager/FM.ico"));
        QVERIFY(!original.isNull());
        QVERIFY(!window.windowIcon().isNull());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.windowHandle());
        for (const QSize &size : original.availableSizes())
            QCOMPARE(window.windowHandle()->icon().pixmap(size).toImage(),
                     original.pixmap(size).toImage());
    }

    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void sourcePanelChrome_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(980, 620), QSize(640, 400)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }

    void sourcePanelChrome() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const UString oldLanguage = g_LangID;
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(oldLanguage, settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
        });
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const QString left = fixture.filePath(QStringLiteral("левый каталог 空"));
        const QString right = fixture.filePath("right folder");
        QVERIFY(QDir().mkpath(left) && QDir().mkpath(right));
        SavePanelPath(0, ToUString(left), settings);
        SavePanelPath(1, ToUString(right), settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.findChild<QAction *>("backAction"));
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QVERIFY(twoPanels);
        for (int phase = 0; phase < 3; ++phase) {
            if (twoPanels->isChecked() != (phase == 1))
                twoPanels->trigger();
            QApplication::processEvents();
            QStringList differences;
            if (!window.statusBar()->isVisible() || window.findChildren<QStatusBar *>().size() != 1)
                differences << "missing or duplicate shared status bar";
            for (unsigned index = 0; index < (phase == 1 ? 2u : 1u); ++index) {
                auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
                QVERIFY(panel);
                auto *parent =
                    panel->findChild<QAbstractButton *>(QString("panelParentButton%1").arg(index));
                QVERIFY(parent && UiInspection::fullyVisible(*parent, window));
                QVERIFY(UiInspection::fullyVisible(*window.statusBar(), window));
                auto *navigation =
                    panel->findChild<QWidget *>(QString("panelNavigation%1").arg(index));
                QVERIFY(navigation && UiInspection::fullyVisible(*navigation, window));
            }
            QVERIFY(UiInspection::writeReport(window, QString("source-panel-chrome-%1-%2-%3")
                                                          .arg(language)
                                                          .arg(size.width())
                                                          .arg(phase)));
            QVERIFY2(differences.isEmpty(), qPrintable(differences.join(", ")));
        }
        window.statusBar()->showMessage(QStringLiteral("operation проверка 空"));
        QApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*window.statusBar(), window));
        window.statusBar()->clearMessage();
        QApplication::processEvents();
        QVERIFY(window.statusBar()->isVisible());
    }

    void statusBarsKeepNativeNoGripStyle_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const auto &language : {QStringLiteral("en"), QStringLiteral("ru")})
            for (const QSize size : {QSize(980, 620), QSize(640, 400)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }

    void statusBarsKeepNativeNoGripStyle() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(UString(), settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
        });
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QVERIFY(twoPanels);
        const QString message = QStringLiteral("status проверка 空");
        for (int phase = 0; phase < 3; ++phase) {
            if (twoPanels->isChecked() != (phase == 1))
                twoPanels->trigger();
            window.statusBar()->showMessage(message);
            QApplication::processEvents();
            QCOMPARE(window.statusBar()->currentMessage(), message);
            QVERIFY(UiInspection::fullyVisible(*window.statusBar(), window));
            for (auto *bar : window.findChildren<QStatusBar *>())
                QVERIFY2(!bar->isSizeGripEnabled(), "7zFM status bars have no resize grip");
            for (auto *grip : window.findChildren<QSizeGrip *>())
                QVERIFY(!grip->isVisible());
            QVERIFY(UiInspection::writeReport(window, QStringLiteral("status-no-grip-%1-%2-%3")
                                                          .arg(language)
                                                          .arg(size.width())
                                                          .arg(phase)));
        }
    }

    void nativeTemporaryFilesCommandUsesActualModalConsumer() {
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *action = window.findChild<QAction *>("tempDirAction");
        QVERIFY(action && action->isEnabled());
        bool observed = false;
        QTimer close;
        connect(&close, &QTimer::timeout, &window, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (dialog && dialog->parentWidget() == &window) {
                observed = dialog->findChild<QTreeWidget *>("tempList") != nullptr;
                close.stop();
                QTest::keyClick(dialog, Qt::Key_Escape);
            }
        });
        close.start(5);
        action->trigger();
        QVERIFY(observed);
        QVERIFY(!window.findChild<QDialog *>("tempBrowseDialog"));
        QVERIFY(!backend.busy());
    }

    void sourceCommandsKeepAvailabilityAndShortcuts() {
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *link = window.findChild<QAction *>("linkAction");
        QVERIFY(link && !link->isEnabled());
        auto *streams = window.findChild<QAction *>("altStreamsAction");
        QVERIFY(streams);
#ifdef Q_OS_WIN
        QVERIFY(streams->isEnabled()); // Source allows opening the current folder's ADS.
#else
        QVERIFY(!streams->isEnabled());
#endif
        QVERIFY(window.findChild<QAction *>("aboutAction")->isEnabled());
        QVERIFY(window.findChild<QAction *>("tempDirAction")->isEnabled());
        QCOMPARE(window.findChild<QAction *>("openInsideAction")->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::Key_PageDown));
        QCOMPARE(window.findChild<QAction *>("viewFileAction")->shortcut(),
                 QKeySequence(Qt::Key_F3));
        QCOMPARE(window.findChild<QAction *>("editFileAction")->shortcut(),
                 QKeySequence(Qt::Key_F4));
    }

    void toolbarCommandsFitBothSizes_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }

    void toolbarCommandsFitBothSizes() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const UString oldLanguage = g_LangID;
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(oldLanguage, settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
        });
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QSettings profile(settings, QSettings::IniFormat);
        const auto previousMask = profile.value("Toolbars");
        profile.setValue("Toolbars", 2u);
        profile.sync();
        const auto restoreMask = qScopeGuard([&] {
            if (previousMask.isValid())
                profile.setValue("Toolbars", previousMask);
            else
                profile.remove("Toolbars");
            profile.sync();
        });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString folder =
            fixture.filePath("Long folder name for navigation and compact layout");
        QVERIFY(QDir().mkpath(folder));
        for (const QString &name : {QStringLiteral("Документы"), QStringLiteral("Photos"),
                                    QStringLiteral("Projects"), QStringLiteral("Downloads")})
            QVERIFY(QDir().mkpath(QDir(folder).filePath(name)));
        for (const QString &name : {QStringLiteral("Annual report.txt"), QStringLiteral("Notes.md"),
                                    QStringLiteral("План проекта.txt")}) {
            QFile file(QDir(folder).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("local review fixture\n") > 0);
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(folder);
        QTRY_VERIFY(window.findChild<FileTable *>("archiveTable")->isVisible());
        QTRY_COMPARE(QDir::cleanPath(window.findChild<QLineEdit *>("archivePath")->text()), folder);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            for (const bool two : {false, true, false}) {
                auto *toggle = window.findChild<QAction *>("twoPanelsAction");
                if (toggle->isChecked() != two)
                    toggle->trigger();
                QTRY_VERIFY(toggle->isEnabled());
                QTRY_VERIFY(window.findChild<FileTable *>("archiveTable")->isEnabled());
                QApplication::processEvents();
                for (unsigned panel = 0; panel < (two ? 2u : 1u); ++panel) {
                    auto *toolbar =
                        window.findChild<QToolBar *>(QString("panelFileActions%1").arg(panel));
                    QVERIFY(toolbar && toolbar->isVisible());
                    QVERIFY(UiInspection::fullyVisible(*toolbar, window));
                    for (auto *action : toolbar->actions()) {
                        if (action->isSeparator() || !action->isVisible())
                            continue;
                        auto *button =
                            qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
                        if (!button)
                            continue;
                        QVERIFY(!button->accessibleName().isEmpty());
                        QVERIFY(UiInspection::fullyVisible(*button, window));
                    }
                }
                for (auto *stack : window.findChildren<QStackedWidget *>("fileViewStack"))
                    if (stack->isVisible())
                        QVERIFY(UiInspection::fullyVisible(*stack->currentWidget(), window));
                for (unsigned panel = 0; panel < (two ? 2u : 1u); ++panel) {
                    auto *navigation =
                        window.findChild<QWidget *>(QString("panelNavigation%1").arg(panel));
                    QVERIFY(navigation);
                    QVERIFY(UiInspection::fullyVisible(*navigation, window));
                    if (panel == 0) {
                        bool currentCrumbVisible = false;
                        for (auto *button : navigation->findChildren<QToolButton *>())
                            if (QDir::cleanPath(button->toolTip()) == folder)
                                currentCrumbVisible |= UiInspection::fullyVisible(*button, window);
                        QVERIFY2(currentCrumbVisible,
                                 "The current folder must remain visible after pane resize");
                    }
                }
                if (two) {
                    auto *splitter = window.findChild<QSplitter *>("filePanelSplitter");
                    splitter->handle(1)->setFocus();
                    QTest::keyClick(splitter->handle(1), Qt::Key_Right, Qt::ShiftModifier);
                    QSignalSpy renamed(window.findChild<QAction *>("renameAction"),
                                       &QAction::triggered);
                    QTest::keyClick(splitter->handle(1), Qt::Key_Return);
                    QCOMPARE(renamed.size(), 0);
                    QApplication::processEvents();
                    auto *rightPanel = splitter->widget(1);
                    auto *rightStack = rightPanel->findChild<QStackedWidget *>("fileViewStack");
                    QCOMPARE(rightStack->width(), rightPanel->width());
                    auto *leftPanel = splitter->widget(0);
                    auto *leftStack = leftPanel->findChild<QStackedWidget *>("fileViewStack");
                    QVERIFY2(
                        qAbs(leftStack->width() - rightStack->width()) <= 1,
                        qPrintable(
                            QString(
                                "Reset file widths: left %1, right %2, window %3; splitter %4/%5")
                                .arg(leftStack->width())
                                .arg(rightStack->width())
                                .arg(window.width())
                                .arg(splitter->sizes().value(0))
                                .arg(splitter->sizes().value(1))));
                }
                QVERIFY(UiInspection::writeReport(window, QString("workspace-shell-%1-%2-%3")
                                                              .arg(language)
                                                              .arg(size.width())
                                                              .arg(two ? "two" : "one")));
            }
            for (const char *name : {"fileMenu", "editMenu", "viewMenu"}) {
                auto *menu = window.findChild<QMenu *>(name);
                menu->popup(window.menuBar()->mapToGlobal(QPoint(0, window.menuBar()->height())));
                QApplication::processEvents();
                QVERIFY(menu->isVisible());
                QVERIFY(UiInspection::writeReport(
                    *menu,
                    QString("workspace-%1-%2-%3").arg(name).arg(language).arg(size.width())));
                menu->hide();
            }
        }
    }

    void fileMenuTracksTheFocusedPanelAfterToggleAndTab() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("left");
        const QString right = fixture.filePath("right with space");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        QFile file(QDir(right).filePath(QStringLiteral("имя.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        SavePanelPath(1, ToUString(right), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *leftPanel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(leftPanel);
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        auto *leftAddress = leftPanel->findChild<QLineEdit *>("archivePath");
        leftAddress->setText(left);
        QTest::keyClick(leftAddress, Qt::Key_Return);
        QTRY_COMPARE(QFileInfo(leftAddress->text()).canonicalFilePath(),
                     QFileInfo(left).canonicalFilePath());
        auto *rename = window.findChild<QAction *>("renameAction");
        QVERIFY(rename);
        QVERIFY(!rename->isEnabled());
        window.findChild<QAction *>("twoPanelsAction")->trigger();
        auto *rightPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(rightPanel && rightPanel->isVisible());
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(rightTable->rowCount() > 0);
        for (int row = 0; row < rightTable->rowCount(); ++row) {
            if (rightTable->model()->index(row, 0).data().toString() == QStringLiteral("имя.txt")) {
                rightTable->selectRow(row);
                rightTable->setCurrentCell(row, 0);
                break;
            }
        }
        rightTable->setFocus();
        QTRY_VERIFY(rename->isEnabled());
        QVERIFY(window.findChild<QMenu *>("fileMenu")->actions().contains(rename));
        auto *copy = window.findChild<QAction *>("copyAction");
        auto *fileMenu = window.findChild<QMenu *>("fileMenu");
        QVERIFY(copy && copy->isEnabled() && fileMenu->actions().contains(copy));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        fileMenu->popup(window.menuBar()->mapToGlobal(QPoint(0, window.menuBar()->height())));
        QTRY_VERIFY(fileMenu->isVisible());
        fileMenu->setActiveAction(copy);
        QSignalSpy invoked(copy, &QAction::triggered);
        bool correctSource = false, correctDestination = false;
        QTimer cancel;
        connect(&cancel, &QTimer::timeout, &window, [&] {
            if (auto *dialog = dynamic_cast<CCopyDialog *>(QApplication::activeModalWidget())) {
                correctSource = ToQString(dialog->Info).contains(QStringLiteral("имя.txt"));
                correctDestination = QFileInfo(dialog->destination()).canonicalFilePath() ==
                                     QFileInfo(left).canonicalFilePath();
                cancel.stop();
                dialog->reject();
            }
        });
        cancel.start(5);
        QTest::keyClick(fileMenu, Qt::Key_Return);
        QCOMPARE(invoked.size(), 1);
        QVERIFY(correctSource && correctDestination);
        QVERIFY(QFileInfo::exists(file.fileName()));
        QVERIFY(!QFileInfo::exists(QDir(left).filePath(QStringLiteral("имя.txt"))));
        QTRY_VERIFY(rightTable->isEnabled());
        QApplication::processEvents();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        rightTable->setFocus();
        QTRY_VERIFY(rightTable->hasFocus());
        QTest::keyClick(rightTable, Qt::Key_Tab);
        QTRY_VERIFY(leftTable->hasFocus());
        QVERIFY(!rename->isEnabled());
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(MainShellTests)
#include "main_shell_tests.moc"
