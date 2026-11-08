#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/agent/Agent.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "support/drag_event_position.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QScopeGuard>

class NativeDropPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
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
            QVERIFY(UiInspection::writeReport(window, QString("native-drop-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }
    bool dropFile(QAbstractItemView &view, const QString &path) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &enter);
        if (!enter.isAccepted())
            return false;
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &drop);
        return drop.isAccepted();
    }
    void verifyPayload(const QString &archive, const QString &name, const QByteArray &wanted) {
        QProcess extract;
        extract.start(runtime_.engine(), {"x", "-so", "--", archive, name});
        QVERIFY(extract.waitForFinished(10000));
        QCOMPARE(extract.exitCode(), 0);
        QCOMPARE(extract.readAllStandardOutput(), wanted);
    }
    void verifySelection(CPanel &panel, const CSelectedState &selected) {
        CSelectedState restored;
        panel.SaveSelectedState(restored);
        QCOMPARE(restored.SelectedNames.Size(), selected.SelectedNames.Size());
        QCOMPARE(ToQString(restored.SelectedNames[0]), ToQString(selected.SelectedNames[0]));
        QCOMPARE(ToQString(restored.FocusedName), ToQString(selected.FocusedName));
    }

  private slots:
    void initTestCase() {
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime_.enableWriter());
        QVERIFY(seeds_.isValid());
        for (int count : {2, 301})
            QVERIFY(runtime_.createArchive(seeds_.filePath(QString::number(count) + ".7z"), count));
    }
    void boundDropUsesNativeWriterAndSourceRefresh_data() {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("rendered");
        QTest::addColumn<bool>("activeTarget");
        // Four publication results, actual writer failure/Cancel and both
        // confirmation refusals. Representative cases cover both pane roles,
        // languages, views, selection modes and the bounded worker threshold.
        QTest::newRow("success") << 0 << 2 << int(S_OK) << true;
        QTest::newRow("success-property-refusal") << 1 << 301 << int(S_FALSE) << true;
        QTest::newRow("success-property-failure") << 2 << 301 << int(E_FAIL) << true;
        QTest::newRow("success-property-cancel") << 3 << 2 << int(E_ABORT) << true;
        QTest::newRow("writer-failure") << 4 << 301 << int(S_OK) << true;
        QTest::newRow("writer-cancel") << 5 << 2 << int(S_OK) << true;
        QTest::newRow("confirmation-no") << 6 << 2 << int(S_OK) << true;
        QTest::newRow("confirmation-cancel") << 7 << 301 << int(S_OK) << true;
        QTest::newRow("inactive-left-table") << 0 << 2 << int(S_OK) << false;
        QTest::newRow("inactive-right-icons") << 1 << 2 << int(S_OK) << false;
    }
    void boundDropUsesNativeWriterAndSourceRefresh() {
        QFETCH(int, scenario);
        QFETCH(int, count);
        QFETCH(int, rendered);
        QFETCH(bool, activeTarget);
        const unsigned pane = unsigned(scenario % 2);
        const bool russian = scenario == 1 || scenario == 2 || scenario == 4 || scenario == 6;
        const bool icons = scenario % 2;
        const HRESULT written = scenario == 4 ? E_FAIL : scenario == 5 ? E_ABORT : S_OK;
        const bool attempted = scenario < 6;
        const bool succeeded = attempted && written == S_OK;
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
        QVERIFY(fixture.isValid());
#ifdef Q_OS_WIN
        const QString archive = fixture.filePath("native drop archive 空.7z");
#else
        const QString archive = fixture.filePath("native drop <b> archive 空.7z");
#endif
        QVERIFY(QFile::copy(seeds_.filePath(QString::number(count) + ".7z"), archive));
        const QByteArray original = SourceExtractFixture::readFile(archive);
        const QString addedName = QStringLiteral("added drop 文件 δ.bin");
        const QString input = fixture.filePath(addedName);
        QVERIFY(SourceExtractFixture::writeFile(input, "dropped payload"));
        const QString peerPath = fixture.filePath("peer δ");
        QVERIFY(SourceExtractFixture::writeFile(QDir(peerPath).filePath("sentinel.txt"), "peer"));
        SavePanelPath(1 - pane, ToUString(peerPath), settings);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(archive);
        startup.ArcFormat = L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        QtWorkspaceApplication application(*qApp);
        std::unique_ptr<MainWindow> observer;
        FileTable *observed = nullptr;
        if (activeTarget && (scenario == 0 || scenario == 6)) {
            observer = std::make_unique<MainWindow>(&backend, nullptr, nullptr, &startup);
            observer->show();
            auto *observedPane = observer->findChild<QWidget *>(QString("filePanel%1").arg(pane));
            QVERIFY(observedPane);
            observed = observedPane->findChild<FileTable *>("archiveTable");
            QVERIFY(observed);
            QTRY_COMPARE(observed->rowCount(), count);
            QVERIFY(observed->sourcePanel->IsArcFolder());
            observed->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        MainWindow window(&backend, {}, nullptr, &startup);
        const auto resetControls = qScopeGuard([&] { runtime_.clear(); });
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled());
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>(provider.Interface()));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        auto *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        QTRY_COMPARE(window.focusWidget(), view);
        panel.SelectAll(false);
        MarkSelectionForTest(panel, L"item 000 空.bin");
        view->selectionModel()->setCurrentIndex(view->model()->index(1, 0),
                                                QItemSelectionModel::NoUpdate);
        CSelectedState selected;
        panel.SaveSelectedState(selected);
        QCOMPARE(selected.SelectedNames.Size(), 1u);
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy peerPublication(peerList->model(), &QAbstractItemModel::rowsRemoved);
        bool confirmed = false, injected = false, cancelled = false, retrying = false;
        bool literalConfirmation = false;
        QStringList errors;
        QTimer responder, cancelDriver;
        dialogs_.connectResponder(responder, &window, [&] {
            if (!injected && !runtime_.writerReceipts().isEmpty()) {
                injected = true;
                QVERIFY(runtime_.arm(count == 301 ? 257 : 0, retrying ? S_OK : HRESULT(rendered),
                                     count + 1));
                if (retrying || written != E_ABORT)
                    QVERIFY(runtime_.armWriter(retrying ? S_OK : written, false));
            }
            auto *modal = dialogs_.activeDialog();
            if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                if (box->objectName() == "progressCancelQuestion")
                    box->button(QMessageBox::Yes)->click();
                else {
                    const auto answer = !retrying && scenario == 6   ? QMessageBox::No
                                        : !retrying && scenario == 7 ? QMessageBox::Cancel
                                                                     : QMessageBox::Yes;
                    if (auto *button = box->button(answer)) {
                        confirmed = true;
                        literalConfirmation = box->textFormat() == Qt::PlainText &&
                                              box->text().contains(QFileInfo(archive).fileName());
                        button->click();
                    } else {
                        errors << box->text();
                        box->accept();
                    }
                }
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && button->property("languageID").toUInt() == IDS_CLOSE)
                    button->click();
            }
        });
        // Cancel opens a nested question. A separate timer must answer it;
        // the timer slot performing click() cannot re-enter itself.
        connect(&cancelDriver, &QTimer::timeout, &window, [&] {
            if (auto *progress = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && injected && !retrying && written == E_ABORT && !cancelled) {
                    cancelled = true;
                    button->click();
                }
            }
        });
        responder.start(5);
        cancelDriver.start(5);
        if (attempted)
            QVERIFY(runtime_.armWriter(written));
        if (!activeTarget) {
            auto *peerView =
                icons ? static_cast<QAbstractItemView *>(peer->findChild<QListView *>("iconList"))
                      : peerList;
            QVERIFY(peerView && peerView->isVisible());
            peerView->setFocus();
            QTRY_COMPARE(window.focusWidget(), static_cast<QWidget *>(peerView));
        }
        QCOMPARE(dropFile(*view, input), succeeded);
        QVERIFY(confirmed);
        QVERIFY(literalConfirmation);
        QTRY_VERIFY2(list->isEnabled() && list->updatesEnabled() &&
                         panel.ShouldProcessNotifications(),
                     qPrintable(QString("enabled=%1 redraw=%2 notify=%3 rows=%4")
                                    .arg(list->isEnabled())
                                    .arg(list->updatesEnabled())
                                    .arg(panel.ShouldProcessNotifications())
                                    .arg(list->rowCount())));
        QCOMPARE(injected, attempted);
        QCOMPARE(cancelled, written == E_ABORT);
        QCOMPARE(!publication.isEmpty(), succeeded);
        QTRY_COMPARE(list->rowCount(),
                     succeeded && rendered != int(S_OK) ? 0 : count + int(succeeded));
        QCOMPARE(panel.GetFolder(), provider.Interface());
        if (observed) {
            QTRY_COMPARE(observed->rowCount(), count + int(succeeded));
            const auto matches = observed->model()->match(observed->model()->index(0, 0),
                                                          Qt::DisplayRole, addedName);
            QCOMPARE(!matches.isEmpty(), succeeded);
        }
        QCOMPARE(window.focusWidget(), static_cast<QWidget *>(view));
        QVERIFY(peerPublication.isEmpty());
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(SourceExtractFixture::readFile(input), QByteArray("dropped payload"));
        if (!succeeded)
            QCOMPARE(SourceExtractFixture::readFile(archive), original);
        if (succeeded)
            verifyPayload(archive, addedName, "dropped payload");
        if (attempted)
            QVERIFY(runtime_.writerReceipts().contains(
                QByteArray::number(QCoreApplication::applicationPid()) + ' ' +
                QByteArray::number(count + 1) + ' ' +
                QByteArray::number(written == E_FAIL ? UInt32(E_FAIL) : 0u) + '\n'));
        else
            QVERIFY(runtime_.writerReceipts().isEmpty());
        QCOMPARE(errors.size(), written == E_FAIL ? 1 : 0);
        if (!succeeded || rendered == int(S_OK)) {
            verifySelection(panel, selected);
            QVERIFY(!QTest::currentTestFailed());
        }
        inspect(window, "published");
        QVERIFY(runtime_.clear());
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        QTRY_COMPARE(window.focusWidget(), static_cast<QWidget *>(view));
        QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(list->rowCount(), count + int(succeeded));
        QTRY_VERIFY(list->isEnabled() && panel.ShouldProcessNotifications());
        QCOMPARE(panel.GetFolder(), provider.Interface());
        if (!succeeded || rendered == int(S_OK)) {
            verifySelection(panel, selected);
            QVERIFY(!QTest::currentTestFailed());
        }
        inspect(window, "recovered");
        if (!succeeded) {
            window.activateWindow();
            QVERIFY(WindowActivation::waitForActive(window));
            view->setFocus();
            QTRY_COMPARE(window.focusWidget(), static_cast<QWidget *>(view));
            retrying = true;
            confirmed = injected = false;
            errors.clear();
            QVERIFY(runtime_.armWriter(S_OK));
            QVERIFY(dropFile(*view, input));
            QVERIFY(confirmed && injected && errors.isEmpty());
            QTRY_COMPARE(list->rowCount(), count + 1);
            QTRY_VERIFY(list->isEnabled() && panel.ShouldProcessNotifications());
            QCOMPARE(panel.GetFolder(), provider.Interface());
            verifyPayload(archive, addedName, "dropped payload");
            inspect(window, "writer-retried");
        }
        verifyPayload(archive, QStringLiteral("item 000 空.bin"),
                      QStringLiteral("item 000 空.bin").toUtf8());
        if (count == 301)
            verifyPayload(archive, QStringLiteral("item 300 空.bin"),
                          QStringLiteral("item 300 空.bin").toUtf8());
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(SourceExtractFixture::readFile(QDir(peerPath).filePath("sentinel.txt")),
                 QByteArray("peer"));
        responder.stop();
        cancelDriver.stop();
        QVERIFY(runtime_.clear());
        PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
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
QTEST_MAIN(NativeDropPublicationTests)
#include "native_drop_publication_tests.moc"
