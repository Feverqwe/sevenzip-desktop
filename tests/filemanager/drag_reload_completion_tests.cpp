#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/drag_event_position.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QDragEnterEvent>
#include <QDropEvent>

class DragReloadCompletionTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_;

  private slots:
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
    }
    void archiveDropUsesSourceRefresh_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("alternative");
        QTest::addColumn<int>("outcome");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    for (bool alternative : {false, true})
                        for (int outcome : {0, 1, 2})
                            QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5")
                                                         .arg(pane)
                                                         .arg(russian)
                                                         .arg(icons)
                                                         .arg(alternative)
                                                         .arg(outcome)))
                                << pane << russian << icons << alternative << outcome;
    }
    void archiveDropUsesSourceRefresh() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, alternative);
        QFETCH(int, outcome);
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
        const QString focused = QStringLiteral("z focus δ.bin");
        const QString added = QStringLiteral("new drop 文件.txt");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(marked), "marked"));
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(focused), "focused"));
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(added), "dropped"));
        const QString archive = fixture.filePath("drop target.zip");
        const QString peerArchive = fixture.filePath("peer archive.zip");
        QProcess create;
        create.setWorkingDirectory(fixture.path());
        create.start(engine_, {"a", "-tzip", "--", archive, marked, focused});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        QVERIFY(QFile::copy(archive, peerArchive));
        const QByteArray original = SourceExtractFixture::readFile(archive);
        const QByteArray peerBytes = SourceExtractFixture::readFile(peerArchive);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        for (QWidget *widget : {peer, owner}) {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            widget->findChild<QLineEdit *>("archivePath")->setFocus();
            window.openArchive(widget == owner ? archive : peerArchive);
        }
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        QCOMPARE(list->rowCount(), 2);
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        panel.SelectAll(false);
        MarkSelectionForTest(panel, ToUString(marked));
        list->setCurrentCell(1, 0, QItemSelectionModel::NoUpdate);
        if (icons)
            action(window, "viewMode0Action")->trigger();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        QWidget *const focusedWidget = window.focusWidget();
        QVERIFY(focusedWidget && owner->isAncestorOf(focusedWidget));
        CSelectedState selection;
        panel.SaveSelectedState(selection);
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy peerPublication(peerList->model(), &QAbstractItemModel::rowsRemoved);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(fixture.filePath(added))});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool confirmed = false;
        QStringList errors;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &window, [&] {
            QWidget *modal = QApplication::activeModalWidget();
            auto *box = qobject_cast<QMessageBox *>(modal);
            if (!box) {
                if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                    auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                    if (close && close->text() == ToQString(LangString(IDS_CLOSE)))
                        QTimer::singleShot(0, progress, [progress, close] {
                            if (QApplication::activeModalWidget() == progress)
                                close->click();
                        });
                }
                return;
            }
            const auto answer = outcome == 1   ? QMessageBox::No
                                : outcome == 2 ? QMessageBox::Cancel
                                               : QMessageBox::Yes;
            if (auto *button = box->button(answer)) {
                confirmed = true;
                button->click();
            } else if (auto *button = box->button(QMessageBox::Ok)) {
                errors.append(box->text());
                button->click();
            }
        });
        respond.start(1);
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &drop);
        respond.stop();
        QVERIFY(confirmed);
        QCOMPARE(drop.isAccepted(), outcome == 0);
        QVERIFY(errors.isEmpty());
        QCOMPARE(panel.GetFolder(), provider.Interface());
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QVERIFY(peerPublication.isEmpty());
        QCOMPARE(!publication.isEmpty(), outcome == 0);
        QCOMPARE(list->rowCount(), outcome == 0 ? 3 : 2);
        CSelectedState restored;
        panel.SaveSelectedState(restored);
        QCOMPARE(restored.SelectedNames.Size(), selection.SelectedNames.Size());
        QCOMPARE(ToQString(restored.SelectedNames[0]), ToQString(selection.SelectedNames[0]));
        QCOMPARE(ToQString(restored.FocusedName), ToQString(selection.FocusedName));
        QVERIFY(!backend.busy());
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(list->updatesEnabled() && view->isEnabled());
        QCOMPARE(window.focusWidget(), focusedWidget);
        QCOMPARE(SourceExtractFixture::readFile(peerArchive), peerBytes);
        if (outcome != 0)
            QCOMPARE(SourceExtractFixture::readFile(archive), original);
        else {
            QProcess extract;
            extract.start(engine_, {"x", "-so", "--", archive, added});
            QVERIFY(extract.waitForFinished(10000));
            QCOMPARE(extract.exitCode(), 0);
            QCOMPARE(extract.readAllStandardOutput(), QByteArray("dropped"));
        }
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(added)), QByteArray("dropped"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("drag-reload-completion-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
};
QTEST_MAIN(DragReloadCompletionTests)
#include "drag_reload_completion_tests.moc"
