#include "7zip/UI/FileManager/resource.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"

class RootNavigationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void openRootNamespace(MainWindow &window) {
        action(window, "openRootAction")->trigger();
        action(window, "parentFolderAction")->trigger();
    }
  private slots:
    void sourceRootHotSwitchPreservesItemsAndSelection() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString("-"), settingsPath);
        ReloadLang(settingsPath);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openRootNamespace(window);
        auto *view = table(window);
        QPersistentModelIndex computer;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 0u)
                computer = view->model()->index(row, 0);
        QVERIFY(computer.isValid());
        view->selectRow(computer.row());
        const int count = view->rowCount();
        for (const char *lang : {"ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath);
            ReloadLang(settingsPath);
            QtNotifyLanguageChanged();
            QCOMPARE(view->rowCount(), count);
            QCOMPARE(view->model()->index(computer.row(), 0), computer);
            QVERIFY(PanelIndexSelected(computer));
            QCOMPARE(computer.data().toString(), QString::fromWCharArray(LangString(IDS_COMPUTER)));
            QCOMPARE(computer.data(Qt::UserRole + 3).toString(), computer.data().toString());
            QCOMPARE(backend.listCalls, 0);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*view, window));
                QVERIFY(UiInspection::writeReport(
                    window, QString("language-root-%1-%2").arg(lang).arg(size.width())));
            }
        }
    }
#ifndef Q_OS_WIN
    void driveTypeHotSwitchPreservesRowsAndSelection() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString("-"), settingsPath);
        ReloadLang(settingsPath);
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, "openRootAction")->trigger();
        auto *view = static_cast<FileTable *>(table(window));
        QVERIFY(view->sourcePanel->IsFSDrivesFolder());
        QVERIFY(view->rowCount() > 0);
        int typeColumn = -1;
        for (int column = 0; column < view->columnCount(); ++column)
            if (view->propertyId(column) == kpidType)
                typeColumn = column;
        QVERIFY(typeColumn >= 0);
        const QPersistentModelIndex type = view->model()->index(0, typeColumn);
        const QPersistentModelIndex name = view->model()->index(0, 0);
        const QString originalName = name.data().toString();
        view->selectRow(0);
        const int count = view->rowCount();
        for (const char *language : {"ru", "-"}) {
            SaveRegLang(UString(language), settingsPath);
            ReloadLang(settingsPath);
            QtNotifyLanguageChanged();
            QCOMPARE(view->rowCount(), count);
            QCOMPARE(view->model()->index(name.row(), 0), name);
            QCOMPARE(name.data().toString(), originalName);
            QVERIFY(PanelIndexSelected(name));
            QCOMPARE(type.data().toString(),
                     QString::fromUtf8(language[0] == 'r' ? "Локальный" : "Fixed"));
        }
    }
    void networkNamespaceOpensWithoutRemoteServices() {
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        openRootNamespace(window);
        int networkRow = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 2u)
                networkRow = row;
        QVERIFY(networkRow >= 0);
        view->selectRow(networkRow);
        action(window, "openItemAction")->trigger();
        QVERIFY(!address->text().isEmpty());
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
        QVERIFY(action(window, "backAction")->isEnabled());
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            window.show();
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*window.findChild<QWidget *>("panelNavigation0"),
                                               window));
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(
                window, QSysInfo::productType() +
                            QStringLiteral("-network-%1x%2").arg(size.width()).arg(size.height())));
        }
        const QString network = address->text();
        action(window, "openRootAction")->trigger();
        QVERIFY(static_cast<FileTable *>(view)->sourcePanel->IsFSDrivesFolder());
        action(window, "openRootAction")->trigger();
        action(window, "parentFolderAction")->trigger();
        QVERIFY(address->text().isEmpty());
        action(window, "backAction")->trigger();
        QVERIFY(view->sourcePanel->IsFSDrivesFolder());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(address->text(), network);
        action(window, "backAction")->trigger();
        QCOMPARE(view->rowCount(), 3);
    }

    void mountedVolumesOpenThroughNativeRoot() {
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        openRootNamespace(window);
        int mountsRow = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 1u)
                mountsRow = row;
        QVERIFY(mountsRow >= 0);
        view->setCurrentCell(mountsRow, 0);
        view->selectRow(mountsRow);
        QVERIFY(action(window, "openItemAction")->isEnabled());
        action(window, "openItemAction")->trigger();
        QVERIFY2(window.windowTitle().contains(QStringLiteral("Volumes")),
                 qPrintable(window.windowTitle() + " address=" + address->text()));
        QVERIFY(view->rowCount() > 0);
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            window.show();
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*window.findChild<QWidget *>("panelNavigation0"),
                                               window));
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(
                window, QSysInfo::productType() +
                            QStringLiteral("-mounts-%1x%2").arg(size.width()).arg(size.height())));
        }
        view->selectRow(0);
        action(window, "openItemAction")->trigger();
        QVERIFY(QFileInfo(address->text()).isDir());
        action(window, "backAction")->trigger();
        QVERIFY(view->rowCount() > 0);
    }
#endif
    void sourceRootRendersAndRoundTripsThroughComputer() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        QVERIFY(address && view);
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *root = action(window, "openRootAction");
        root->trigger();
        const QString drivesPrefix = address->text();
        action(window, "parentFolderAction")->trigger();
#ifdef Q_OS_WIN
        QCOMPARE(view->rowCount(), 4);
#else
        QCOMPARE(view->rowCount(), 3);
#endif
        int computerRow = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 0u)
                computerRow = row;
        QVERIFY(computerRow >= 0);
        QVERIFY(!view->model()->index(computerRow, 0).data(Qt::UserRole + 3).toString().isEmpty());
        QVERIFY(address->text().isEmpty());
        QCOMPARE(window.windowTitle(), QStringLiteral("7-Zip"));
        QVERIFY(!action(window, "parentFolderAction")->isEnabled());
        action(window, "refreshAction")->trigger();
#ifdef Q_OS_WIN
        QCOMPARE(view->rowCount(), 4);
#else
        QCOMPARE(view->rowCount(), 3);
#endif
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            window.show();
            QCoreApplication::processEvents();
            auto *toolbar = window.findChild<QToolBar *>("panelFileActions0");
            QVERIFY(toolbar);
            QVERIFY(UiInspection::fullyVisible(*toolbar, window));
            QVERIFY(UiInspection::fullyVisible(*window.findChild<QWidget *>("panelNavigation0"),
                                               window));
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::fullyVisible(*window.statusBar(), window));
            QVERIFY(UiInspection::writeReport(
                window, QSysInfo::productType() +
                            QStringLiteral("-root-%1x%2").arg(size.width()).arg(size.height())));
        }
        view->selectRow(computerRow);
        QVERIFY(action(window, "openItemAction")->isEnabled());
        QVERIFY(!action(window, "openOutsideAction")->isEnabled());
        QVERIFY(!action(window, "copyAction")->isEnabled());
        action(window, "openItemAction")->trigger();
        QVERIFY(view->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(address->text(), drivesPrefix);
        action(window, "backAction")->trigger();
        QVERIFY(address->text().isEmpty());
        QVERIFY(view->rowCount() > 0);
        action(window, "backAction")->trigger();
        QVERIFY(view->sourcePanel->IsFSDrivesFolder());
        action(window, "backAction")->trigger();
        QCOMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
#ifndef Q_OS_WIN
        PanelKeys::focusAddress(address);
        address->setText(QStringLiteral("/"));
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(view->sourcePanel->IsFSFolder());
        QVERIFY(!view->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(address->text(), QStringLiteral("/"));
#endif
    }

    void keyboardRootAndTypedRelativeAddressUseNativeBinding() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath(QStringLiteral("папка с пробелом"))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        PanelKeys::focusAddress(address);
        address->setText(QStringLiteral("папка с пробелом"));
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(
            address->text(),
            QFileInfo(fixture.filePath(QStringLiteral("папка с пробелом"))).canonicalFilePath());
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Backslash);
        QVERIFY(view->rowCount() > 0);
        QVERIFY(!address->text().isEmpty());
        QVERIFY(static_cast<FileTable *>(view)->sourcePanel->IsFSDrivesFolder());
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
    }

    void busyArchiveCannotEnterRootThroughStaleAction() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        const QString archive = fixture.filePath(QStringLiteral("архив space.zip"));
        openListedArchive(window, backend, archive, {ArchiveEntry{"item.txt", 1, 1}});
        auto *root = action(window, "openRootAction");
        QVERIFY(root->isEnabled());
        action(window, "testArchiveAction")->trigger();
        QVERIFY(!root->isEnabled());
        root->setEnabled(true);
        root->trigger();
        QCOMPARE(table(window)->rowCount(), 1);
        QCOMPARE(backend.testCalls, 1);
        backend.cancel();
        root->trigger();
        QVERIFY(table(window)->rowCount() > 0);
        QVERIFY(!window.findChild<QLineEdit *>("archivePath")->text().isEmpty());
        QVERIFY(static_cast<FileTable *>(table(window))->sourcePanel->IsFSDrivesFolder());
    }
};

QTEST_MAIN(RootNavigationTests)
#include "root_navigation_tests.moc"
