#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"

class ArchiveReloadCommandTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_;
    quint32 version_ = 0;

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official native archive module unavailable");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        ArchiveCodecCatalog catalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
#endif
    }
    void refreshLoadsBoundProvider_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("alternative");
        QTest::addColumn<bool>("keyboard");
        for (const QString &format : {QString("zip"), QString("7z")})
            for (unsigned pane : {0u, 1u})
                for (bool russian : {false, true})
                    for (bool icons : {false, true})
                        for (bool alternative : {false, true})
                            for (bool keyboard : {false, true})
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(format)
                                                             .arg(pane)
                                                             .arg(russian)
                                                             .arg(icons)
                                                             .arg(alternative)
                                                             .arg(keyboard)))
                                    << format << pane << russian << icons << alternative
                                    << keyboard;
    }
    void refreshLoadsBoundProvider() {
        QFETCH(QString, format);
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, alternative);
        QFETCH(bool, keyboard);
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString marked = QStringLiteral("a marked 文件.txt");
        const QString focusedName = QStringLiteral("z focused δ.bin");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(marked), "marked"));
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(focusedName), "focused"));
        const QString archive = fixture.filePath("source archive." + format);
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-t" + format, "--", archive, marked, focusedName});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray archiveBytes = SourceExtractFixture::readFile(archive);
        QVERIFY(!archiveBytes.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
#ifdef SEVENZIP_CODEC_MODULE
        QCOMPARE(backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), version_),
                 S_OK);
#endif
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        owner->findChild<QLineEdit *>("archivePath")->setFocus();
        window.openArchive(archive);
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(list->rowCount(), 2);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        QCOMPARE(panel._mySelectMode, alternative);
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>((IFolderFolder *)provider));
        panel.SelectAll(false);
        MarkSelectionForTest(panel, ToUString(marked));
        list->setCurrentCell(1, 0, QItemSelectionModel::NoUpdate);
        if (alternative)
            list->selectionModel()->select(list->model()->index(1, 0),
                                           QItemSelectionModel::ClearAndSelect |
                                               QItemSelectionModel::Rows);
        CSelectedState selection;
        panel.SaveSelectedState(selection);
        QCOMPARE(selection.SelectedNames.Size(), 1u);
        QCOMPARE(ToQString(selection.SelectedNames[0]), marked);
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        const int peerRows = peerList->rowCount();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        QWidget *const focused = window.focusWidget();
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        if (keyboard)
            QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        else
            action(window, "refreshAction")->trigger();
        QVERIFY(!publication.isEmpty());
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QCOMPARE(list->rowCount(), 2);
        CSelectedState restored;
        panel.SaveSelectedState(restored);
        QCOMPARE(restored.SelectedNames.Size(), selection.SelectedNames.Size());
        QCOMPARE(ToQString(restored.SelectedNames[0]), marked);
        QCOMPARE(ToQString(restored.FocusedName), ToQString(selection.FocusedName));
        publication.clear();
        QCOMPARE(panel.RefreshListCtrl(restored), S_OK);
        QVERIFY(!publication.isEmpty());
        QCOMPARE(panel.GetFolder(), provider.Interface());
        QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(list->updatesEnabled() && view->isEnabled());
        QCOMPARE(window.focusWidget(), focused);
        QCOMPARE(peerList->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peerList->rowCount(), peerRows);
        QCOMPARE(SourceExtractFixture::readFile(archive), archiveBytes);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(marked)), QByteArray("marked"));
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(focusedName)),
                 QByteArray("focused"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::fullyVisible(*peerList, window));
            QVERIFY(UiInspection::writeReport(window, QString("archive-reload-command-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
};
QTEST_MAIN(ArchiveReloadCommandTests)
#include "archive_reload_command_tests.moc"
