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
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class NativeRenamePublicationTests final : public QObject, protected MainWindowTestSupport {
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
                QVERIFY(UiInspection::fullyVisible(*address, window));
                QVERIFY(UiInspection::fullyVisible(*view, window));
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-rename-%1-%2-%3")
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
    void renamePublishesAfterNativeWriter_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("written");
        QTest::addColumn<int>("rendered");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("alternative");
        const int requested = qEnvironmentVariableIntValue("SEVENZIP_RENAME_ROWS");
        for (int count : {2, 301}) {
            if (requested && requested != count)
                continue;
            for (HRESULT written : {S_OK, E_FAIL, E_ABORT})
                for (HRESULT rendered : {S_OK, S_FALSE, E_FAIL, E_ABORT})
                    for (unsigned pane : {0u, 1u})
                        for (bool russian : {false, true})
                            for (bool icons : {false, true})
                                for (bool alternative : {false, true})
                                    QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6-%7")
                                                                 .arg(count)
                                                                 .arg(UInt32(written), 0, 16)
                                                                 .arg(UInt32(rendered), 0, 16)
                                                                 .arg(pane)
                                                                 .arg(russian)
                                                                 .arg(icons)
                                                                 .arg(alternative)))
                                        << count << int(written) << int(rendered) << pane << russian
                                        << icons << alternative;
        }
    }
    void autoRefreshHonorsSourceNotificationGuard() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        QSettings values(settings, QSettings::IniFormat);
        values.setValue("FM/ShowDots", false);
        values.sync();
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = 2;
        info.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath("guard δ.txt"), "guard"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *list = static_cast<FileTable *>(
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(list->rowCount(), 1);
        unsigned guardedStatus = 0;
        list->sourcePanel->AttachStatusPresentation([&](const CPanelStatus &) {
            if (!list->sourcePanel->ShouldProcessNotifications())
                ++guardedStatus;
        });
        const auto resetStatus =
            qScopeGuard([&] { list->sourcePanel->AttachStatusPresentation({}); });
        QTimer *periodic = nullptr;
        for (auto *timer : window.findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly))
            if (timer->interval() == 1000 && timer->isActive()) {
                QVERIFY(!periodic);
                periodic = timer;
            }
        QVERIFY(periodic);
        QSignalSpy ticks(periodic, &QTimer::timeout);
        {
            CPanel::CDisableNotify guard(*list->sourcePanel);
            auto *peerAddress =
                window.findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
            peerAddress->setFocus();
            QTRY_VERIFY(
                window.focusWidget() &&
                window.findChild<QWidget *>("filePanel1")->isAncestorOf(window.focusWidget()));
            auto *sourceAddress =
                window.findChild<QWidget *>("filePanel0")->findChild<QLineEdit *>("archivePath");
            sourceAddress->setFocus();
            QTRY_VERIFY(
                window.focusWidget() &&
                window.findChild<QWidget *>("filePanel0")->isAncestorOf(window.focusWidget()));
            QCOMPARE(guardedStatus, 0u);
            QTRY_VERIFY_WITH_TIMEOUT(!ticks.isEmpty(), 2000);
            QCOMPARE(guardedStatus, 0u);
        }
        QVERIFY(list->sourcePanel->ShouldProcessNotifications());
    }
    void renamePublishesAfterNativeWriter() {
        QFETCH(int, count);
        QFETCH(int, written);
        QFETCH(int, rendered);
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, alternative);
        QVERIFY(runtime_.clear());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QSettings values(settings, QSettings::IniFormat);
        values.setValue("FM/ShowDots", false);
        values.setValue("FM/AlternativeSelection", alternative);
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
        const QString path = fixture.filePath("rename archive 空 with spaces.7z");
        QVERIFY(QFile::copy(seeds_.filePath(QString::number(count) + ".7z"), path));
        const QByteArray original = SourceExtractFixture::readFile(path);
        const QString peerPath = fixture.filePath("peer δ");
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("sentinel.txt"), "peer"));
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
        panel.SetAlternativeSelection(alternative);
        view->setCurrentIndex(view->model()->index(0, 0));
        panel.SelectAll(false);
        MarkSelectionForTest(panel, L"item 000 空.bin");
        bool injected = false, cancelled = false, retrying = false;
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
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (box->objectName() == "progressCancelQuestion")
                    box->button(QMessageBox::Yes)->click();
                else {
                    if (!retrying && HRESULT(written) == E_FAIL &&
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
        const QString renamed = QStringLiteral("renamed 文件 δ.bin");
        const auto edit = [&] {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            view->setFocus();
            QTRY_COMPARE(window.focusWidget(), view);
            const auto operationCount = [&] {
                CRecordVector<UInt32> indices;
                panel.Get_ItemIndices_Operated(indices);
                return indices.Size();
            };
            QTRY_COMPARE(operationCount(), 1u);
            QTRY_VERIFY(action(window, "renameAction")->isEnabled());
            QTest::keyClick(view, Qt::Key_F2);
            QLineEdit *editor = nullptr;
            for (auto *candidate : view->findChildren<QLineEdit *>())
                if (candidate->isVisible())
                    editor = candidate;
            QVERIFY2(editor, qPrintable(QString("retry=%1 row=%2 marks=%3 current=%4")
                                            .arg(retrying)
                                            .arg(list->currentRow())
                                            .arg(view->selectionModel()->selectedRows().size())
                                            .arg(view->currentIndex().column())));
            editor->setText(renamed);
            QTest::keyClick(editor, Qt::Key_Return);
        };
        QVERIFY(runtime_.armWriter(HRESULT(written)));
        responder.start(5);
        driver.start(5);
        QVERIFY(action(window, "renameAction")->isEnabled());
        edit();
        QTRY_VERIFY_WITH_TIMEOUT(injected, 10000);
        QTRY_VERIFY(!QApplication::activeModalWidget());
        QTRY_VERIFY(panel.ShouldProcessNotifications());
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QTRY_COMPARE(list->rowCount(), rendered == int(S_OK) ? count : 0);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(panel.ShouldDisplayItemText());
        QCOMPARE(cancelled, HRESULT(written) == E_ABORT);
        QVERIFY(runtime_.writerReceipts().contains(
            QByteArray::number(QCoreApplication::applicationPid()) + ' ' +
            QByteArray::number(count) + ' ' +
            QByteArray::number(HRESULT(written) == E_FAIL ? UInt32(E_FAIL) : 0u) + '\n'));
        QVERIFY(!runtime_.receipts().isEmpty());
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join('\n')));
        QCOMPARE(writerErrors.size(), HRESULT(written) == E_FAIL ? 1 : 0);
        QCOMPARE(panel.GetFolder(), provider.Interface());
        if (HRESULT(written) != S_OK)
            QCOMPARE(SourceExtractFixture::readFile(path), original);
        const QString wanted =
            HRESULT(written) == S_OK ? renamed : QStringLiteral("item 000 空.bin");
        QCOMPARE(ExternalEditorFixture::contents(path, wanted), QByteArray("item 000 空.bin"));
        if (rendered == int(S_OK)) {
            QTRY_VERIFY(list->currentRow() >= 0);
            QCOMPARE(list->model()->index(list->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                     wanted);
            CRecordVector<UInt32> marks;
            panel.Get_ItemIndices_Selected(marks);
            QCOMPARE(marks.Size(), alternative && HRESULT(written) == S_OK ? 0u : 1u);
        }
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
        inspect(window, "recovered");
        if (HRESULT(written) != S_OK) {
            retrying = true;
            injected = false;
            for (int row = 0; row < list->rowCount(); ++row)
                if (list->model()->index(row, 0).data(Qt::UserRole + 3).toString() ==
                    "item 000 空.bin") {
                    view->setCurrentIndex(view->model()->index(row, 0));
                    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                                      view->visualRect(view->model()->index(row, 0)).center());
                }
            panel.SelectAll(false);
            MarkSelectionForTest(panel, L"item 000 空.bin");
            CRecordVector<UInt32> retryMarks;
            panel.Get_ItemIndices_Selected(retryMarks);
            QCOMPARE(retryMarks.Size(), 1u);
            QVERIFY(runtime_.armWriter(S_OK));
            edit();
            QTRY_VERIFY_WITH_TIMEOUT(injected, 10000);
            QTRY_VERIFY(!QApplication::activeModalWidget());
            QTRY_VERIFY(panel.ShouldProcessNotifications());
            QTRY_COMPARE(list->rowCount(), count);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            QTRY_VERIFY(list->currentRow() >= 0);
            QTRY_COMPARE(
                list->model()->index(list->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                renamed);
            QCOMPARE(ExternalEditorFixture::contents(path, renamed), QByteArray("item 000 空.bin"));
            QCOMPARE(panel.GetFolder(), provider.Interface());
            inspect(window, "writer-retried");
        }
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join('\n')));
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(SourceExtractFixture::readFile(QDir(peerPath).filePath("sentinel.txt")),
                 QByteArray("peer"));
        responder.stop();
        driver.stop();
        QVERIFY(runtime_.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        owner->findChild<QLineEdit *>("archivePath")->setFocus();
        window.openPath(peerPath);
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("timer.txt"), "timer"));
        panel.Set_AutoRefresh_Mode(true);
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QTRY_COMPARE(list->rowCount(), 2);
        panel.Set_AutoRefresh_Mode(false);
    }
};
QTEST_MAIN(NativeRenamePublicationTests)
#include "native_rename_publication_tests.moc"
