#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <atomic>
#include <thread>

namespace {
class CompletingTransfer final : public FileTransferOperation {
    QSemaphore &started_;
    QSemaphore &finish_;
    void run() override {
        started_.release();
        finish_.acquire();
        emit result(false, QStringLiteral("fixture failure"), {});
    }

  public:
    CompletingTransfer(QSemaphore &started, QSemaphore &finish)
        : FileTransferOperation({}, {}, false), started_(started), finish_(finish) {}
};
} // namespace

class LocalTaskLifecycleTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cutUsesThePanelThatIssuedTheCommand() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        const QString file = source + "/file.txt";
        QVERIFY(write(file, "payload"));
        const auto cleanup = qScopeGuard([] { QApplication::clipboard()->clear(); });
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>();
        auto *right = window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>();
        left->setFocus();
        window.openPath(source);
        QTRY_VERIFY(left->isEnabled());
        const auto matches = left->model()->match(left->model()->index(0, 0), Qt::DisplayRole,
                                                  QStringLiteral("file.txt"), 1, Qt::MatchExactly);
        QCOMPARE(matches.size(), 1);
        const int row = matches.front().row();
        left->setCurrentCell(row, 0);
        left->selectRow(row);
        right->setFocus();
        window.openPath(destination);
        QTRY_VERIFY(right->hasFocus());
        QVERIFY(!action(window, "cutFilesAction")->isEnabled());
        left->sourcePanel->EditCut();
        const QList<QUrl> expected{QUrl::fromLocalFile(file)};
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), expected);
        QVERIFY(left->model()->index(row, 0).data(QtPanelList::CutRole).toBool());
        QVERIFY(right->hasFocus());
        QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
        QTRY_VERIFY(left->hasFocus());
        QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
        QTRY_VERIFY(right->hasFocus());
        QVERIFY(action(window, "pasteFilesAction")->isEnabled());
        PanelKeys::command(window.focusWidget(), Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QCOMPARE(read(destination + "/file.txt"), QByteArray("payload"));
        QVERIFY(!QFileInfo::exists(file));
    }
    void transferPublishesChangesWithoutAutoRefresh_data() {
        QTest::addColumn<bool>("linkedDestination");
        QTest::addColumn<bool>("viaClipboard");
        QTest::addColumn<bool>("move");
        QTest::newRow("copy-command") << false << false << false;
        QTest::newRow("move-command") << false << false << true;
        QTest::newRow("clipboard-directory") << false << true << false;
#ifdef Q_OS_UNIX
        QTest::newRow("clipboard-directory-symlink") << true << true << false;
#endif
    }
    void transferPublishesChangesWithoutAutoRefresh() {
        QFETCH(bool, linkedDestination);
        QFETCH(bool, viaClipboard);
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("source 空.txt"));
        const QString destination = fixture.filePath("destination folder");
        QVERIFY(write(source, "source bytes"));
        QVERIFY(QDir().mkpath(destination));
        QString opened = destination;
        if (linkedDestination) {
            opened = fixture.filePath("directory link");
            QVERIFY(QFile::link(destination, opened));
            QVERIFY(QFileInfo(opened).isSymLink());
        }
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend, observerBackend, sourceObserverBackend;
        MainWindow window(&backend), observer(&observerBackend);
        observer.show();
        observer.openPath(destination);
        auto *observed = table(observer);
        QTRY_VERIFY(observed->isEnabled());
        if (linkedDestination) {
            COpenResult openedLink;
            QCOMPARE(
                observed->sourcePanel->BindToPath(ToUString(opened + '/'), UString(), openedLink),
                S_OK);
            QCOMPARE(observed->sourcePanel->RefreshListCtrl(), S_OK);
            QCOMPARE(QDir::cleanPath(ToQString(observed->sourcePanel->GetCurrentFolderPrefix())),
                     opened);
        }
        observed->sourcePanel->Set_AutoRefresh_Mode(false);
        const QString name = QFileInfo(source).fileName();
        std::unique_ptr<MainWindow> sourceObserver;
        if (move) {
            sourceObserver = std::make_unique<MainWindow>(&sourceObserverBackend);
            sourceObserver->show();
            sourceObserver->openPath(fixture.path());
            QTRY_VERIFY(filesystemRow(*sourceObserver, name) >= 0);
            table(*sourceObserver)->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        window.show();
        window.openPath(viaClipboard ? opened : fixture.path());
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        QCOMPARE(window.focusWidget(), view);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        if (viaClipboard) {
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(source)});
            QApplication::clipboard()->setMimeData(mime);
            auto *paste = action(window, "pasteFilesAction");
            QTRY_VERIFY(paste->isEnabled());
            paste->trigger();
        } else {
            const int row = filesystemRow(window, name);
            QVERIFY(row >= 0);
            view->setCurrentCell(row, 0);
            view->selectRow(row);
            QTimer submit;
            connect(&submit, &QTimer::timeout, &window, [&] {
                auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                if (!dialog || !dialog->isVisible())
                    return;
                submit.stop();
                const auto close = qScopeGuard([&] {
                    if (dialog->isVisible())
                        dialog->reject();
                });
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            });
            submit.start(1);
            action(window, move ? "moveAction" : "copyAction")->trigger();
        }
        QTRY_VERIFY(filesystemRow(observer, name) >= 0);
        QTRY_COMPARE(filesystemRow(window, name) >= 0, !move);
        if (sourceObserver)
            QTRY_COMPARE(filesystemRow(*sourceObserver, name), -1);
        QCOMPARE(read(QDir(destination).filePath(name)), QByteArray("source bytes"));
        if (move)
            QVERIFY(!QFileInfo::exists(source));
        else
            QCOMPARE(read(source), QByteArray("source bytes"));
        if (linkedDestination)
            QVERIFY(QFileInfo(opened).isSymLink());
        if (viaClipboard)
            QVERIFY(action(window, "pasteFilesAction")->isEnabled());
        QTRY_COMPARE(window.focusWidget(), view);
    }

    void partialFolderCutRefreshesSourceContents() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto cleanup = qScopeGuard([] { QApplication::clipboard()->clear(); });
        const QString source = fixture.filePath("source/folder");
        const QString destination = fixture.filePath("destination");
        const QString target = destination + "/folder";
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(target));
        QVERIFY(write(source + "/moved.txt", "moved bytes"));
        QVERIFY(write(source + "/skipped.txt", "skipped bytes"));
        QVERIFY(write(target + "/skipped.txt", "existing bytes"));

        QtWorkspaceApplication application(*qApp);
        ControlledBackend sourceBackend, destinationBackend;
        MainWindow sourceWindow(&sourceBackend), destinationWindow(&destinationBackend);
        sourceWindow.show();
        destinationWindow.show();
        sourceWindow.openPath(source);
        destinationWindow.openPath(destination);
        QTRY_VERIFY(filesystemRow(sourceWindow, "moved.txt") >= 0);
        table(sourceWindow)->sourcePanel->Set_AutoRefresh_Mode(false);
        const QList<QUrl> urls{QUrl::fromLocalFile(source)};
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        mime->setData("application/x-kde-cutselection", "1");
        QApplication::clipboard()->setMimeData(mime);

        QTimer drive;
        bool skipped = false;
        dialogs_.connectResponder(drive, &destinationWindow, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (auto *question = dynamic_cast<COverwriteDialog *>(dialog)) {
                skipped = true;
                question->findChild<QPushButton *>("overwriteNo")->click();
            } else if (auto *report = TestProgressDialog::ResultWindow(dialog))
                TestProgressDialog::CloseResult(*report);
        });
        drive.start(5);
        auto *paste = action(destinationWindow, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY(skipped && QtLocalFileTask::Active().isEmpty());
        QCOMPARE(read(target + "/moved.txt"), QByteArray("moved bytes"));
        QCOMPARE(read(target + "/skipped.txt"), QByteArray("existing bytes"));
        QCOMPARE(read(source + "/skipped.txt"), QByteArray("skipped bytes"));
        QVERIFY(!QFileInfo::exists(source + "/moved.txt"));
        QTRY_VERIFY(filesystemRow(sourceWindow, "moved.txt") < 0);
        QVERIFY(filesystemRow(sourceWindow, "skipped.txt") >= 0);
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), urls);
    }

    void gnomeCutAppliesOnlyToMatchingClipboardFiles_data() {
        QTest::addColumn<QString>("metadata");
        QTest::newRow("same-files-different-order") << QString("matching");
        QTest::newRow("same-files-with-a-repeated-url") << QString("repeated");
        QTest::newRow("different-files") << QString("different");
        QTest::newRow("invalid-file-list") << QString("invalid");
    }
    void gnomeCutAppliesOnlyToMatchingClipboardFiles() {
        QFETCH(QString, metadata);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto cleanup = qScopeGuard([] { QApplication::clipboard()->clear(); });
        const QString first = fixture.filePath("first.txt");
        const QString second = fixture.filePath("second.txt");
        const QString unrelated = fixture.filePath("unrelated.txt");
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(write(first, "first") && write(second, "second") && write(unrelated, "other"));
        auto *mime = new QMimeData;
        const auto firstUrl = QUrl::fromLocalFile(first);
        const auto secondUrl = QUrl::fromLocalFile(second);
        const bool repeated = metadata == "repeated";
        const bool matching = metadata == "matching" || repeated;
        mime->setUrls(repeated ? QList{firstUrl, secondUrl, firstUrl} : QList{firstUrl, secondUrl});
        QByteArray gnome = "cut\n";
        if (matching)
            gnome += secondUrl.toEncoded() + '\n' + firstUrl.toEncoded();
        else if (metadata == "different")
            gnome += QUrl::fromLocalFile(unrelated).toEncoded();
        else
            gnome += "https://example.invalid/not-a-file";
        mime->setData("x-special/gnome-copied-files", gnome);
        QApplication::clipboard()->setMimeData(mime);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(destination);
        auto *paste = action(window, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QCOMPARE(read(QDir(destination).filePath("first.txt")), QByteArray("first"));
        QCOMPARE(read(QDir(destination).filePath("second.txt")), QByteArray("second"));
        QCOMPARE(QFileInfo::exists(first), !matching);
        QCOMPARE(QFileInfo::exists(second), !matching);
        QCOMPARE(read(unrelated), QByteArray("other"));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath("unrelated.txt")));
    }

    void partialCutCanBeRetried_data() {
        QTest::addColumn<bool>("replaceClipboard");
        QTest::addColumn<bool>("gnomeOnly");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("retry-skipped-file") << false << false << false;
        QTest::newRow("keep-new-clipboard") << true << false << false;
        QTest::newRow("retry-gnome-cut") << false << true << false;
        QTest::newRow("retry-after-escape") << false << false << true;
    }
    void partialCutCanBeRetried() {
        QFETCH(bool, replaceClipboard);
        QFETCH(bool, gnomeOnly);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto cleanup = qScopeGuard([] { QApplication::clipboard()->clear(); });
        const QString source = fixture.filePath("source");
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        const QString moved = QStringLiteral("01 перенести 空.txt");
        const QString skipped = QStringLiteral("02 пропустить δ.txt");
        QVERIFY(write(source + '/' + moved, "moved bytes"));
        QVERIFY(write(source + '/' + skipped, "skipped bytes"));
        QVERIFY(write(destination + '/' + skipped, "existing bytes"));

        QtWorkspaceApplication application(*qApp);
        ControlledBackend sourceBackend, destinationBackend;
        MainWindow sourceWindow(&sourceBackend), destinationWindow(&destinationBackend);
        sourceWindow.show();
        destinationWindow.show();
        sourceWindow.openPath(source);
        destinationWindow.openPath(destination);
        QTRY_VERIFY(filesystemRow(sourceWindow, moved) >= 0 &&
                    filesystemRow(sourceWindow, skipped) >= 0);
        QTRY_VERIFY(filesystemRow(destinationWindow, skipped) >= 0);
        auto *view = table(sourceWindow);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        table(destinationWindow)->sourcePanel->Set_AutoRefresh_Mode(false);
        for (const auto &name : {moved, skipped})
            view->selectionModel()->select(
                view->model()->index(filesystemRow(sourceWindow, name), 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        action(sourceWindow, "cutFilesAction")->trigger();
        QTRY_COMPARE(QApplication::clipboard()->mimeData()->urls().size(), 2);
        if (gnomeOnly) {
            auto *external = new QMimeData;
            external->setData("x-special/gnome-copied-files",
                              QByteArray("cut\n") +
                                  QUrl::fromLocalFile(source + '/' + moved).toEncoded() + '\n' +
                                  QUrl::fromLocalFile(source + '/' + skipped).toEncoded() + '\n');
            QApplication::clipboard()->setMimeData(external);
        }

        bool answered = false;
        bool retrying = false;
        QTimer drive;
        dialogs_.connectResponder(drive, &destinationWindow, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (auto *question = dynamic_cast<COverwriteDialog *>(dialog)) {
                if (answered)
                    return;
                answered = true;
                if (replaceClipboard)
                    QApplication::clipboard()->setText("new clipboard data");
                if (retrying)
                    question->findChild<QPushButton *>("overwriteYes")->click();
                else if (cancel)
                    QTest::keyClick(question, Qt::Key_Escape);
                else
                    question->findChild<QPushButton *>("overwriteNo")->click();
            } else if (auto *report = TestProgressDialog::ResultWindow(dialog))
                TestProgressDialog::CloseResult(*report);
        });
        drive.start(5);
        auto *paste = action(destinationWindow, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY(answered && QtLocalFileTask::Active().isEmpty());
        QCOMPARE(read(destination + '/' + moved),
                 cancel ? QByteArray() : QByteArray("moved bytes"));
        QCOMPARE(read(source + '/' + moved), cancel ? QByteArray("moved bytes") : QByteArray());
        QCOMPARE(read(destination + '/' + skipped), QByteArray("existing bytes"));
        QCOMPARE(read(source + '/' + skipped), QByteArray("skipped bytes"));
        QTRY_COMPARE(filesystemRow(sourceWindow, moved) >= 0, cancel);
        QTRY_COMPARE(filesystemRow(destinationWindow, moved) >= 0, !cancel);
        QTRY_VERIFY(filesystemRow(sourceWindow, skipped) >= 0);
        const auto stillCut = [&] {
            return view->model()
                ->index(filesystemRow(sourceWindow, skipped), 0)
                .data(QtPanelList::CutRole)
                .toBool();
        };
        QTRY_COMPARE(stillCut(), !replaceClipboard);
        if (replaceClipboard) {
            QCOMPARE(QApplication::clipboard()->text(), QString("new clipboard data"));
            return;
        }
        const auto urls = QApplication::clipboard()->mimeData()->urls();
        const auto skippedUrl = QUrl::fromLocalFile(source + '/' + skipped);
        const auto movedUrl = QUrl::fromLocalFile(source + '/' + moved);
        QCOMPARE(urls.size(), cancel ? 2 : 1);
        QVERIFY(urls.contains(skippedUrl));
        QCOMPARE(urls.contains(movedUrl), cancel);
        answered = false;
        retrying = true;
        QVERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY(answered && QtLocalFileTask::Active().isEmpty());
        QCOMPARE(read(destination + '/' + skipped), QByteArray("skipped bytes"));
        QCOMPARE(read(destination + '/' + moved), QByteArray("moved bytes"));
        QVERIFY(!QFileInfo::exists(source + '/' + moved));
        QVERIFY(!QFileInfo::exists(source + '/' + skipped));
        QTRY_VERIFY(filesystemRow(sourceWindow, skipped) < 0);
        QVERIFY(!paste->isEnabled());
    }
    void cleanup() {
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            for (auto *window : QApplication::topLevelWidgets())
                TestProgressDialog::DismissVisibleResult(window);
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->accept();
            if (auto *question =
                    dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget()))
                question->reject();
        });
        answer.start(5);
        for (auto *task : QtLocalFileTask::Active()) {
            if (auto *copy = task->findChild<FileTransferOperation *>()) {
                copy->cancel();
                copy->wait();
            }
        }
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QApplication::clipboard()->clear();
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void completionRetiresItsOwnStopQuestionBeforeReview() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        QSemaphore started, finish;
        QPointer<CompletingTransfer> worker = new CompletingTransfer(started, finish);
        QPointer<QtLocalFileTask> task = new QtLocalFileTask(*worker, "Copy", "source 空");
        task->BindOwner(*table(window)->sourcePanel, window);
        const auto settle = qScopeGuard([&] {
            finish.release();
            if (worker)
                worker->wait();
            if (task)
                task->StopAndWait();
        });
        QSignalSpy completed(task, &QtLocalFileTask::finished);
        task->Start();
        QTRY_VERIFY(started.available() > 0);
        started.acquire();
        auto *progress =
            dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(progress);
        QPointer<QMessageBox> question;
        bool opened = false, reviewed = false;
        QTimer observe;
        connect(&observe, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                box && box->objectName() == "progressCancelQuestion" && !opened) {
                opened = true;
                question = box;
                finish.release();
            } else if (auto *result =
                           TestProgressDialog::ResultWindow(QApplication::activeModalWidget())) {
                const auto close = qScopeGuard([&] { TestProgressDialog::CloseResult(*result); });
                reviewed = true;
                QVERIFY(!question || !question->isVisible());
                QVERIFY(!result->Sync.Get_Stopped());
                QCOMPARE(TestProgressDialog::ReportText(*result), QString("fixture failure"));
            }
        });
        observe.start(5);
        progress->findChild<QPushButton *>("IDCANCEL")->click();
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(opened && reviewed);
        QVERIFY(!completed[0][0].toBool());
        QVERIFY(!completed[0][1].toBool());
    }
    void overwriteKeepsOwnerUntilAnswered_data() {
        QTest::addColumn<QString>("choice");
        QTest::addColumn<QString>("locale");
        for (const QString &locale : {QString("-"), QString("ru")})
            for (const QString &choice :
                 {QString("overwriteYes"), QString("overwriteNo"), QString("overwriteCancel")})
                QTest::newRow(qPrintable(locale + '-' + choice)) << choice << locale;
    }
    void overwriteKeepsOwnerUntilAnswered() {
        QFETCH(QString, choice);
        QFETCH(QString, locale);
        SaveRegLang(ToUString(locale), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source 空");
        const auto destination = fixture.filePath("destination with spaces");
        const QString name = QStringLiteral("данные.txt");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        const QByteArray incoming("new bytes");
        const QByteArray existing("original bytes");
        QVERIFY(write(source + '/' + name, incoming));
        QVERIFY(write(destination + '/' + name, existing));
        QtWorkspaceApplication application(*qApp);
        ControlledBackend sourceBackend, destinationBackend;
        auto sourceWindow = std::make_unique<MainWindow>(&sourceBackend);
        auto destinationWindow = std::make_unique<MainWindow>(&destinationBackend);
        sourceWindow->show();
        destinationWindow->show();
        sourceWindow->openPath(source);
        destinationWindow->openPath(destination);
        QTRY_VERIFY(filesystemRow(*sourceWindow, name) >= 0);
        QTRY_VERIFY(filesystemRow(*destinationWindow, name) >= 0);
        auto *destinationView = table(*destinationWindow);
        destinationView->sourcePanel->Set_AutoRefresh_Mode(false);
        const int sizeColumn = destinationView->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        const auto listedSize = [&] {
            return destinationView->model()
                ->index(filesystemRow(*destinationWindow, name), sizeColumn)
                .data(Qt::UserRole + 2)
                .toLongLong();
        };
        QCOMPARE(listedSize(), qint64(existing.size()));
        const int row = filesystemRow(*sourceWindow, name);
        QVERIFY(row >= 0);
        table(*sourceWindow)->selectRow(row);
        table(*sourceWindow)->setCurrentCell(row, 0);
        auto *cut = action(*sourceWindow, "cutFilesAction");
        QVERIFY(cut->isEnabled());
        cut->trigger();
        const QPointer<const QMimeData> original(QApplication::clipboard()->mimeData());
        auto *paste = action(*destinationWindow, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        const auto tasks = QtLocalFileTask::Active();
        QCOMPARE(tasks.size(), 1);
        QSignalSpy finished(tasks.first(), &QtLocalFileTask::finished);
        QSignalSpy published(tasks.first(), &QtLocalFileTask::transferOutput);
        bool answered = false, closed = false, retained = false, contextModal = false;
        bool reviewed = false;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &application, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *question = dynamic_cast<COverwriteDialog *>(modal);
                question && !answered && dialogs_.ready(*question)) {
                answered = true;
                contextModal = question->windowModality() == Qt::WindowModal;
                QVERIFY(UiInspection::writeReport(*question,
                                                  "local-task-question-" + locale + '-' + choice));
                closed = sourceWindow->close() && !destinationWindow->close();
                sourceWindow.reset();
                retained = question->isVisible() && destinationWindow->isVisible();
                question->findChild<QPushButton *>(choice)->click();
            } else if (auto *report = TestProgressDialog::ResultWindow(modal);
                       report && !reviewed && dialogs_.ready(*report)) {
                reviewed = true;
                drive.stop();
                contextModal &= report->windowModality() == Qt::WindowModal;
                QVERIFY(UiInspection::writeReport(*report,
                                                  "local-task-result-" + locale + '-' + choice));
                QTimer::singleShot(150, report, [&, report] {
                    retained &= report->isVisible() && !QtLocalFileTask::Active().isEmpty() &&
                                QApplication::clipboard()->mimeData() == original;
                    TestProgressDialog::CloseResult(*report);
                });
            }
        });
        drive.start(5);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        drive.stop();
        QVERIFY(answered && closed && retained && contextModal);
        QCOMPARE(reviewed, choice != "overwriteYes");
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][0].toBool(), choice != "overwriteCancel");
        QCOMPARE(finished[0][1].toBool(), choice == "overwriteCancel");
        QCOMPARE(published.size(), choice == "overwriteYes" ? 1 : 0);
        const auto &expected = choice == "overwriteYes" ? incoming : existing;
        QCOMPARE(read(destination + '/' + name), expected);
        QTRY_COMPARE(listedSize(), qint64(expected.size()));
        QCOMPARE(QFileInfo::exists(source + '/' + name), choice != "overwriteYes");
        if (choice == "overwriteYes")
            QVERIFY(!QApplication::clipboard()->mimeData() ||
                    !QApplication::clipboard()->mimeData()->hasUrls());
        else {
            QCOMPARE(QApplication::clipboard()->mimeData(), original.data());
            QCOMPARE(read(source + '/' + name), QByteArray("new bytes"));
        }
    }
    void destroyingOwnerDuringModalDoesNotPublishAfterClose_data() {
        QTest::addColumn<bool>("result");
        QTest::newRow("overwrite") << false;
        QTest::newRow("result") << true;
    }
    void destroyingOwnerDuringModalDoesNotPublishAfterClose() {
        QFETCH(bool, result);
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source 空");
        const auto destination = fixture.filePath("destination with spaces");
        const QString name = QStringLiteral("данные.txt");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        QVERIFY(write(source + '/' + name, "new bytes"));
        QVERIFY(write(destination + '/' + name, "old bytes"));
        ControlledBackend sourceBackend, destinationBackend;
        MainWindow peer(&sourceBackend);
        auto owner = std::make_unique<MainWindow>(&destinationBackend);
        peer.show();
        owner->show();
        peer.openPath(source);
        owner->openPath(destination);
        QTRY_VERIFY(filesystemRow(peer, name) >= 0);
        QTRY_VERIFY(filesystemRow(*owner, name) >= 0);
        const int row = filesystemRow(peer, name);
        QVERIFY(row >= 0);
        table(peer)->selectRow(row);
        table(peer)->setCurrentCell(row, 0);
        action(peer, "cutFilesAction")->trigger();
        const QPointer<const QMimeData> original(QApplication::clipboard()->mimeData());
        auto *paste = action(*owner, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        const auto tasks = QtLocalFileTask::Active();
        QCOMPARE(tasks.size(), 1);
        const QPointer<FileTransferOperation> worker =
            tasks.first()->findChild<FileTransferOperation *>();
        QVERIFY(worker);
        QSignalSpy completed(tasks.first(), &QtLocalFileTask::transferCompleted);
        QSignalSpy finished(tasks.first(), &QtLocalFileTask::finished);
        bool destroyed = false, joined = false;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &peer, [&] {
            if (destroyed)
                return;
            auto *question = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget());
            if (question && dialogs_.ready(*question) && result) {
                question->findChild<QPushButton *>("overwriteNo")->click();
                return;
            }
            auto *report = TestProgressDialog::ResultWindow(QApplication::activeModalWidget());
            if (result ? (!report || !dialogs_.ready(*report))
                       : (!question || !dialogs_.ready(*question)))
                return;
            destroyed = true;
            owner.reset();
            joined = !worker || !worker->isRunning();
        });
        drive.start(5);
        QTRY_VERIFY_WITH_TIMEOUT(destroyed, 10000);
        drive.stop();
        QVERIFY(joined);
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QCOMPARE(completed.size(), result ? 1 : 0);
        if (result)
            QVERIFY(!completed[0][0].toBool());
        QVERIFY(finished.isEmpty());
        QVERIFY(peer.isVisible());
        QTRY_VERIFY(action(peer, "createFolderAction")->isEnabled());
        QCOMPARE(QApplication::clipboard()->mimeData(), original.data());
        QCOMPARE(read(source + '/' + name), QByteArray("new bytes"));
        QCOMPARE(read(destination + '/' + name), QByteArray("old bytes"));
    }
    void destroyingOwnerFromCancelQuestionJoinsBeforeReturning() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source.txt");
        const auto destination = fixture.filePath("output");
        QVERIFY(write(source, "original bytes"));
        QVERIFY(QDir().mkpath(destination));
        ControlledBackend ownerBackend, peerBackend;
        auto owner = std::make_unique<MainWindow>(&ownerBackend);
        MainWindow peer(&peerBackend);
        owner->show();
        peer.show();
        owner->openPath(fixture.path());
        peer.openPath(fixture.path());
        QSemaphore entered;
        auto hold = std::make_shared<std::atomic<bool>>(true);
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [hold, &entered](const QString &, const QString &) {
            entered.release();
            while (*hold)
                QThread::msleep(1);
            return false;
        };
        const QPointer<FileTransferOperation> worker =
            new FileTransferOperation({source}, destination, true, nullptr, adapter);
        const auto release = qScopeGuard([&] {
            *hold = false;
            if (worker)
                worker->wait();
        });
        auto *task = new QtLocalFileTask(*worker, "Move", destination);
        task->BindOwner(*table(*owner)->sourcePanel, *owner);
        QSignalSpy completed(task, &QtLocalFileTask::transferCompleted);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        task->Start();
        QVERIFY(entered.tryAcquire(1, 2000));
        auto *progress = owner->findChild<QDialog *>("progressDialog");
        QVERIFY(progress);
        bool destroyed = false, joined = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &peer, [&] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!question || destroyed || question->objectName() != "progressCancelQuestion" ||
                !dialogs_.ready(*question))
                return;
            destroyed = true;
            std::thread releaseWorker([hold] {
                QThread::msleep(50);
                *hold = false;
            });
            owner.reset();
            releaseWorker.join();
            joined = !worker || !worker->isRunning();
        });
        answer.start(5);
        progress->findChild<QPushButton *>("IDCANCEL")->click();
        answer.stop();
        QVERIFY(destroyed && joined);
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QVERIFY(completed.isEmpty() && finished.isEmpty());
        QCOMPARE(read(source), QByteArray("original bytes"));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath("source.txt")));
        QVERIFY(peer.isVisible());
    }
    void lateStopDoesNotHideTransferFailure_data() {
        QTest::addColumn<bool>("nativeQuestion");
        QTest::newRow("ready") << false;
        QTest::newRow("native-question-without-widget-modal") << true;
    }
    void lateStopDoesNotHideTransferFailure() {
        QFETCH(bool, nativeQuestion);
        QTemporaryDir fixture;
        auto *worker =
            new FileTransferOperation({fixture.filePath("missing input")}, fixture.path(), false);
        auto *task = new QtLocalFileTask(*worker, "Copy", fixture.filePath("missing input"));
        connect(
            worker, &FileTransferOperation::result, worker, [worker] { worker->cancel(); },
            Qt::DirectConnection);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        bool shown = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            for (auto *window : QApplication::topLevelWidgets())
                if (auto *report = TestProgressDialog::ResultWindow(window);
                    report && report->isVisible()) {
                    shown = true;
                    TestProgressDialog::CloseResult(*report);
                }
        });
        answer.start(5);
        std::unique_ptr<QtTaskPresentation::QuestionScope> question;
        if (nativeQuestion)
            question = std::make_unique<QtTaskPresentation::QuestionScope>();
        task->Start();
        if (question) {
            QVERIFY(worker->wait(10000));
            QTest::qWait(100);
            QVERIFY(finished.isEmpty());
            QVERIFY(!shown);
            question.reset(); // Presentation must resume without a focusChanged event.
        }
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(shown);
        QVERIFY(!finished[0][0].toBool());
        QVERIFY(!finished[0][1].toBool());
        QVERIFY(!finished[0][2].toString().isEmpty());
    }
    void interruptedFolderMovePublishesRetainedOutput() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString destination = fixture.filePath("destination");
        const QString merged = QDir(destination).filePath("source");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(merged));
        QVERIFY(write(QDir(source).filePath("first.txt"), "first"));
        QVERIFY(write(QDir(source).filePath("second.txt"), "second"));
        QVERIFY(write(QDir(merged).filePath("existing.txt"), "existing"));
        FileTransferOperation *worker = nullptr;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.removeSource = [&worker](const QString &path) {
            const bool removed = QFile::remove(path);
            if (removed)
                worker->cancel();
            return removed;
        };
        worker = new FileTransferOperation({source}, destination, true, nullptr, adapter);
        auto *task = new QtLocalFileTask(*worker, "Move", destination);
        QSignalSpy output(task, &QtLocalFileTask::transferOutput);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            for (auto *window : QApplication::topLevelWidgets())
                TestProgressDialog::DismissVisibleResult(window);
        });
        answer.start(5);
        task->Start();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.constFirst().constFirst().toBool());
        QCOMPARE(output.size(), 1);
        QVERIFY(output.constFirst().constFirst().toStringList().isEmpty());
        QCOMPARE(QDir(source).entryList(QDir::Files).size(), 1);
        QCOMPARE(read(QDir(merged).filePath("first.txt")), QByteArray("first"));
        QCOMPARE(read(QDir(merged).filePath("second.txt")), QByteArray("second"));
        QCOMPARE(read(QDir(merged).filePath("existing.txt")), QByteArray("existing"));
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
    }

    void lateFailurePreservesAnotherWindowsDraft_data() {
        QTest::addColumn<bool>("closeTask");
        QTest::newRow("explicit-show") << false;
        QTest::newRow("close-completed-task") << true;
    }
    void lateFailurePreservesAnotherWindowsDraft() {
        QFETCH(bool, closeTask);
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source.txt");
        const QString destination = fixture.filePath("output");
        QVERIFY(write(source, "unchanged bytes"));
        QVERIFY(QDir().mkpath(destination));
        QSemaphore entered, release;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [&](const QString &, const QString &) {
            entered.release();
            release.acquire();
            return false;
        };
        adapter.removeSource = [](const QString &) { return false; };
        QPointer<FileTransferOperation> worker =
            new FileTransferOperation({source}, destination, true, nullptr, adapter);
        const auto unblock = qScopeGuard([&] {
            release.release();
            if (worker)
                worker->wait();
        });
        auto *task = new QtLocalFileTask(*worker, "Move", destination);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        task->Start();
        QVERIFY(entered.tryAcquire(1, 2000));
        QPointer<CProgressDialog> progress;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *candidate = dynamic_cast<CProgressDialog *>(widget);
                candidate && candidate->isVisible())
                progress = candidate;
        QVERIFY(progress);
        progress->showMinimized();
        int commits = 0;
        QWidget peer;
        QLineEdit field(&peer);
        peer.show();
        QVERIFY(WindowActivation::waitForActive(peer));
        field.setFocus();
        QTRY_VERIFY(field.hasFocus());
        QTest::keyClicks(&field, "untouched draft");
        field.setSelection(2, 5);
        const auto selection = field.selectedText();
        const auto cursor = field.cursorPosition();
        connect(&field, &QLineEdit::editingFinished, &peer, [&] { ++commits; });
        release.release();
        QVERIFY(worker->wait(10000));
        QTest::qWait(100);
        QVERIFY(finished.isEmpty());
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(field.text(), QString("untouched draft"));
        QCOMPARE(field.selectedText(), selection);
        QCOMPARE(field.cursorPosition(), cursor);
        QVERIFY(field.hasFocus());
        QCOMPARE(commits, 0);
        QTimer answer;
        bool reviewed = false, restored = false, askedToCancel = false;
        connect(&answer, &QTimer::timeout, &peer, [&] {
            if (auto *report = TestProgressDialog::ResultWindow(progress)) {
                reviewed = true;
                restored = progress && !progress->isMinimized();
                TestProgressDialog::CloseResult(*report);
            } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                       box && box->objectName() == "progressCancelQuestion") {
                askedToCancel = true;
                box->button(QMessageBox::No)->click();
            }
        });
        answer.start(5);
        if (closeTask)
            progress->close();
        else
            task->Show();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(reviewed);
        QVERIFY(restored);
        QVERIFY(!askedToCancel);
        QVERIFY(!finished[0][0].toBool());
        QCOMPARE(read(source), QByteArray("unchanged bytes"));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LocalTaskLifecycleTests)
#include "local_task_lifecycle_tests.moc"
