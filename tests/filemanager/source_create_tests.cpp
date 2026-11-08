#include "7zip/UI/FileManager/resource.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class SourceCreateTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static int row(FileTable *view, const QString &name) {
        for (int i = 0; i < view->rowCount(); ++i)
            if (view->model()->index(i, 0).data(Qt::UserRole + 3).toString() == name)
                return i;
        return -1;
    }
  private slots:
    void invalidDraftCanBeCorrected() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend, observerBackend;
        MainWindow window(&backend), observer(&observerBackend);
        observer.show();
        observer.openPath(fixture.path());
        auto *observed = table(observer);
        QTRY_VERIFY(observed->isEnabled());
        observed->sourcePanel->Set_AutoRefresh_Mode(false);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 0);
        view->setFocus();
        const QString finalName = QStringLiteral("corrected folder");
        action(window, "createFolderAction")->trigger();
        auto *name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        name->setText(QStringLiteral("truncated") + QChar::Null + "suffix");
        QTest::keyClick(name, Qt::Key_Return);
        auto *error = window.findChild<QLabel *>("inlineFolderError");
        QVERIFY(error && error->isVisible() && !error->text().isEmpty());
        QVERIFY(!QFileInfo::exists(fixture.filePath("truncated")));
        QCOMPARE(view->rowCount(), 0);
        QCOMPARE(observed->rowCount(), 0);
        QCOMPARE(name->selectedText(), name->text());
        QTest::keyClicks(name, finalName);
        QCOMPARE(name->text(), finalName);
        QTest::keyClick(name, Qt::Key_Return);
        const QFileInfo created(fixture.filePath(finalName));
        QVERIFY(created.isDir());
        QVERIFY(!QFileInfo::exists(fixture.filePath("truncated")));
        QTRY_COMPARE(view->rowCount(), 1);
        QTRY_COMPARE(row(observed, finalName), 0);
        QCOMPARE(view->currentRow(), row(view, finalName));
    }

    void create_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(pane).arg(language)))
                    << pane << language;
    }
    void create() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QTemporaryDir peerDirectory;
        QVERIFY(fixture.isValid());
        QVERIFY(peerDirectory.isValid());
        QFile keep(fixture.filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        QTRY_COMPARE(view->rowCount(), 1);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->selectRow(row(view, "keep.bin"));
        view->setCurrentCell(row(view, "keep.bin"), 0);
        view->setFocus();
        const auto initial = view->selectionModel()->selection();
        const QString entered = QStringLiteral("new folder 空/child");
        action(window, "createFolderAction")->trigger();
        QPointer<QLineEdit> name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        name->setText(entered);
        for (const int width : {980, 640}) {
            window.resize(width, 560);
            QTRY_VERIFY(UiInspection::fullyVisible(*name, window));
            QVERIFY(UiInspection::writeReport(
                window, QString("source-create-%1-%2").arg(QTest::currentDataTag()).arg(width)));
        }
        QTest::keyClick(name, Qt::Key_Escape);
        QTRY_VERIFY(!name);
        QCOMPARE(view->selectionModel()->selection(), initial);
        QVERIFY(!QFileInfo::exists(fixture.filePath(entered)));
        action(window, "createFolderAction")->trigger();
        name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        name->setText(entered);
        QTest::keyClick(name, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo::exists(fixture.filePath(entered)));
        QTRY_COMPARE(view->rowCount(), 2);
        const QString focused = QStringLiteral("new folder 空");
        QTRY_COMPARE(view->currentRow(), row(view, focused));
        QTRY_COMPARE(view->selectionModel()->selectedRows().size(), 1);
        UStringVector selectedNames;
        view->sourcePanel->GetSelectedNames(selectedNames);
        QCOMPARE(selectedNames.Size(), 1u);
        QCOMPARE(ToQString(selectedNames[0]), focused);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peer->rowCount(), 0);
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
    void createdPathSelectsVisibleFolder_data() {
        QTest::addColumn<QString>("pathForm");
        QTest::newRow("relative") << QString("relative");
        QTest::newRow("absolute") << QString("absolute");
        QTest::newRow("outside") << QString("outside");
    }
    void createdPathSelectsVisibleFolder() {
        QFETCH(QString, pathForm);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("current");
        QVERIFY(QDir().mkdir(source));
        QFile keep(QDir(source).filePath("keep.txt"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.close();
        const QString folder = QStringLiteral("created δ");
        const bool outside = pathForm == "outside";
        const QString target = outside ? fixture.filePath("other") : source;
        if (outside)
            QVERIFY(QDir().mkdir(target));
        const QString destination = QDir(target).filePath(folder);
        const QString entered = pathForm == "relative" ? "./" + folder : destination;

        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend, observerBackend;
        MainWindow window(&backend), observer(&observerBackend);
        observer.show();
        auto *observed = openPanel(observer, 0, target);
        QTRY_COMPARE(observed->rowCount(), outside ? 0 : 1);
        observed->sourcePanel->Set_AutoRefresh_Mode(false);
        window.show();
        auto *view = openPanel(window, 0, source);
        QTRY_COMPARE(view->rowCount(), 1);
        view->setFocus();
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(window, "createFolderAction")->trigger();
        auto *input = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(input && input->isVisible());
        input->setText(entered);
        QTest::keyClick(input, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo(destination).isDir());
        QTRY_VERIFY(row(observed, folder) >= 0);
        QTRY_COMPARE(view->rowCount(), outside ? 1 : 2);
        const int selected = row(view, outside ? "keep.txt" : folder);
        QVERIFY(selected >= 0);
        QCOMPARE(view->currentRow(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(selected, {}));
    }
    void revealsCreatedFolder_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("immediate") << 100;
        QTest::newRow("deferred") << 257;
    }
    void revealsCreatedFolder() {
        QFETCH(int, count);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < count; ++i) {
            QFile entry(fixture.filePath(QString("old-%1.bin").arg(i)));
            QVERIFY(entry.open(QIODevice::WriteOnly));
            QCOMPARE(entry.write("keep bytes"), qint64(10));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == count);
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        view->setCurrentCell(view->rowCount() - 1, 0);
        view->scrollToBottom();
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        const QString name = QStringLiteral("new 文件 with spaces");
        action(window, "createFolderAction")->trigger();
        auto *input = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(input && input->isVisible());
        input->setText(name);
        QTest::keyClick(input, Qt::Key_Return);
        if (count == 257)
            QVERIFY(!panel.ShouldProcessNotifications());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == count + 1);
        QCOMPARE(window.focusWidget(), view);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(), name);
        QVERIFY(view->viewport()->rect().intersects(view->visualRect(view->currentIndex())));
        QVERIFY(QFileInfo::exists(fixture.filePath(name)));
        QFile keep(fixture.filePath("old-0.bin"));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("keep bytes"));
    }
    void navigationDoesNotRetargetDraft_data() {
        QTest::addColumn<bool>("queued");
        QTest::newRow("unsubmitted") << false;
        QTest::newRow("queued-during-refresh") << true;
    }
    void navigationDoesNotRetargetDraft() {
        QFETCH(bool, queued);
        QTemporaryDir source;
        QTemporaryDir destination;
        QVERIFY(source.isValid() && destination.isValid());
        if (queued)
            for (int i = 0; i < 257; ++i) {
                QFile entry(source.filePath(QString("entry-%1.bin").arg(i)));
                QVERIFY(entry.open(QIODevice::WriteOnly));
            }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *view = openPanel(window, 0, source.path());
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        action(window, "createFolderAction")->trigger();
        QPointer<QLineEdit> name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        name->setText("retained draft");
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(view->isEnabled());
        QVERIFY(name && name->isVisible());
        QCOMPARE(name->text(), QString("retained draft"));
        if (queued) {
            action(window, "refreshAction")->trigger();
            QVERIFY(!view->isEnabled());
            QTest::keyClick(name, Qt::Key_Return);
        }
        openPanel(window, 0, destination.path());
        QTRY_VERIFY(view->isEnabled());
        QTRY_VERIFY(!name);
        QTRY_COMPARE(QDir(ToQString(view->sourcePanel->GetCurrentFolderPrefix())).canonicalPath(),
                     QDir(destination.path()).canonicalPath());
        QVERIFY(!QFileInfo::exists(source.filePath("retained draft")));
        QVERIFY(!QFileInfo::exists(destination.filePath("retained draft")));
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        action(window, "createFolderAction")->trigger();
        auto *fresh = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(fresh && fresh->isVisible());
        QVERIFY(fresh->text().isEmpty());
        fresh->setText("new destination folder");
        QTest::keyClick(fresh, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo(destination.filePath("new destination folder")).isDir());
        QVERIFY(!QFileInfo::exists(source.filePath("new destination folder")));
    }
    void failure_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("deferred");
        QTest::addColumn<bool>("retry");
        QTest::addColumn<bool>("switchPane");
        for (const QString language : {"-", "ru"})
            QTest::newRow(qPrintable(language)) << language << false << false << false;
        QTest::newRow("immediate-retry") << QString("-") << false << true << false;
        QTest::newRow("deferred") << QString("-") << true << false << false;
        QTest::newRow("deferred-retry") << QString("-") << true << true << false;
        QTest::newRow("deferred-retry-peer-focus") << QString("-") << true << true << true;
    }
    void failure() {
        QFETCH(QString, language);
        QFETCH(bool, deferred);
        QFETCH(bool, retry);
        QFETCH(bool, switchPane);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile keep(fixture.filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        const int initialRows = deferred ? 257 : 1;
        for (int i = 1; i < initialRows; ++i) {
            QFile entry(fixture.filePath(QString("entry-%1.bin").arg(i)));
            QVERIFY(entry.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QTemporaryDir peerFolder;
        FileTable *peer = nullptr;
        if (switchPane) {
            QVERIFY(peerFolder.isValid());
            action(window, "twoPanelsAction")->trigger();
            peer = openPanel(window, 1, peerFolder.path());
            QTRY_VERIFY(peer->isEnabled());
        }
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == initialRows);
        QVERIFY(WindowActivation::waitForActive(window));
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        const int originalRow = row(view, "keep.bin");
        QVERIFY(originalRow >= 0);
        view->selectRow(originalRow);
        view->setCurrentCell(originalRow, 0);
        view->setFocus();
        action(window, "createFolderAction")->trigger();
        auto *name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        QFile external(fixture.filePath("external.bin"));
        QVERIFY(external.open(QIODevice::WriteOnly));
        QCOMPARE(external.write("external bytes"), qint64(14));
        external.close();
        name->setText("keep.bin");
        QTest::keyClick(name, Qt::Key_Return);
        auto *error = window.findChild<QLabel *>("inlineFolderError");
        QVERIFY(error && error->isVisible() && !error->text().isEmpty());
        QCOMPARE(name->text(), QString("keep.bin"));
        QCOMPARE(name->selectedText(), QString("keep.bin"));
        const QString corrected = QStringLiteral("corrected folder");
        if (deferred)
            QVERIFY(!view->isEnabled());
        if (retry || deferred) {
            QCOMPARE(window.focusWidget(), name);
            QTest::keyClicks(name, corrected);
            QCOMPARE(name->text(), corrected);
            QTest::keyClick(name, Qt::Key_Return);
        }
        if (peer) {
            QTest::mouseClick(peer->viewport(), Qt::LeftButton);
            QCOMPARE(window.focusWidget(), peer);
        }
        if (!retry)
            QTest::keyClick(name, Qt::Key_Escape);
        // Create Folder refreshes after failure and preserves selection.
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == initialRows + 1 + int(retry));
        QTRY_COMPARE(window.focusWidget(), peer ? peer : view);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->currentRow(), row(view, retry ? corrected : QString("keep.bin")));
        QCOMPARE(QFileInfo(fixture.filePath(corrected)).isDir(), retry);
        if (peer)
            QVERIFY(!QFileInfo::exists(peerFolder.filePath(corrected)));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceCreateTests)
#include "source_create_tests.moc"
