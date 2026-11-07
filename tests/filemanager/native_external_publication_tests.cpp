#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/support/native_property_failure_runtime.h"
#include "support/panel_keys.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class NativeExternalPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir seeds_;

    void inspect(MainWindow &window, const QString &phase) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned pane : {0u, 1u}) {
                auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
                auto *address = owner->findChild<QLineEdit *>("archivePath");
                auto *table = owner->findChild<FileTable *>("archiveTable");
                QWidget *view = table->isVisible() ? static_cast<QWidget *>(table)
                                                   : owner->findChild<QListView *>("iconList");
                QVERIFY(address->isEnabled() && view->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(address), window));
                QVERIFY(UiInspection::fullyVisible(*view, window));
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-external-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const QString engine = "7z.exe";
#else
        const QString engine = "7z";
#endif
        if (!QFileInfo::exists(QDir(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME)).filePath(engine)))
            QSKIP("Prepare the official native codec runtime first");
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime_.enableWriter());
        for (int count : {2, 301})
            QVERIFY(runtime_.createArchive(seeds_.filePath(QString::number(count) + ".7z"), count));
    }
    void externalUpdatePublishesAfterNativeWriter_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("written");
        QTest::addColumn<int>("rendered");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        const int requested = qEnvironmentVariableIntValue("SEVENZIP_EXTERNAL_ROWS");
        for (int count : {2, 301}) {
            if (requested && requested != count)
                continue;
            for (HRESULT written : {S_OK, E_FAIL, E_ABORT})
                for (HRESULT rendered : {S_OK, S_FALSE, E_FAIL, E_ABORT})
                    for (unsigned pane : {0u, 1u})
                        for (bool russian : {false, true})
                            for (bool icons : {false, true})
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(count)
                                                             .arg(UInt32(written), 0, 16)
                                                             .arg(UInt32(rendered), 0, 16)
                                                             .arg(pane)
                                                             .arg(russian)
                                                             .arg(icons)))
                                    << count << int(written) << int(rendered) << pane << russian
                                    << icons;
        }
    }
    void externalUpdatePublishesAfterNativeWriter() {
        QFETCH(int, count);
        QFETCH(int, written);
        QFETCH(int, rendered);
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
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
        const QString path = fixture.filePath("archive 空 with spaces.7z");
        QVERIFY(QFile::copy(seeds_.filePath(QString::number(count) + ".7z"), path));
        const QByteArray original = SourceExtractFixture::readFile(path);
        const QString peerPath = fixture.filePath("peer δ");
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("sentinel.txt"), "peer"));
        SavePanelPath(1 - pane, ToUString(peerPath), settings);
        const QString control = fixture.filePath("editor control");
        const auto finishEditor =
            qScopeGuard([&] { ExternalEditorFixture::write(control + ".exit", "exit"); });
        ExternalEditorFixture::configure(control);
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
        const auto resetControls = qScopeGuard([&] { runtime_.clear(); });
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled());
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        auto *agent = dynamic_cast<CAgentFolder *>(provider.Interface());
        QVERIFY(agent);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource());
        QVERIFY(resource && resource->HasNativeUpdateService());
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        auto *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        QTRY_COMPARE(window.focusWidget(), view);
        QTest::keyClick(view, Qt::Key_Home);
        view->setCurrentIndex(view->model()->index(0, 0));
        panel.SelectAll(false);
        MarkSelectionForTest(panel, L"item 000 空.bin");
        CRecordVector<UInt32> initialMarks;
        panel.Get_ItemIndices_Selected(initialMarks);
        QCOMPARE(initialMarks.Size(), 1u);
        QVERIFY(action(window, "editFileAction")->isEnabled());
        bool prompted = false, injected = false, cancelled = false, failed = false,
             retrying = false;
        bool closeRejectedDuringPublication = false;
        const bool checkPublicationClose =
            count == 2 && HRESULT(written) == S_OK && HRESULT(rendered) == S_OK;
        connect(list->model(), &QAbstractItemModel::rowsRemoved, &window, [&] {
            if (checkPublicationClose && injected && !closeRejectedDuringPublication &&
                !QApplication::activeModalWidget()) {
                closeRejectedDuringPublication = !window.close();
                QVERIFY(closeRejectedDuringPublication);
            }
        });
        QStringList unexpected, writerErrors;
        QTimer responder, driver;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (!injected && !runtime_.writerReceipts().isEmpty()) {
                injected = true;
                QVERIFY(runtime_.arm(count == 301 ? 257 : 0, retrying ? S_OK : HRESULT(rendered),
                                     count));
                if (retrying || HRESULT(written) != E_ABORT)
                    QVERIFY(runtime_.armWriter(retrying ? S_OK : HRESULT(written), false));
            }
            auto *modal = QApplication::activeModalWidget();
            if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                if (box->objectName() == "externalUpdatePrompt") {
                    prompted = true;
                    box->button(QMessageBox::Yes)->click();
                } else if (box->objectName() == "progressCancelQuestion") {
                    box->button(QMessageBox::Yes)->click();
                } else {
                    if (box->objectName() == "externalUpdateError")
                        failed = true;
                    else if (HRESULT(written) == E_FAIL &&
                             dynamic_cast<CProgressDialog *>(box->parentWidget()))
                        writerErrors << box->text();
                    else
                        unexpected << box->text();
                    box->accept();
                }
            }
        });
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto *progress =
                    dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && button->property("languageID").toUInt() == IDS_CLOSE)
                    button->click();
                else if (button && injected && !retrying && HRESULT(written) == E_ABORT &&
                         !cancelled) {
                    cancelled = true;
                    button->click();
                }
            }
        });
        responder.start(5);
        driver.start(5);
        action(window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary =
            QString::fromUtf8(ExternalEditorFixture::read(control + ".ready"));
        auto *peerAddress = peer->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(peerAddress);
        QWidget *const focused = window.focusWidget();
        QVERIFY(focused && peer->isAncestorOf(focused));
        QVERIFY(runtime_.armWriter(HRESULT(written)));
        QVERIFY(ExternalEditorFixture::write(control, "change"));
        QVERIFY(ExternalEditorFixture::write(control + ".exit", "exit"));
        QTRY_VERIFY_WITH_TIMEOUT(injected, 10000);
        if (HRESULT(written) == S_OK)
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(temporary), 10000);
        else
            QTRY_VERIFY_WITH_TIMEOUT(failed, 10000);
        QTRY_COMPARE(list->rowCount(), rendered == int(S_OK) ? count : 0);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QTRY_COMPARE(window.focusWidget(), focused);
        QVERIFY(panel.ShouldProcessNotifications());
        if (rendered == int(S_OK)) {
            const auto marks = [&] {
                CRecordVector<UInt32> selected;
                panel.Get_ItemIndices_Selected(selected);
                return selected.Size();
            };
            QTRY_COMPARE(marks(), 1u);
            QTRY_COMPARE(view->selectionModel()->selectedRows().size(), 1);
        }
        QVERIFY(prompted);
        if (checkPublicationClose)
            QVERIFY(closeRejectedDuringPublication);
        QCOMPARE(cancelled, HRESULT(written) == E_ABORT);
        QVERIFY(runtime_.writerReceipts().contains(
            QByteArray::number(QCoreApplication::applicationPid()) + ' ' +
            QByteArray::number(count) + ' ' +
            QByteArray::number(HRESULT(written) == E_FAIL ? UInt32(E_FAIL) : 0u) + '\n'));
        QVERIFY(!runtime_.receipts().isEmpty());
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join('\n')));
        QCOMPARE(writerErrors.size(), HRESULT(written) == E_FAIL ? 1 : 0);
        QCOMPARE(failed, HRESULT(written) != S_OK);
        if (HRESULT(written) != S_OK) {
            QCOMPARE(SourceExtractFixture::readFile(path), original);
            QCOMPARE(ExternalEditorFixture::read(temporary), ExternalEditorFixture::modified);
        }
        const QByteArray wanted = HRESULT(written) == S_OK ? ExternalEditorFixture::modified
                                                           : QByteArray("item 000 空.bin");
        QCOMPARE(ExternalEditorFixture::contents(path, "item 000 空.bin"), wanted);
        QCOMPARE(panel.GetFolder(), provider.Interface());
        inspect(window, "published");
        QVERIFY(runtime_.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        view->setFocus();
        QTRY_COMPARE(window.focusWidget(), view);
        QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(panel.GetFolder(), provider.Interface());
        QVERIFY(panel.ShouldProcessNotifications());
        inspect(window, "recovered");
        if (HRESULT(written) != S_OK) {
            retrying = true;
            prompted = injected = failed = false;
            writerErrors.clear();
            const QString retryControl = control + ".retry";
            const auto finishRetry =
                qScopeGuard([&] { ExternalEditorFixture::write(retryControl + ".exit", "exit"); });
            QVERIFY(runtime_.clear());
            QVERIFY(runtime_.armWriter(S_OK));
            ExternalEditorFixture::configure(retryControl);
            view->setCurrentIndex(view->model()->index(0, 0));
            panel.SelectAll(false);
            MarkSelectionForTest(panel, L"item 000 空.bin");
            QVERIFY(action(window, "editFileAction")->isEnabled());
            action(window, "editFileAction")->trigger();
            QTRY_VERIFY(QFileInfo(retryControl + ".ready").size() > 0);
            const QString retried =
                QString::fromUtf8(ExternalEditorFixture::read(retryControl + ".ready"));
            PanelKeys::focusAddress(peerAddress);
            QVERIFY(ExternalEditorFixture::write(retryControl, "change"));
            QVERIFY(ExternalEditorFixture::write(retryControl + ".exit", "exit"));
            QTRY_VERIFY_WITH_TIMEOUT(injected, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(retried), 10000);
            QTRY_COMPARE(list->rowCount(), count);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            QTRY_COMPARE(window.focusWidget(), focused);
            QVERIFY(prompted && !failed && writerErrors.isEmpty());
            QVERIFY(panel.ShouldProcessNotifications());
            QCOMPARE(ExternalEditorFixture::contents(path, "item 000 空.bin"),
                     ExternalEditorFixture::modified);
            QCOMPARE(ExternalEditorFixture::read(temporary), ExternalEditorFixture::modified);
            QCOMPARE(panel.GetFolder(), provider.Interface());
            inspect(window, "writer-retried");
        }
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(SourceExtractFixture::readFile(QDir(peerPath).filePath("sentinel.txt")),
                 QByteArray("peer"));
        responder.stop();
        driver.stop();
        QVERIFY(runtime_.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
        window.openPath(peerPath);
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("timer.txt"), "timer"));
        panel.Set_AutoRefresh_Mode(true);
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QTRY_COMPARE(list->rowCount(), 2);
        panel.Set_AutoRefresh_Mode(false);
        if (HRESULT(written) != S_OK)
            QVERIFY(QDir(QFileInfo(temporary).absolutePath()).removeRecursively());
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication application(argc, argv);
    NativeExternalPublicationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "native_external_publication_tests.moc"
