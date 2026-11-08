#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/agent/Agent.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class NativeParentPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir seeds_;

    static bool readyToClose(CProgressDialog &progress) {
        auto *button = progress.findChild<QPushButton *>("IDCANCEL");
        return button && button->isEnabled() && button->text() == ToQString(LangString(IDS_CLOSE));
    }

    void renameChild(MainWindow &window, QWidget &owner, FileTable &list, bool icons) {
        auto &panel = *list.sourcePanel;
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view =
            icons ? static_cast<QAbstractItemView *>(owner.findChild<QListView *>("iconList"))
                  : &list;
        view->setFocus();
        QTRY_COMPARE(window.focusWidget(), view);
        const auto index = view->model()->index(0, 0);
        view->setCurrentIndex(index);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                          view->visualRect(index).center());
        panel.SelectAll(false);
        MarkSelectionForTest(panel, L"item 000 空.bin");
        QTRY_VERIFY(action(window, "renameAction")->isEnabled());
        QVERIFY(runtime_.armWriter(S_OK, false));
        QStringList errors;
        QTimer close;
        dialogs_.connectResponder(close, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                errors << box->text();
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && readyToClose(*progress))
                    button->click();
            }
        });
        close.start(5);
        QTest::keyClick(view, Qt::Key_F2);
        QLineEdit *editor = nullptr;
        for (auto *candidate : view->findChildren<QLineEdit *>())
            if (candidate->isVisible())
                editor = candidate;
        QVERIFY(editor);
        const QString name =
            QStringLiteral("renamed nested 文件 δ with spaces and a longer name.bin");
        editor->setText(name);
        QTest::keyClick(editor, Qt::Key_Return);
        auto published = [&] {
            for (int row = 0; row < list.rowCount(); ++row)
                if (list.model()->index(row, 0).data(Qt::UserRole + 3).toString() == name)
                    return true;
            return false;
        };
        QTRY_VERIFY(panel.ShouldProcessNotifications() && list.isEnabled() && published());
        QCOMPARE(list.rowCount(), 2);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QVERIFY(runtime_.writerReceipts().contains(
            QByteArray::number(QCoreApplication::applicationPid()) + " 2 0\n"));
    }

    void inspect(MainWindow &window, const QString &phase) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned pane : {0u, 1u}) {
                auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
                auto *address = owner->findChild<QLineEdit *>("archivePath");
                auto *navigation = PanelKeys::navigationFor(address);
                auto *table = owner->findChild<FileTable *>("archiveTable");
                QWidget *view = table->isVisible() ? static_cast<QWidget *>(table)
                                                   : owner->findChild<QListView *>("iconList");
                QVERIFY(address->isEnabled() && view->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*navigation, window));
                QVERIFY(UiInspection::fullyVisible(*view, window));
                QVERIFY(UiInspection::bounds(*navigation, window).bottom() <
                        UiInspection::bounds(*view, window).top());
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-parent-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }

  private slots:
    void initTestCase() {
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime_.enableWriter());
        QVERIFY(runtime_.createArchive(seeds_.filePath("inner archive 空.7z"), 2));
        for (int count : {2, 301}) {
            const QString path = seeds_.filePath(QString::number(count) + ".7z");
            QVERIFY(runtime_.createArchive(path, count - 1));
            QProcess add;
            add.setWorkingDirectory(seeds_.path());
            add.start(runtime_.engine(), {"a", "-m0=Copy", "--", path, "inner archive 空.7z"});
            QVERIFY(add.waitForFinished(10000));
            QCOMPARE(add.exitCode(), 0);
        }
    }
    void changedParentPublishesAndPreservesRecovery_data() {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("rendered");
        QTest::addColumn<int>("variant");
        // Cover each distinct answer/writer/publication outcome without a matrix.
        // 0/1: No/Cancel, 2/3/4: Yes with writer success/failure/actual Cancel.
        struct Case {
            int scenario;
            HRESULT result;
            int variant;
        };
        for (const Case entry : {Case{0, S_OK, 0}, Case{1, E_FAIL, 3}, Case{2, E_FAIL, 2},
                                 Case{3, S_FALSE, 1}, Case{4, E_ABORT, 0}}) {
            QTest::newRow(
                qPrintable(QString("%1-%2").arg(entry.scenario).arg(UInt32(entry.result), 0, 16)))
                << entry.scenario << int(entry.result) << entry.variant;
        }
    }
    void changedParentPublishesAndPreservesRecovery() {
        QFETCH(int, scenario);
        QFETCH(int, rendered);
        QFETCH(int, variant);
        const int count = variant % 2 ? 301 : 2;
        const unsigned pane = variant % 2;
        const bool icons = variant >= 2;
        const bool russian = variant % 2;
        const HRESULT written = scenario == 3 ? E_FAIL : scenario == 4 ? E_ABORT : S_OK;
        auto answer = scenario == 0   ? QMessageBox::No
                      : scenario == 1 ? QMessageBox::Cancel
                                      : QMessageBox::Yes;
        QVERIFY(runtime_.clear());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QSettings values(settings, QSettings::IniFormat);
        values.setValue("FM/ShowDots", false);
        values.sync();
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settings);
        CListMode modes;
        modes.Panels[0] = modes.Panels[1] = icons ? 0 : 3;
        modes.Save(settings);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("outer 空 with spaces.7z");
        QVERIFY(QFile::copy(seeds_.filePath(QString::number(count) + ".7z"), path));
        const QByteArray original = SourceExtractFixture::readFile(path);
        const QString peerPath = fixture.filePath("peer δ");
        const QString sentinel = QDir(peerPath).filePath("sentinel.txt");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, "peer bytes"));
        SavePanelPath(1 - pane, ToUString(peerPath), settings);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(path);
        startup.ArcFormat = L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_COMPARE(peerList->rowCount(), 1);
        CMyComPtr<IFolderFolder> outer = panel.GetFolder();
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        UInt32 innerIndex = UInt32(-1);
        for (int i = 0; i < count; ++i)
            if (ToQString(panel.GetItemName(i)) == "inner archive 空.7z")
                innerIndex = i;
        QVERIFY(innerIndex != UInt32(-1));
        PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
        QVERIFY(runtime_.forceDisk(true));
        panel.OpenItem(innerIndex, true, false, L"SevenzipFixture7z");
        QTRY_COMPARE(list->rowCount(), 2);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(panel.GetParentFolders().Size(), 2u);
        const QString spool = ToQString(fs2us(panel.GetParentFolders().Back().FilePath));
        QVERIFY(runtime_.clear());
        renameChild(window, *owner, *list, icons);
        QVERIFY(!QTest::currentTestFailed());
        const QByteArray modified = SourceExtractFixture::readFile(spool);
        QVERIFY(!modified.isEmpty());
        QVERIFY(modified.size() !=
                SourceExtractFixture::readFile(seeds_.filePath("inner archive 空.7z")).size());
        QVERIFY(runtime_.clear());
        bool prompted = false, injected = false, cancelled = false;
        QStringList errors;
        QPointer<QMessageBox> closingError;
        QPointer<QPushButton> pendingButton;
        QTimer responder, driver;
        dialogs_.connectResponder(responder, &window, [&] {
            if (scenario >= 2 && !injected && !runtime_.writerReceipts().isEmpty()) {
                injected = true;
                QVERIFY(runtime_.arm(count == 301 ? 257 : 0, HRESULT(rendered), count));
                if (written != E_ABORT)
                    QVERIFY(runtime_.armWriter(written, false));
            }
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                if (box->objectName() == "modifiedArchiveQuestion") {
                    prompted = true;
                    box->button(answer)->click();
                } else if (box->objectName() == "progressCancelQuestion") {
                    closingError = box;
                    box->button(QMessageBox::Yes)->click();
                } else {
                    errors << box->text();
                    closingError = box;
                    box->accept();
                }
            }
        });
        connect(&driver, &QTimer::timeout, &window, [&] {
            // These responders use widget dialogs. A hidden, accepted error
            // can outlive its modal loop without blocking its parent.
            if (closingError && !closingError->isVisible())
                closingError.clear();
            if (closingError || pendingButton)
                return;
            if (auto *progress = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                const bool close = readyToClose(*progress);
                const bool cancel = button && button->text() != ToQString(LangString(IDS_CLOSE)) &&
                                    injected && written == E_ABORT && !cancelled;
                if (!close && !cancel)
                    return;
                pendingButton = button;
                // A native confirmation must not enter from the timer callback
                // that also dispatches its responder. Recheck modal ownership
                // after posting, since a final error may have appeared meanwhile.
                QTimer::singleShot(0, &driver,
                                   [&, button = QPointer<QPushButton>(button),
                                    progress = QPointer<CProgressDialog>(progress), cancel] {
                                       pendingButton.clear();
                                       if (!button || !progress || closingError ||
                                           QApplication::activeModalWidget() != progress)
                                           return;
                                       if (cancel
                                               ? button->text() == ToQString(LangString(IDS_CLOSE))
                                               : !readyToClose(*progress))
                                           return;
                                       if (cancel)
                                           cancelled = true;
                                       button->click();
                                   });
            }
        });
        if (scenario >= 2)
            QVERIFY(runtime_.armWriter(written));
        else
            QVERIFY(runtime_.arm(count == 301 ? 257 : 0, HRESULT(rendered), count));
        responder.start(5);
        driver.start(5);
        action(window, "parentFolderAction")->trigger();
        if (scenario == 1) {
            QVERIFY(prompted);
            QCOMPARE(panel.GetParentFolders().Size(), 2u);
            QCOMPARE(SourceExtractFixture::readFile(spool), modified);
            QCOMPARE(SourceExtractFixture::readFile(path), original);
            answer = QMessageBox::No;
            prompted = false;
            QTRY_VERIFY(action(window, "parentFolderAction")->isEnabled());
            action(window, "parentFolderAction")->trigger();
        }
        QTRY_COMPARE(panel.GetParentFolders().Size(), 1u);
        QTRY_COMPARE(list->rowCount(), rendered == int(S_OK) ? count : 0);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QVERIFY(prompted);
        QCOMPARE(panel.GetFolder(), outer.Interface());
        QVERIFY(panel.ShouldProcessNotifications());
        if (rendered == int(S_OK)) {
            CSelectedState selected;
            panel.SaveSelectedState(selected);
            QVERIFY(selected.FocusedName_Defined);
            QCOMPARE(ToQString(selected.FocusedName), QString("inner archive 空.7z"));
        }
        QCOMPARE(cancelled, written == E_ABORT);
        QCOMPARE(injected, scenario >= 2);
        QCOMPARE(errors.isEmpty(), scenario < 3);
        QVERIFY(!runtime_.receipts().isEmpty());
        if (scenario >= 2)
            QVERIFY(runtime_.writerReceipts().contains(
                QByteArray::number(QCoreApplication::applicationPid()) + ' ' +
                QByteArray::number(count) + ' ' +
                QByteArray::number(written == E_FAIL ? UInt32(E_FAIL) : 0u) + '\n'));
        const bool retained = scenario >= 3;
        QCOMPARE(QFileInfo::exists(spool), retained);
        if (retained)
            QCOMPARE(SourceExtractFixture::readFile(spool), modified);
        if (scenario != 2)
            QCOMPARE(SourceExtractFixture::readFile(path), original);
        QProcess extract;
        extract.start(runtime_.engine(), {"e", "-so", "--", path, "inner archive 空.7z"});
        QVERIFY(extract.waitForFinished(10000));
        QCOMPARE(extract.exitCode(), 0);
        QCOMPARE(extract.readAllStandardOutput(),
                 scenario == 2
                     ? modified
                     : SourceExtractFixture::readFile(seeds_.filePath("inner archive 空.7z")));
        inspect(window, "published");
        QVERIFY(runtime_.clear());
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        PanelKeys::command(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(panel.GetFolder(), outer.Interface());
        inspect(window, "recovered");
        if (retained) {
            // Repeat the actual edit/Parent workflow. The first modified spool
            // remains recoverable even after the new update succeeds.
            innerIndex = UInt32(-1);
            for (int i = 0; i < count; ++i)
                if (ToQString(panel.GetItemName(i)) == "inner archive 空.7z")
                    innerIndex = i;
            QVERIFY(innerIndex != UInt32(-1));
            PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
            QVERIFY(runtime_.forceDisk(true));
            panel.OpenItem(innerIndex, true, false, L"SevenzipFixture7z");
            QTRY_COMPARE(list->rowCount(), 2);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            const QString retrySpool = ToQString(fs2us(panel.GetParentFolders().Back().FilePath));
            QVERIFY(retrySpool != spool);
            QVERIFY(runtime_.clear());
            renameChild(window, *owner, *list, icons);
            QVERIFY(!QTest::currentTestFailed());
            const QByteArray retried = SourceExtractFixture::readFile(retrySpool);
            QVERIFY(!retried.isEmpty());
            QVERIFY(runtime_.clear());
            QVERIFY(runtime_.armWriter(S_OK, false));
            prompted = false;
            errors.clear();
            action(window, "parentFolderAction")->trigger();
            QTRY_COMPARE(panel.GetParentFolders().Size(), 1u);
            QTRY_COMPARE(list->rowCount(), count);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            QVERIFY(prompted);
            QVERIFY(errors.isEmpty());
            QVERIFY(!QFileInfo::exists(retrySpool));
            QCOMPARE(SourceExtractFixture::readFile(spool), modified);
            QCOMPARE(panel.GetFolder(), outer.Interface());
            QVERIFY(panel.ShouldProcessNotifications());
            QVERIFY(runtime_.writerReceipts().contains(
                QByteArray::number(QCoreApplication::applicationPid()) + ' ' +
                QByteArray::number(count) + " 0\n"));
            extract.start(runtime_.engine(), {"e", "-so", "--", path, "inner archive 空.7z"});
            QVERIFY(extract.waitForFinished(10000));
            QCOMPARE(extract.exitCode(), 0);
            QCOMPARE(extract.readAllStandardOutput(), retried);
            inspect(window, "writer-retried");
        }
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("peer bytes"));
        responder.stop();
        driver.stop();
        PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
        window.openPath(peerPath);
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("timer.txt"), "timer"));
        panel.Set_AutoRefresh_Mode(true);
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QTRY_COMPARE(list->rowCount(), 2);
        panel.Set_AutoRefresh_Mode(false);
        if (retained)
            QVERIFY(QDir(QFileInfo(spool).absolutePath()).removeRecursively());
    }
};

QTEST_MAIN(NativeParentPublicationTests)
#include "native_parent_publication_tests.moc"
