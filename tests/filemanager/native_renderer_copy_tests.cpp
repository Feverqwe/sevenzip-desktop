#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QLineEdit>

class NativeRendererCopyTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir archive_;

    void inspectState(MainWindow &window, const QString &state) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned index : {0u, 1u}) {
                auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
                auto *table = owner->findChild<FileTable *>("archiveTable");
                auto *icons = owner->findChild<QListView *>("iconList");
                QWidget *view = table->isVisible() ? static_cast<QWidget *>(table) : icons;
                auto *address = owner->findChild<QLineEdit *>("archivePath");
                QVERIFY(view->isEnabled() && address->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*view, window));
                QVERIFY(UiInspection::fullyVisible(*address, window));
                QVERIFY(UiInspection::bounds(*address, window).bottom() <
                        UiInspection::bounds(*view, window).top());
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-renderer-copy-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), state)
                                                          .arg(size.width())));
        }
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const QString engineName = "7z.exe";
#else
        const QString engineName = "7z";
#endif
        if (!QFileInfo::exists(
                QDir(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME)).filePath(engineName)))
            QSKIP("Run scripts/build-codec-runtime.sh for the official native module");
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(archive_.isValid());
        QVERIFY(runtime_.createArchive(archive_.filePath("native.7z"), 301));
    }
    void copyCompletionWaitsForNativeReload_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("move");
        QTest::addColumn<int>("result");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("alternative");
        for (unsigned pane : {0u, 1u})
            for (bool move : {false, true})
                for (HRESULT result : {S_FALSE, E_FAIL, E_ABORT})
                    for (bool russian : {false, true})
                        for (bool icons : {false, true})
                            for (bool alternative : {false, true})
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(pane)
                                                             .arg(move)
                                                             .arg(quint32(result), 0, 16)
                                                             .arg(russian)
                                                             .arg(icons)
                                                             .arg(alternative)))
                                    << pane << move << int(result) << russian << icons
                                    << alternative;
    }
    void copyCompletionWaitsForNativeReload() {
        QFETCH(unsigned, pane);
        QFETCH(bool, move);
        QFETCH(int, result);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, alternative);
        QVERIFY(runtime_.clear());
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowDots", false);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.sync();
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        CListMode modes;
        modes.Panels[0] = modes.Panels[1] = icons ? 0 : 3;
        modes.Save(settingsPath);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("source folder 空");
        const QString output = fixture.filePath("output folder δ");
        const QString name = QStringLiteral("payload 文件 δ.bin");
        const QByteArray bytes("copy\0native", 11);
        QVERIFY(QDir().mkpath(output));
        QVERIFY(SourceExtractFixture::writeFile(QDir(input).filePath(name), bytes));
        SavePanelPath(1 - pane, ToUString(input), settingsPath);
        const QString archivePath = fixture.filePath("native archive δ.7z");
        QVERIFY(QFile::copy(archive_.filePath("native.7z"), archivePath));
        const QByteArray before = SourceExtractFixture::readFile(archivePath);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(archivePath);
        startup.ArcFormat = L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        auto *sourceUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *source = static_cast<FileTable *>(sourceUi->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(peer->rowCount(), 301);
        QTRY_VERIFY(peer->isEnabled() && peer->updatesEnabled());
        QCOMPARE(source->rowCount(), 1);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        source->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> provider = peer->sourcePanel->GetFolder();
        sourceUi->findChild<QLineEdit *>("archivePath")->setFocus();
        source->sourcePanel->SelectAll(true);
        QAbstractItemView *sourceView =
            icons ? static_cast<QAbstractItemView *>(sourceUi->findChild<QListView *>("iconList"))
                  : source;
        sourceView->setFocus();
        QTRY_VERIFY(sourceView->hasFocus());
        bool submitted = false;
        QStringList messages;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                dialog && dialog->isVisible() && !submitted) {
                submitted = true;
                dialog->findChild<QLineEdit *>("copyDestination")->setText(output);
                dialog->accept();
            }
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    messages << box->text();
                    box->accept();
                }
        });
        QVERIFY(runtime_.arm(257, HRESULT(result)));
        driver.start(1);
        QTest::keyClick(sourceView, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(submitted);
        QTRY_COMPARE(peer->rowCount(), 0);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        // Refresh availability belongs to the active source. The peer reload
        // publishes asynchronously and restores redraw at its own completion.
        QTRY_VERIFY(peer->isEnabled() && peer->updatesEnabled());
        driver.stop();
        // Original OnCopy ignores the final RefreshListCtrl HRESULT. Explicit
        // OnReload diagnostics are covered by the separate renderer suite.
        QVERIFY(messages.isEmpty());
        QVERIFY(peer->isEnabled() && peer->updatesEnabled());
        QVERIFY(source->isEnabled() && source->updatesEnabled());
        QVERIFY(peer->sourcePanel->ShouldProcessNotifications());
        QVERIFY(source->sourcePanel->ShouldProcessNotifications());
        QVERIFY(runtime_.receipts().contains("257 " + QByteArray::number(quint32(result))));
        QCOMPARE(peer->rowCount(), 0);
        QCOMPARE(peer->sourcePanel->GetFolder(), provider.Interface());
        QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(name)), bytes);
        QCOMPARE(QFileInfo::exists(QDir(input).filePath(name)), !move);
        if (!move)
            QCOMPARE(SourceExtractFixture::readFile(QDir(input).filePath(name)), bytes);
        QCOMPARE(SourceExtractFixture::readFile(archivePath), before);
        UStringVector marked;
        source->sourcePanel->GetSelectedNames(marked);
        QVERIFY(marked.IsEmpty());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(sourceView->hasFocus());
        QCOMPARE(QFileInfo(window.windowTitle()).canonicalFilePath(),
                 QFileInfo(input).canonicalFilePath());
        inspectState(window, "failed");
        QVERIFY(runtime_.clear());
        peer->sourcePanel->OnReload();
        QTRY_COMPARE(peer->rowCount(), 301);
        QTRY_VERIFY(peer->isEnabled() && peer->updatesEnabled());
        QVERIFY(peer->sourcePanel->ShouldProcessNotifications());
        QCOMPARE(peer->sourcePanel->GetFolder(), provider.Interface());
        QCOMPARE(SourceExtractFixture::readFile(archivePath), before);
        inspectState(window, "recovered");
    }
};

QTEST_MAIN(NativeRendererCopyTests)
#include "native_renderer_copy_tests.moc"
