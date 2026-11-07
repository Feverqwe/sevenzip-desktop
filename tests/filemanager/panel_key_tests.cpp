#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QSplitterHandle>

extern bool g_CaseSensitive;

class PanelKeyTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static QAction *action(MainWindow &window, const char *name) {
        return window.findChild<QAction *>(name);
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void shiftSelectionDoesNotIncludeFollowingUnmodifiedMove_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("alternative");
        for (int mode : {0, 3})
            for (bool alternative : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(mode).arg(alternative)))
                    << mode << alternative;
    }
    void shiftSelectionDoesNotIncludeFollowingUnmodifiedMove() {
        QFETCH(int, mode);
        QFETCH(bool, alternative);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, fixture.filePath("marks.zip"),
                          {ArchiveEntry{"a.txt", 1, 1}, ArchiveEntry{"b.txt", 2, 2},
                           ArchiveEntry{"c.txt", 3, 3}});
        window.show();
        auto *view = window.findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(view->rowCount(), 3);
        auto *modeAction = action(window, qPrintable(QString("viewMode%1Action").arg(mode)));
        QTRY_VERIFY(modeAction->isEnabled());
        modeAction->trigger();
        auto *target = mode == 3 ? static_cast<QAbstractItemView *>(view)
                                 : window.findChild<QListView *>("iconList");
        target->setFocus();
        QTRY_VERIFY(target->hasFocus());
        view->setCurrentCell(0, 0);
        target->setCurrentIndex(view->model()->index(0, 0));
        view->selectRow(0);
        const auto arrow = mode <= 1 ? Qt::Key_Right : Qt::Key_Down;
        QKeyEvent shiftDown(QEvent::KeyPress, arrow, Qt::ShiftModifier);
        QKeyEvent down(QEvent::KeyPress, arrow, Qt::NoModifier);
        QApplication::sendEvent(target, &shiftDown);
        QCOMPARE(view->currentRow(), 1);
        if (!alternative)
            QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        QApplication::sendEvent(target, &down);
        QCOMPARE(view->currentRow(), 2);
        QApplication::processEvents();
        CPanel *panel = static_cast<FileTable *>(view)->sourcePanel;
        for (int row = 0; row < 3; ++row)
            QCOMPARE(
                panel->IsItemMarked(view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt()),
                alternative ? row < 2 : row == 2);
    }
    void archiveOpenDisablesBothPanelAddresses() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        SavePanelPath(1, ToUString(fixture.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 480);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *leftAddress = left->findChild<QComboBox *>("pathCombo");
        auto *rightAddress = right->findChild<QComboBox *>("pathCombo");
        auto *rightList = right->findChild<FileTable *>("archiveTable");
        rightList->setFocus();
        QTRY_VERIFY(rightList->hasFocus());
        const QString archive = fixture.filePath("focus archive.zip");
        QVERIFY(backend.prepareArchiveFixture(archive));
        bool leftDisabled = false;
        bool rightDisabled = false;
        bool blockedReport = false;
        backend.queueListAction([&] {
            leftDisabled = !leftAddress->isEnabled();
            rightDisabled = !rightAddress->isEnabled();
            blockedReport =
                UiInspection::writeReport(window, QStringLiteral("archive-open-address-blocked"));
            backend.completeList({ArchiveEntry{"entry.txt", 1, 1}});
        });
        window.openArchive(archive);
        QVERIFY(leftDisabled);
        QVERIFY(rightDisabled);
        QVERIFY(blockedReport);
        QVERIFY(leftAddress->isEnabled());
        QVERIFY(rightAddress->isEnabled());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *leftEdit = left->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(leftEdit);
        QTRY_VERIFY(leftEdit->hasFocus());
        QTRY_VERIFY(!action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(leftAddress), window));
        QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(rightAddress), window));
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("archive-open-address-ready")));
    }
    void viewSwitchRetainsPanelBindingAcrossFocusChange() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        SavePanelPath(1, ToUString(fixture.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *list = right->findChild<FileTable *>("archiveTable");
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        // A native QStackedWidget switch can synchronously focus another panel.
        auto *stack = right->findChild<QStackedWidget *>("fileViewStack");
        connect(stack, &QStackedWidget::currentChanged, &window,
                [address] { PanelKeys::focusAddress(address); });
        action(window, "viewMode0Action")->trigger();
        static_cast<FileTable *>(list)->sourcePanel->SetFocusToList();
        QTRY_VERIFY(right->findChild<QListView *>("iconList")->hasFocus());
    }
    void twoPanelDiskSwitchKeepsIndependentViews() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("левая папка");
        const QString right = fixture.filePath("правая папка");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        QVERIFY(QDir().mkpath(QDir(right).filePath("nested")));
        QFile leftFile(QDir(left).filePath("left.txt"));
        QVERIFY(leftFile.open(QIODevice::WriteOnly));
        leftFile.close();
        QFile rightFile(QDir(right).filePath("right.txt"));
        QVERIFY(rightFile.open(QIODevice::WriteOnly));
        rightFile.close();
        SavePanelPath(1, ToUString(right), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *leftPanel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(leftPanel);
        auto *leftAddress = leftPanel->findChild<QLineEdit *>("archivePath");
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        leftAddress->setText(left);
        QTest::keyClick(leftAddress, Qt::Key_Return);
        QTRY_VERIFY(leftTable->rowCount() > 0);
        const int leftRows = leftTable->rowCount();
        int leftFileRow = -1;
        for (int row = 0; row < leftRows; ++row)
            if (leftTable->model()->index(row, 0).data().toString() == QStringLiteral("left.txt"))
                leftFileRow = row;
        QVERIFY(leftFileRow >= 0);
        leftTable->selectRow(leftFileRow);
        leftTable->setCurrentCell(leftFileRow, 0);
        action(window, "twoPanelsAction")->trigger();
        auto *rightPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(rightPanel);
        QVERIFY(rightPanel->isVisible());
        auto *rightAddress = rightPanel->findChild<QLineEdit *>("archivePath");
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                 QFileInfo(right).canonicalFilePath());
        QTRY_VERIFY(rightTable->rowCount() > 0);
        int rightFileRow = -1;
        for (int row = 0; row < rightTable->rowCount(); ++row)
            if (rightTable->model()->index(row, 0).data().toString() == QStringLiteral("right.txt"))
                rightFileRow = row;
        QVERIFY(rightFileRow >= 0);
        rightTable->selectRow(rightFileRow);
        const int rightRows = rightTable->rowCount();
        QCOMPARE(leftTable->rowCount(), leftRows);
        QCOMPARE(leftTable->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(rightTable->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(QFileInfo(leftAddress->text()).canonicalFilePath(),
                 QFileInfo(left).canonicalFilePath());
        // Resizing is presentation-only: neither panel is rebound or loses its
        // selected item, and pointer dragging keeps focus in the active list.
        window.activateWindow();
        rightTable->setFocus();
        QTRY_VERIFY(rightTable->hasFocus());
        const QPersistentModelIndex leftSelected(leftTable->model()->index(leftFileRow, 0));
        const QPersistentModelIndex rightSelected(rightTable->model()->index(rightFileRow, 0));
        QSignalSpy leftReset(leftTable->model(), &QAbstractItemModel::modelReset);
        QSignalSpy rightReset(rightTable->model(), &QAbstractItemModel::modelReset);
        auto *splitter = window.findChild<QSplitter *>("filePanelSplitter");
        QVERIFY(splitter);
        auto *handle = splitter->handle(1);
        const QPoint grip = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(handle, grip + QPoint(40, 0));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, grip + QPoint(40, 0));
        QVERIFY(rightTable->hasFocus());
        QVERIFY(leftSelected.isValid() && rightSelected.isValid());
        QVERIFY(leftTable->selectionModel()->isSelected(leftSelected));
        QVERIFY(rightTable->selectionModel()->isSelected(rightSelected));
        QVERIFY(leftReset.isEmpty() && rightReset.isEmpty());
        QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                 QFileInfo(right).canonicalFilePath());
        leftTable->setFocus();
        QTRY_VERIFY(leftTable->hasFocus());
        QFile added(QDir(right).filePath("added while inactive.txt"));
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.close();
        QTRY_VERIFY_WITH_TIMEOUT(rightTable->rowCount() > rightRows, 4000);
        QCOMPARE(leftTable->rowCount(), leftRows);
        window.resize(640, 400);
        QApplication::processEvents();
        QCOMPARE(window.width(), 640);
        QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(leftAddress), window));
        QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(rightAddress), window));
        QVERIFY(UiInspection::fullyVisible(*leftTable, window));
        QVERIFY(UiInspection::fullyVisible(*rightTable, window));
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("two-panel-disk-table-640")));
        action(window, "viewMode0Action")->trigger();
        QCOMPARE(leftPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 0);
        auto *leftIcons = leftPanel->findChild<QListView *>("iconList");
        auto *rightIcons = rightPanel->findChild<QListView *>("iconList");
        leftIcons->setFocus();
        QTest::keyClick(leftIcons, Qt::Key_Tab);
        QTRY_VERIFY(rightTable->hasFocus());
        QCOMPARE(QFileInfo(window.windowTitle()).canonicalFilePath(),
                 QFileInfo(right).canonicalFilePath());
        QVERIFY(action(window, "copyAction")->isEnabled());
        action(window, "viewMode0Action")->trigger();
        QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QCOMPARE(rightTable->selectionModel()->selectedRows().size(), 1);
        rightAddress->setText(QDir(right).filePath("nested"));
        QTest::keyClick(rightAddress, Qt::Key_Return);
        QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                 QFileInfo(QDir(right).filePath("nested")).canonicalFilePath());
        QCOMPARE(QFileInfo(leftAddress->text()).canonicalFilePath(),
                 QFileInfo(left).canonicalFilePath());
        QVERIFY(action(window, "copyAction")->isEnabled());
        const int extractCalls = backend.extractCalls;

        action(window, "copyAction")->trigger();
        QCOMPARE(backend.extractCalls, extractCalls);

        rightIcons->setFocus();
        QTest::keyClick(rightIcons, Qt::Key_Tab);
        QTRY_VERIFY(leftIcons->hasFocus());
        QCOMPARE(leftTable->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(rightTable->selectionModel()->selectedRows().size(), 0);
        QVERIFY(action(window, "copyAction")->isEnabled());
        QCOMPARE(QFileInfo(window.windowTitle()).canonicalFilePath(),
                 QFileInfo(left).canonicalFilePath());
        QTest::keyClick(leftIcons, Qt::Key_F9);
        QVERIFY(!rightPanel->isVisible());
        QApplication::processEvents();
        QVERIFY(leftPanel->width() >= window.width() - 8);
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(rightPanel->isVisible());
        QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                 QFileInfo(QDir(right).filePath("nested")).canonicalFilePath());
        QCOMPARE(leftPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        rightIcons->setFocus();
        QTest::keyClick(rightIcons, Qt::Key_F9);
        QApplication::processEvents();
        QVERIFY(!leftPanel->isVisible());
        QVERIFY(rightPanel->width() >= window.width() - 8);
        QTest::keyClick(rightIcons, Qt::Key_F9);
        QApplication::processEvents();
        QVERIFY(leftPanel->isVisible());
        leftIcons->setFocus();
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(leftAddress), window));
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(rightAddress), window));
            QVERIFY(UiInspection::fullyVisible(*leftIcons, window));
            QVERIFY(UiInspection::fullyVisible(*rightIcons, window));
            QVERIFY(
                UiInspection::writeReport(window, QStringLiteral("two-panel-disk-%1").arg(width)));
        }
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(!rightPanel->isVisible());
        QApplication::processEvents();
        QVERIFY(leftPanel->width() >= window.width() - 8);
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("one-panel-after-f9-640")));
        QVERIFY(window.close());
    }

    void mixedArchivePanelsRouteFocusAndDestination() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("left disk");
        const QString right = fixture.filePath("right disk");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        QFile file(QDir(left).filePath("source file.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        SavePanelPath(1, ToUString(right), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QWidget *leftPanel = window.findChild<QWidget *>("filePanel0");
        auto *leftAddress = leftPanel->findChild<QLineEdit *>("archivePath");
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        leftAddress->setText(left);
        QTest::keyClick(leftAddress, Qt::Key_Return);
        QTRY_VERIFY(leftTable->rowCount() > 0);
        action(window, "twoPanelsAction")->trigger();
        QWidget *rightPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(rightPanel);
        auto *rightAddress = rightPanel->findChild<QLineEdit *>("archivePath");
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        auto *rightIcons = rightPanel->findChild<QListView *>("iconList");
        for (int row = 0; row < leftTable->rowCount(); ++row)
            if (leftTable->model()->index(row, 0).data().toString() ==
                QStringLiteral("source file.txt")) {
                leftTable->selectRow(row);
                leftTable->setCurrentCell(row, 0);
            }
        QVERIFY(action(window, "copyAction")->isEnabled());
        for (const char *command : {"copyAction", "moveAction"}) {
            QTimer responder;
            QString destination;
            bool observed = false;
            connect(&responder, &QTimer::timeout, &window, [&] {
                auto *dialog = dynamic_cast<CopyDialog *>(QApplication::activeModalWidget());
                if (!dialog || !dialog->isVisible())
                    return;
                responder.stop();
                observed = true;
                destination = dialog->destination();
                dialog->reject();
            });
            responder.start(5);
            action(window, command)->trigger();
            responder.stop();
            QVERIFY(observed);
            QCOMPARE(QFileInfo(destination).canonicalFilePath(),
                     QFileInfo(right).canonicalFilePath());
        }
        window.activateWindow();
        QApplication::processEvents();
        rightTable->setFocus();
        QTRY_VERIFY(rightTable->hasFocus());
        const QString rightArchive = fixture.filePath("right archive.zip");
        openListedArchive(window, backend, rightArchive, {ArchiveEntry{"right entry.txt", 1, 1}});
        QTRY_VERIFY(rightTable->rowCount() > 0);
        QVERIFY(rightAddress->text().contains("right archive.zip"));
        int rightEntry = -1;
        for (int row = 0; row < rightTable->rowCount(); ++row)
            if (rightTable->model()->index(row, 0).data(Qt::UserRole).toString() ==
                QStringLiteral("right entry.txt"))
                rightEntry = row;
        QVERIFY(rightEntry >= 0);
        rightTable->selectRow(rightEntry);
        action(window, "viewMode0Action")->trigger();
        QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QVERIFY2(leftTable->isEnabled(), "inactive disk panel stayed disabled after archive open");
        window.activateWindow();
        QApplication::processEvents();
        PanelKeys::focusAddress(leftAddress);
        QTRY_VERIFY(leftAddress->hasFocus());
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        QTRY_VERIFY2(
            !action(window, "extractArchiveAction")->isEnabled(),
            qPrintable(
                QString("focus=%1 title=%2 leftNotify=%3 rightNotify=%4 leftArc=%5 rightArc=%6")
                    .arg(QApplication::focusWidget() ? QApplication::focusWidget()->objectName()
                                                     : QString())
                    .arg(window.windowTitle())
                    .arg(static_cast<FileTable *>(leftTable)
                             ->sourcePanel->ShouldProcessNotifications())
                    .arg(static_cast<FileTable *>(rightTable)
                             ->sourcePanel->ShouldProcessNotifications())
                    .arg(static_cast<FileTable *>(leftTable)->sourcePanel->IsArcFolder())
                    .arg(static_cast<FileTable *>(rightTable)->sourcePanel->IsArcFolder())));
        QTest::keyClick(leftTable, Qt::Key_F2, Qt::AltModifier);
        QTRY_VERIFY(rightPanel->findChild<QComboBox *>("pathCombo")->view()->isVisible());
        rightPanel->findChild<QComboBox *>("pathCombo")->hidePopup();
        QTest::keyClick(rightAddress, Qt::Key_F1, Qt::AltModifier);
        QTRY_VERIFY(leftPanel->findChild<QComboBox *>("pathCombo")->view()->isVisible());
        leftPanel->findChild<QComboBox *>("pathCombo")->hidePopup();
        rightIcons->setFocus();
        QTRY_VERIFY(rightIcons->hasFocus());
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        QTimer responder;
        QString destination;
        bool observed = false;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = dynamic_cast<CopyDialog *>(QApplication::activeModalWidget());
            if (!dialog || !dialog->isVisible())
                return;
            responder.stop();
            observed = true;
            destination = dialog->destination();
            dialog->reject();
        });
        responder.start(5);
        action(window, "extractArchiveAction")->trigger();
        responder.stop();
        QVERIFY(observed);
        QCOMPARE(QFileInfo(destination).canonicalFilePath(), QFileInfo(left).canonicalFilePath());
        const QString leftArchive = fixture.filePath("left archive.zip");
        window.activateWindow();
        QApplication::processEvents();
        leftTable->setFocus();
        QTRY_VERIFY(leftTable->hasFocus());
        openListedArchive(window, backend, leftArchive, {ArchiveEntry{"left entry.txt", 2, 2}});
        QTRY_VERIFY(leftTable->rowCount() > 0);
        QVERIFY(leftAddress->text().contains("left archive.zip"));
        int leftEntry = -1;
        for (int row = 0; row < leftTable->rowCount(); ++row)
            if (leftTable->model()->index(row, 0).data(Qt::UserRole).toString() ==
                QStringLiteral("left entry.txt"))
                leftEntry = row;
        QVERIFY(leftEntry >= 0);
        leftTable->selectRow(leftEntry);
        QCOMPARE(leftPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 0);
        window.activateWindow();
        QApplication::processEvents();
        rightIcons->setFocus();
        QTRY_VERIFY(rightIcons->hasFocus());
        QVERIFY(rightAddress->text().contains("right archive.zip"));
        QCOMPARE(rightTable->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        leftTable->setFocus();
        QTRY_VERIFY(leftTable->hasFocus());
        QVERIFY(leftAddress->text().contains("left archive.zip"));
        QCOMPARE(leftTable->selectionModel()->selectedRows().size(), 1);
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(leftAddress), window));
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(rightAddress), window));
            QVERIFY(UiInspection::fullyVisible(*leftTable, window));
            QVERIFY(UiInspection::fullyVisible(*rightIcons, window));
            QVERIFY(UiInspection::writeReport(window,
                                              QStringLiteral("two-panel-archive-%1").arg(width)));
        }
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(!rightPanel->isVisible());
        QVERIFY(leftAddress->text().contains("left archive.zip"));
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("archive-hidden-panel")));
    }
    void restoresPanelCountFocusPathsAndSplitter() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("left restore");
        const QString right = fixture.filePath("right restore");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(0, ToUString(left), settings);
        SavePanelPath(1, ToUString(right), settings);
        CListMode modes;
        modes.Panels[0] = 0;
        modes.Panels[1] = 3;
        modes.Save(settings);
        CWindowInfo saved{};
        saved.rect = {0, 0, 980, 620};
        saved.numPanels = 2;
        saved.currentPanel = 1;
        saved.splitterPos = 300;
        saved.Save(settings);
        ControlledBackend backend;
        {
            MainWindow window(&backend);
            window.show();
            QWidget *leftPanel = window.findChild<QWidget *>("filePanel0");
            QWidget *rightPanel = window.findChild<QWidget *>("filePanel1");
            QVERIFY(leftPanel && rightPanel);
            QTRY_VERIFY(leftPanel->isVisible() && rightPanel->isVisible());
            auto *leftAddress = leftPanel->findChild<QLineEdit *>("archivePath");
            auto *rightAddress = rightPanel->findChild<QLineEdit *>("archivePath");
            QCOMPARE(QFileInfo(leftAddress->text()).canonicalFilePath(),
                     QFileInfo(left).canonicalFilePath());
            QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                     QFileInfo(right).canonicalFilePath());
            QTRY_COMPARE(QFileInfo(window.windowTitle()).canonicalFilePath(),
                         QFileInfo(right).canonicalFilePath());
            QCOMPARE(leftPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
            QCOMPARE(rightPanel->findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 0);
            auto *splitter = window.findChild<QSplitter *>("filePanelSplitter");
            QVERIFY(splitter);
            QTRY_VERIFY2(qAbs(splitter->sizes().value(0) - 300) <= 8,
                         qPrintable(QString("split=%1/%2 min=%3/%4 width=%5")
                                        .arg(splitter->sizes().value(0))
                                        .arg(splitter->sizes().value(1))
                                        .arg(leftPanel->minimumSizeHint().width())
                                        .arg(rightPanel->minimumSizeHint().width())
                                        .arg(window.width())));
            splitter->setSizes({400, 570});
            QApplication::processEvents();
            QVERIFY(window.close());
        }
        bool positionDefined = false, panelsDefined = false;
        CWindowInfo persisted{};
        persisted.Read(positionDefined, panelsDefined, settings);
        QVERIFY(panelsDefined);
        QCOMPARE(persisted.numPanels, 2u);
        QCOMPARE(persisted.currentPanel, 1u);
        QVERIFY2(qAbs(int(persisted.splitterPos) - 400) <= 8,
                 qPrintable(QString::number(persisted.splitterPos)));
        MainWindow reopened(&backend);
        reopened.show();
        auto *splitter = reopened.findChild<QSplitter *>("filePanelSplitter");
        QTRY_VERIFY(qAbs(splitter->sizes().value(0) - 400) <= 8);
        QTRY_COMPARE(QFileInfo(reopened.windowTitle()).canonicalFilePath(),
                     QFileInfo(right).canonicalFilePath());
        QVERIFY(UiInspection::writeReport(reopened, QStringLiteral("two-panel-restored")));
    }
    void hiddenLeftPanelPathSurvivesRestart() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("hidden left");
        const QString right = fixture.filePath("visible right");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(0, ToUString(left), settings);
        SavePanelPath(1, ToUString(right), settings);
        CWindowInfo saved{};
        saved.rect = {0, 0, 980, 620};
        saved.numPanels = 1;
        saved.currentPanel = 1;
        saved.splitterPos = 390;
        saved.Save(settings);
        ControlledBackend backend;
        {
            MainWindow window(&backend);
            window.show();
            QVERIFY(!window.findChild<QWidget *>("filePanel0"));
            QVERIFY(window.findChild<QWidget *>("filePanel1"));
            QTRY_COMPARE(QFileInfo(window.windowTitle()).canonicalFilePath(),
                         QFileInfo(right).canonicalFilePath());
            QVERIFY(window.close());
        }
        UString retained;
        QVERIFY(ReadPanelPath(0, retained, settings));
        QCOMPARE(QFileInfo(ToQString(retained)).canonicalFilePath(),
                 QFileInfo(left).canonicalFilePath());
        MainWindow reopened(&backend);
        reopened.show();
        action(reopened, "twoPanelsAction")->trigger();
        auto *leftPanel = reopened.findChild<QWidget *>("filePanel0");
        QVERIFY(leftPanel);
        QTRY_VERIFY(leftPanel->isVisible());
        QCOMPARE(
            QFileInfo(leftPanel->findChild<QLineEdit *>("archivePath")->text()).canonicalFilePath(),
            QFileInfo(left).canonicalFilePath());
        QVERIFY(UiInspection::writeReport(reopened, QStringLiteral("hidden-left-restored")));
    }
    void parentKey_data() { keypadSelection_data(); }
    void parentKey() {
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = QStringLiteral("папка с пробелом");
        QVERIFY(QDir(dir.path()).mkdir(folder));
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *table = window.findChild<FileTable *>("archiveTable");
        address->setText(dir.path());
        QTest::keyClick(address, Qt::Key_Return);
        if (archive) {
            openListedArchive(window, backend, dir.filePath("parent.zip"),
                              {ArchiveEntry{folder + "/file.txt", 1, 1}});
        }
        action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
        window.show();
        QWidget *view = mode == 3
                            ? static_cast<QWidget *>(table)
                            : static_cast<QWidget *>(window.findChild<QListView *>("iconList"));
        const QString parentPath = address->text();
        for (auto modifiers :
             {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier}) {
#ifdef Q_OS_MACOS
            if (modifiers == Qt::ControlModifier)
                continue; // Command-Backspace is the redesigned Delete command.
#endif
            table->selectRow(0);
            table->setCurrentCell(0, 0);
            action(window, "openItemAction")->trigger();
            QVERIFY(address->text() != parentPath);
            const QString childPath = address->text();
            // Editing the address must never dispatch a panel navigation command.
            PanelKeys::focusAddress(address);
            QTest::keyClick(address, Qt::Key_Backspace, modifiers);
            address->setText(childPath);
            view->setFocus();
            QTest::keyClick(view, Qt::Key_Backspace, modifiers);
            QCOMPARE(address->text(), parentPath);
        }
        if (mode == 3 && archive &&
            qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1) {
            const QString screenshots = QDir::currentPath() + "/ui-screenshots";
            QVERIFY(QDir().mkpath(screenshots));
            for (int width : {980, 640}) {
                window.resize(width, width == 980 ? 620 : 400);
                QApplication::processEvents();
                QVERIFY(window.grab().save(screenshots +
                                           QStringLiteral("/parent-key-%1.png").arg(width)));
            }
        }
        QVERIFY(backend.prepareArchiveFixture(dir.filePath("busy.zip")));
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.cancel(); });
            const QString busyPath = address->text();
            QVERIFY(!action(window, "parentFolderAction")->isEnabled());
            QTest::keyClick(view, Qt::Key_Backspace, Qt::ControlModifier);
            QCOMPARE(address->text(), busyPath);
        });
        window.openArchive(dir.filePath("busy.zip"));
    }

    void addressKeys_data() { keypadSelection_data(); }
    void addressKeys() {
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(dir.path());
        QTest::keyClick(address, Qt::Key_Return);
        if (archive) {
            openListedArchive(window, backend, dir.filePath("адрес space.zip"),
                              {ArchiveEntry{"папка/файл.txt", 1, 1}});
        }
        action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QWidget *view = mode == 3
                            ? static_cast<QWidget *>(window.findChild<FileTable *>("archiveTable"))
                            : static_cast<QWidget *>(window.findChild<QListView *>("iconList"));
        const QString currentPath = address->text();
        for (int key : {Qt::Key_F1, Qt::Key_F2}) {
            view->setFocus();
            QTRY_VERIFY(view->hasFocus());
            QTest::keyClick(view, static_cast<Qt::Key>(key), Qt::AltModifier);
            QTRY_VERIFY(combo->view()->isVisible());
            QCOMPARE(address->text(), currentPath);
            combo->hidePopup();
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            QTest::keyClick(address, static_cast<Qt::Key>(key), Qt::AltModifier);
            QTRY_VERIFY(combo->view()->isVisible());
            combo->hidePopup();
            PanelKeys::focusAddress(address);
            QTest::keyClick(address, Qt::Key_Tab);
            QTRY_VERIFY(view->hasFocus());
            QCOMPARE(address->text(), currentPath);
            QTest::keyClick(view, static_cast<Qt::Key>(key), Qt::AltModifier | Qt::ShiftModifier);
            QVERIFY(!combo->view()->isVisible());
        }
        if (mode == 3 && !archive &&
            qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1) {
            const QString screenshots = QDir::currentPath() + "/ui-screenshots";
            QVERIFY(QDir().mkpath(screenshots));
            for (int width : {980, 640}) {
                combo->hidePopup();
                window.resize(width, width == 980 ? 620 : 400);
                QCOMPARE(window.width(), width);
                combo->showPopup();
                QApplication::processEvents();
                QVERIFY(window.grab().save(screenshots +
                                           QStringLiteral("/address-key-%1.png").arg(width)));
                QVERIFY(combo->view()->window()->grab().save(
                    screenshots + QStringLiteral("/address-popup-%1.png").arg(width)));
            }
        }
    }

    void keypadSelection_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("archive");
        for (int mode : {0, 3}) {
            QTest::newRow(qPrintable(QStringLiteral("archive-%1").arg(mode))) << mode << true;
            QTest::newRow(qPrintable(QStringLiteral("disk-%1").arg(mode))) << mode << false;
        }
    }

    void keypadSelection() {
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        const QStringList names = {QStringLiteral("Альфа файл.txt"), "beta.TXT", "other.bin"};
        if (archive) {
            openListedArchive(window, backend, temp.filePath("keys.zip"),
                              {ArchiveEntry{names[0], 1, 1}, ArchiveEntry{names[1], 2, 2},
                               ArchiveEntry{names[2], 3, 3}});
        } else {
            for (const auto &name : names) {
                QFile file(temp.filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
            }
            auto *address = window.findChild<QLineEdit *>("archivePath");
            address->setText(temp.path());
            QTest::keyClick(address, Qt::Key_Return);
        }
        auto *table = window.findChild<FileTable *>("archiveTable");
        action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
        window.show();
        QWidget *view = mode == 3
                            ? static_cast<QWidget *>(table)
                            : static_cast<QWidget *>(window.findChild<QListView *>("iconList"));
        QVERIFY(view);
        view->setFocus();
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() == names[0])
                table->setCurrentCell(row, 0);
        table->clearSelection();
        const Qt::KeyboardModifiers keypad = Qt::KeypadModifier;
        QTest::keyClick(view, Qt::Key_Plus, keypad | Qt::ShiftModifier);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 3);
        QTest::keyClick(view, Qt::Key_Minus, keypad | Qt::ShiftModifier);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 0);
        for (const auto modifier : {keypad, keypad | Qt::AltModifier})
            for (const auto key : {Qt::Key_Plus, Qt::Key_Minus, Qt::Key_Asterisk}) {
                QTest::keyClick(view, key, modifier);
                QCOMPARE(table->selectionModel()->selectedRows().size(), 0);
            }

        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTest::keyClick(address, Qt::Key_Asterisk, keypad);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 0);
        QVERIFY(backend.prepareArchiveFixture(temp.filePath("busy.zip")));
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.cancel(); });
            QVERIFY(!action(window, "selectAllAction")->isEnabled());
            QTest::keyClick(view, Qt::Key_Plus, keypad | Qt::ShiftModifier);
            QCOMPARE(table->selectionModel()->selectedRows().size(), 0);
        });
        window.openArchive(temp.filePath("busy.zip"));
    }

    void alternativeSelection_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("archive");
        for (int mode : {0, 3})
            for (bool archive : {false, true})
                QTest::newRow(qPrintable(
                    QStringLiteral("%1-mode%2").arg(archive ? "archive" : "disk").arg(mode)))
                    << mode << archive;
    }
    void alternativeSelection() {
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", true);
        settings.sync();
        const auto restore = qScopeGuard([&] {
            settings.remove("FM/AlternativeSelection");
            settings.sync();
        });
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        const QStringList names = {QStringLiteral("Альфа файл.txt"), "beta.TXT", "other.bin"};
        if (archive) {
            openListedArchive(window, backend, temp.filePath("marks.zip"),
                              {ArchiveEntry{names[0], 1, 1}, ArchiveEntry{names[1], 2, 2},
                               ArchiveEntry{names[2], 3, 3}});
        } else {
            for (const auto &name : names) {
                QFile file(temp.filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
            }
            auto *address = window.findChild<QLineEdit *>("archivePath");
            address->setText(temp.path());
            QTest::keyClick(address, Qt::Key_Return);
        }
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *icons = window.findChild<QListView *>("iconList");
        QTRY_COMPARE(table->rowCount(), 3);
        QAction *viewMode =
            action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)));
        QTRY_VERIFY(viewMode->isEnabled());
        viewMode->trigger();
        QAbstractItemView *view = mode == 3 ? static_cast<QAbstractItemView *>(table)
                                            : static_cast<QAbstractItemView *>(icons);
        QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
        QCOMPARE(icons->selectionMode(), QAbstractItemView::SingleSelection);
        auto click = [&](int row, Qt::KeyboardModifiers modifiers) {
            const QModelIndex index = table->model()->index(row, 0);
            view->scrollTo(index);
            QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers,
                              view->visualRect(index).center());
        };
        auto marked = [&](int row) {
            return table->model()->index(row, 0).data(Qt::BackgroundRole).isValid();
        };
        QSignalSpy tableClicks(table, &QTableView::clicked);
        QSignalSpy iconClicks(icons, &QListView::clicked);
        click(0, Qt::ControlModifier);
        QCOMPARE(mode == 3 ? tableClicks.count() : iconClicks.count(), 1);
        QVERIFY2(marked(0),
                 qPrintable(
                     QStringLiteral("focus=%1 selected=%2 source=%3 background=%4 name=%5 index=%6")
                         .arg(table->currentRow())
                         .arg(table->selectionModel()->selectedRows().size())
                         .arg(static_cast<FileTable *>(table)->sourcePanel->IsItemMarked(
                             table->model()->index(0, 0).data(Qt::UserRole + 6).toUInt()))
                         .arg(table->model()->index(0, 0).data(Qt::BackgroundRole).toString())
                         .arg(table->model()->index(0, 0).data().toString())
                         .arg(table->model()->index(0, 0).data(Qt::UserRole + 6).toUInt())));
        click(2, Qt::ControlModifier);
        QVERIFY(marked(0) && marked(2));
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        click(1, Qt::ShiftModifier);
        QVERIFY(!marked(0));
        QVERIFY(marked(1) && marked(2));
        CRecordVector<UInt32> markedIndices;
        static_cast<FileTable *>(table)->sourcePanel->Get_ItemIndices_Selected(markedIndices);
        QCOMPARE(markedIndices.Size(), unsigned(2));
        if (mode == 3 && !archive) {
            QVERIFY(action(window, "renameAction")->isEnabled());
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<QDialog *>("copyDialog");
                QVERIFY(dialog);
                const auto close = qScopeGuard([dialog] { dialog->reject(); });
                const QString summary = ToQString(static_cast<CopyDialog *>(dialog)->Info)
                                            .normalized(QString::NormalizationForm_C);
                for (int row : {1, 2})
                    QVERIFY2(
                        summary.contains(table->model()->index(row, 0).data().toString().normalized(
                            QString::NormalizationForm_C)),
                        qPrintable(summary));
                QVERIFY2(!summary.contains(table->model()->index(0, 0).data().toString().normalized(
                             QString::NormalizationForm_C)),
                         qPrintable(summary));
            });
            action(window, "copyAction")->trigger();
        }
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Insert);
        QVERIFY(!marked(1));
        QCOMPARE(table->currentRow(), 2);
        action(window, "selectAllAction")->trigger();
        for (int row = 0; row < 3; ++row)
            QVERIFY(marked(row));
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        if (((mode == 3 && !archive) || (mode == 0 && archive)) &&
            qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1) {
            const QString folder =
                QDir(qEnvironmentVariable("SEVENZIP_UI_REPORT_DIR")).filePath("../ui-screenshots");
            QVERIFY(QDir().mkpath(folder));
            QVERIFY(view->viewport()->grab().save(QDir(folder).filePath(
                archive ? "alternative-mark-icons.png" : "alternative-mark-table.png")));
        }
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY2(UiInspection::fullyVisible(*view, window),
                     qPrintable(QStringLiteral("view=%1,%2 %3x%4 root=%5x%6 visible=%7 mode=%8")
                                    .arg(UiInspection::bounds(*view, window).x())
                                    .arg(UiInspection::bounds(*view, window).y())
                                    .arg(view->width())
                                    .arg(view->height())
                                    .arg(window.width())
                                    .arg(window.height())
                                    .arg(view->isVisible())
                                    .arg(mode)));
            QVERIFY(UiInspection::writeReport(window,
                                              QStringLiteral("alternative-selection-%1-mode%2-%3")
                                                  .arg(archive ? "archive" : "disk")
                                                  .arg(mode)
                                                  .arg(width)));
        }
        action(window, mode == 3 ? "viewMode0Action" : "viewMode3Action")->trigger();
        for (int row = 0; row < 3; ++row)
            QVERIFY(marked(row));
        action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
        if (archive)
            backend.queueList({{names[0], 1, 1}, {names[1], 2, 2}, {names[2], 3, 3}});
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(table->rowCount(), 3);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        for (int row = 0; row < 3; ++row)
            QVERIFY(marked(row));
        if (mode == 3 && !archive) {
            const auto toggleMode = [&](bool enabled) {
                QTimer::singleShot(0, &window, [&] {
                    auto *dialog = window.findChild<QDialog *>("settingsDialog");
                    QVERIFY(dialog);
                    dialog->findChild<QCheckBox *>("alternativeSelectionOption")
                        ->setChecked(enabled);
                    dialog->accept();
                });
                action(window, "settingsAction")->trigger();
            };
            toggleMode(false);
            QCOMPARE(table->selectionMode(), QAbstractItemView::ExtendedSelection);
            QCOMPARE(table->selectionModel()->selectedRows().size(), 3);
            for (int row = 0; row < 3; ++row)
                QVERIFY(!marked(row));
            toggleMode(true);
            QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
            QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
            for (int row = 0; row < 3; ++row)
                QVERIFY(marked(row));
        }
        action(window, "deselectAllAction")->trigger();
        for (int row = 0; row < 3; ++row)
            QVERIFY(!marked(row));
        click(0, Qt::NoModifier);
        view->setFocus();
        QTest::keyClick(view, mode <= 1 ? Qt::Key_Right : Qt::Key_Down, Qt::ShiftModifier);
        QApplication::processEvents();
        QVERIFY(marked(0));
        QVERIFY(marked(1));
    }

    void alternativeRootUsesSourceMarks() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", true);
        settings.sync();
        const auto restore = qScopeGuard([&] {
            settings.remove("FM/AlternativeSelection");
            settings.sync();
        });
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(action(window, "openRootAction")->isEnabled());
        action(window, "openRootAction")->trigger();
        auto *view = table(window);
        QVERIFY(view->rowCount() > 0);
        QCOMPARE(view->selectionMode(), QAbstractItemView::SingleSelection);
        action(window, "selectAllAction")->trigger();
        for (int row = 0; row < view->rowCount(); ++row)
            QVERIFY(view->model()->index(row, 0).data(Qt::BackgroundRole).isValid());
        action(window, "deselectAllAction")->trigger();
        for (int row = 0; row < view->rowCount(); ++row)
            QVERIFY(!view->model()->index(row, 0).data(Qt::BackgroundRole).isValid());
    }

    void alternativeMarksSurviveRemovalAndScroll() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", true);
        settings.setValue("FM/ShowDots", true);
        settings.sync();
        const auto restore = qScopeGuard([&] {
            settings.remove("FM/AlternativeSelection");
            settings.remove("FM/ShowDots");
            settings.sync();
        });
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        for (int i = 0; i < 50; ++i) {
            QFile file(temp.filePath(QStringLiteral("файл %1.txt").arg(i, 2, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        window.resize(640, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *files = table(window);
        QTRY_COMPARE(files->rowCount(), 51);
        action(window, "selectAllAction")->trigger();
        QCOMPARE(files->model()->index(0, 0).data(Qt::UserRole + 6).toUInt(), UInt32(-1));
        QVERIFY(!files->model()->index(0, 0).data(Qt::BackgroundRole).isValid());
        QVERIFY(files->model()->index(50, 0).data(Qt::BackgroundRole).isValid());
        QVERIFY(QFile::remove(temp.filePath(QStringLiteral("файл 25.txt"))));
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(files->rowCount(), 50);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        CRecordVector<UInt32> marks;
        static_cast<FileTable *>(files)->sourcePanel->Get_ItemIndices_Selected(marks);
        QCOMPARE(marks.Size(), unsigned(49));
        QVERIFY(!files->model()->index(0, 0).data(Qt::BackgroundRole).isValid());
        action(window, "viewMode0Action")->trigger();
        auto *icons = window.findChild<QListView *>("iconList");
        icons->scrollTo(icons->model()->index(49, 0));
        QApplication::processEvents();
        QVERIFY(files->model()->index(49, 0).data(Qt::BackgroundRole).isValid());
        static_cast<FileTable *>(files)->sourcePanel->Get_ItemIndices_Selected(marks);
        QCOMPARE(marks.Size(), unsigned(49));
        QVERIFY(UiInspection::fullyVisible(*icons, window));
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("alternative-removal-640")));
    }
};

QTEST_MAIN(PanelKeyTests)
#include "panel_key_tests.moc"
