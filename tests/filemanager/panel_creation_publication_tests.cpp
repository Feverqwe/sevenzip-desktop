#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"

class CreationProbeBackend final : public ControlledBackend {
  public:
    QString ProbePath;
    QList<ArchiveEntry> Entries;
    std::function<void()> BeforeProbe;
    std::function<void()> CompleteProbe;
    void openFolder(const QString &archive, const QString &format,
                    const QString &password) override {
        const bool probe =
            QDir::fromNativeSeparators(archive) == QDir::fromNativeSeparators(ProbePath);
        if (probe && BeforeProbe)
            BeforeProbe();
        if (probe && CompleteProbe)
            queueListAction(CompleteProbe);
        else
            queueList(Entries);
        ControlledBackend::openFolder(archive, format, password);
    }
};

class PanelCreationPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_;
    quint32 version_ = 0;

  private slots:
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
#ifdef SEVENZIP_CODEC_MODULE
        ArchiveCodecCatalog catalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
#endif
    }
    void creationRetainsOriginalLoadResultPolicy_data() {
        QTest::addColumn<int>("result");
        QTest::newRow("success") << int(S_OK);
        QTest::newRow("load-error") << int(E_FAIL);
        QTest::newRow("load-cancel") << int(E_ABORT);
    }
    void creationRetainsOriginalLoadResultPolicy() {
        QFETCH(int, result);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CApp app;
        CPanel &panel = app.Panels[0];
        int refreshes = 0;
        bool beforeCreated = false;
        panel.AttachCreationPresentation([&](CPanel &created) {
            created.AttachReloadPresentation([&](const CSelectedState &) {
                ++refreshes;
                beforeCreated = !panel.PanelCreated;
                return HRESULT(result);
            });
            return true;
        });
        COpenResult opened;
        QCOMPARE(panel.Create(nullptr, nullptr, 1000, ToUString(fixture.path()), UString(), nullptr,
                              &app.AppState, false, opened),
                 S_OK);
        QCOMPARE(refreshes, 1);
        QVERIFY(beforeCreated);
        QVERIFY(panel.PanelCreated && panel.IsFSFolder());
    }
    void initialPanelsPublishInSourceCreationOrder_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("late");
        QTest::addColumn<bool>("secondMain");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("large");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QStringList kinds{"transport", "filesystem"};
#ifdef SEVENZIP_CODEC_MODULE
        kinds << "zip" << "7z";
#endif
        for (bool late : {false, true})
            for (unsigned pane : {0u, 1u})
                for (const QString &kind : kinds) {
                    // Original Create starts at panel0 unless NeedOpenArc is true.
                    if (!late && pane == 1 && kind == "filesystem")
                        continue;
                    const QList<bool> mainChoices = !late && kind == "filesystem"
                                                        ? QList<bool>{false, true}
                                                        : QList<bool>{false};
                    for (bool secondMain : mainChoices)
                        for (bool large : {false, true})
                            for (bool russian : {false, true})
                                for (bool icons : {false, true})
                                    QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6-%7")
                                                                 .arg(late)
                                                                 .arg(pane)
                                                                 .arg(kind)
                                                                 .arg(large)
                                                                 .arg(russian)
                                                                 .arg(icons)
                                                                 .arg(secondMain)))
                                        << pane << late << secondMain << kind << large << russian
                                        << icons;
                }
    }
    void initialPanelsPublishInSourceCreationOrder() {
        QFETCH(unsigned, pane);
        QFETCH(bool, late);
        QFETCH(bool, secondMain);
        QFETCH(QString, kind);
        QFETCH(bool, large);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        const bool controlledOpening = kind == "filesystem" || kind == "transport";
        const int count = large ? 301 : 2;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = late ? 1 : 2;
        info.currentPanel = secondMain ? 1 : pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        CListMode modes;
        modes.Panels[0] = modes.Panels[1] = icons ? 0 : 3;
        modes.Save(settingsPath);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source directory 空");
        QVERIFY(QDir().mkdir(root));
        QList<ArchiveEntry> entries;
        QStringList names;
        for (int i = 0; i < count; ++i) {
            const QString name = QString("item-%1 文件.txt").arg(i, 3, 10, QLatin1Char('0'));
            names << name;
            entries << ArchiveEntry{name, qint64(name.toUtf8().size()), 0};
            QVERIFY(SourceExtractFixture::writeFile(QDir(root).filePath(name), name.toUtf8()));
        }
        CreationProbeBackend backend;
        backend.Entries = entries;
        backend.ProbePath = fixture.filePath("second panel.fixture");
        QVERIFY(backend.prepareArchiveFixture(backend.ProbePath));
        QString firstPath = root;
        if (kind != "filesystem") {
            firstPath = fixture.filePath("first panel." + (kind == "transport" ? "fixture" : kind));
            if (kind == "transport")
                QVERIFY(backend.prepareArchiveFixture(firstPath));
            else {
                QProcess create;
                create.setWorkingDirectory(root);
                create.start(engine_, QStringList{"a", "--", firstPath} + names);
                QVERIFY(create.waitForFinished(10000));
                QCOMPARE(create.exitCode(), 0);
                QVERIFY(QFile::remove(backend.ProbePath));
                QVERIFY(QFile::copy(firstPath, backend.ProbePath));
#ifdef SEVENZIP_CODEC_MODULE
                QCOMPARE(
                    backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), version_),
                    S_OK);
#endif
            }
        }
        const QByteArray archiveBefore =
            kind == "filesystem" ? QByteArray() : SourceExtractFixture::readFile(firstPath);
        SavePanelPath(1 - pane, ToUString(backend.ProbePath), settingsPath);
        if (secondMain)
            SavePanelPath(pane, ToUString(firstPath), settingsPath);
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(firstPath);
        startup.NeedOpenArc = kind != "filesystem";
        bool observed = false, started = false;
        int rowsBeforeSecond = -1;
        CMyComPtr<IFolderFolder> firstProvider;
        FileTable *observedList = nullptr;
        backend.BeforeProbe = [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *window = dynamic_cast<MainWindow *>(widget);
                if (!window)
                    continue;
                auto *ui = window->findChild<QWidget *>(QString("filePanel%1").arg(pane));
                if (!ui)
                    continue;
                auto *list = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
                if (!list)
                    continue;
                observed = true;
                observedList = list;
                rowsBeforeSecond = list->rowCount();
                started = rowsBeforeSecond > 0 || !list->updatesEnabled();
                firstProvider = list->sourcePanel->GetFolder();
            }
        };
        QTimer releaseProbe;
        if (secondMain) {
            connect(&releaseProbe, &QTimer::timeout, &backend, [&] {
                if (observedList && observedList->rowCount() == count &&
                    observedList->isEnabled() && observedList->updatesEnabled()) {
                    releaseProbe.stop();
                    backend.completeList(entries);
                }
            });
            backend.CompleteProbe = [&] {
                // Old missing-OnCreate publication must fail its assertion,
                // rather than deadlock waiting for a scan that never started.
                if (!started)
                    backend.completeList(entries);
                else
                    releaseProbe.start(1);
            };
        }
        MainWindow window(&backend, {}, nullptr, secondMain ? nullptr : &startup);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (late) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            auto *list = ui->findChild<FileTable *>("archiveTable");
            QTRY_COMPARE(list->rowCount(), count);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            if (controlledOpening)
                QVERIFY(!observed);
            QVERIFY(!window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane)));
            action(window, "twoPanelsAction")->trigger();
        }
        // The controlled transport exposes the peer-open boundary. Native
        // archives bypass that test backend; verify their published views below.
        if (controlledOpening) {
            QVERIFY(observed);
            QVERIFY2(started, "OnCreate must publish or start its asynchronous publication before "
                              "the next panel opens");
            if (!large)
                QCOMPARE(rowsBeforeSecond, count);
        }
        auto *firstUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *secondUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *first = static_cast<FileTable *>(firstUi->findChild<FileTable *>("archiveTable"));
        auto *second = static_cast<FileTable *>(secondUi->findChild<FileTable *>("archiveTable"));
        for (FileTable *table : {first, second}) {
            QTRY_COMPARE(table->rowCount(), count);
            QTRY_VERIFY(table->isEnabled() && table->updatesEnabled());
            QVERIFY(table->sourcePanel->ShouldProcessNotifications());
            QVERIFY(table->sourcePanel->PanelCreated);
            UInt32 nativeCount = 0;
            QCOMPARE(table->sourcePanel->GetFolder()->GetNumberOfItems(&nativeCount), S_OK);
            QCOMPARE(nativeCount, UInt32(count));
            table->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        const int initialListCalls = backend.listCalls;
        // Source LoadItems reloads the bound provider, including the worker path.
        if (controlledOpening)
            QCOMPARE(first->sourcePanel->GetFolder(), firstProvider.Interface());
        firstProvider = first->sourcePanel->GetFolder();
        CMyComPtr<IFolderFolder> secondProvider = second->sourcePanel->GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>(secondProvider.Interface()));
        QCOMPARE(first->sourcePanel->IsFSFolder(), kind == "filesystem");
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(firstUi->findChild<QListView *>("iconList"))
                  : first;
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        view->setFocus();
        QVERIFY(firstUi->isAncestorOf(window.focusWidget()));
        first->sourcePanel->SelectAll(false);
        MarkSelectionForTest(*first->sourcePanel, ToUString(names[0]));
        first->setCurrentCell(count - 1, 0, QItemSelectionModel::NoUpdate);
        QSignalSpy refreshPublication(first->model(), &QAbstractItemModel::rowsRemoved);
        QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY(!refreshPublication.isEmpty());
        QTRY_COMPARE(first->rowCount(), count);
        QTRY_VERIFY(first->isEnabled() && first->updatesEnabled());
        CSelectedState selection;
        first->sourcePanel->SaveSelectedState(selection);
        QCOMPARE(selection.SelectedNames.Size(), 1u);
        QCOMPARE(ToQString(selection.SelectedNames[0]), names[0]);
        QCOMPARE(ToQString(selection.FocusedName), names.last());
        QCOMPARE(first->sourcePanel->GetFolder(), firstProvider.Interface());
        if (kind == "filesystem" && large) {
            QCOMPARE(QFileInfo(ToQString(GetFolderPath(first->sourcePanel->GetFolder())))
                         .canonicalFilePath(),
                     QFileInfo(root).canonicalFilePath());
        }
        action(window, "twoPanelsAction")->trigger();
        action(window, "twoPanelsAction")->trigger();
        QCOMPARE(second->sourcePanel->GetFolder(), secondProvider.Interface());
        QCOMPARE(second->rowCount(), count);
        QCOMPARE(backend.listCalls, initialListCalls);
        if (kind != "filesystem")
            QCOMPARE(SourceExtractFixture::readFile(firstPath), archiveBefore);
        for (const QString &name : names)
            QCOMPARE(SourceExtractFixture::readFile(QDir(root).filePath(name)), name.toUtf8());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::bounds(*window.findChild<QWidget *>("filePanel0"), window).x() <
                    UiInspection::bounds(*window.findChild<QWidget *>("filePanel1"), window).x());
            for (QWidget *ui : {firstUi, secondUi}) {
                auto *visible =
                    icons ? static_cast<QAbstractItemView *>(ui->findChild<QListView *>("iconList"))
                          : ui->findChild<FileTable *>("archiveTable");
                QVERIFY(visible->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*visible, window));
            }
            QVERIFY(UiInspection::writeReport(
                window,
                QString("panel-creation-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
    }
};
QTEST_MAIN(PanelCreationPublicationTests)
#include "panel_creation_publication_tests.moc"
