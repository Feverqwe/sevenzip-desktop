#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QScrollBar>
#include <QStatusBar>

using namespace SourceExtractFixture;

class WorkspaceArchivePublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static QStringList names(MainWindow &window) {
        QStringList result;
        for (int row = 0; row < table(window)->rowCount(); ++row)
            result.append(table(window)->model()->index(row, 0).data().toString());
        return result;
    }
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *child : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(child))
                return owner;
        return nullptr;
    }
    static bool notice(MainWindow &window) {
        auto *retry = window.findChild<QPushButton *>("navigationRetry0");
        return retry && retry->isVisible();
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void inlineChangesRefreshAnotherWindow() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("shared.7z");
        QList<NativeArchiveFixture::Item> items{{"original.txt", "payload"}};
        for (int index = 0; index < 80; ++index)
            items.append({QString("file-%1.txt").arg(index, 3, 10, QChar('0')), "payload"});
        QVERIFY(NativeArchiveFixture::Write(archive, items));
        QtWorkspaceApplication application(*qApp);
        MainWindow writer, observer;
        observer.resize(640, 400);
        observer.show();
        observer.openArchive(archive);
        QTRY_VERIFY(names(observer).contains("original.txt"));
        table(observer)->sourcePanel->Set_AutoRefresh_Mode(false);
        QVERIFY(WindowActivation::waitForActive(observer));
        workspace(observer)->EditLocation();
        auto *address = observer.findChild<QLineEdit *>("archivePath");
        QTRY_VERIFY(address->hasFocus());
        address->selectAll();
        QTest::keyClicks(address, "unfinished path");
        address->setSelection(2, 5);
        writer.show();
        writer.openArchive(archive);
        QVERIFY(WindowActivation::waitForActive(writer));
        const QString addressSelection = address->selectedText();
        const int addressCursor = address->cursorPosition();
        QVERIFY(address->isModified());
        auto *observed = table(observer);
        const QStringList selectedNames{"file-001.txt", "file-003.txt"};
        observed->clearSelection();
        for (const QString &name : selectedNames) {
            const int selectedRow = names(observer).indexOf(name);
            QVERIFY(selectedRow >= 0);
            observed->selectionModel()->select(observed->model()->index(selectedRow, 0),
                                               QItemSelectionModel::Select |
                                                   QItemSelectionModel::Rows);
        }
        auto *scroll = observed->verticalScrollBar();
        QTRY_VERIFY(scroll->maximum() > 2);
        scroll->setValue(scroll->maximum() / 2);
        const int topRow = observed->rowAt(0);
        QVERIFY(topRow >= 0);
        const QString anchor = observed->model()->index(topRow, 0).data().toString();
        const int offset = observed->rowViewportPosition(topRow);
        QTRY_VERIFY(action(writer, "createFolderAction")->isEnabled());
        action(writer, "createFolderAction")->trigger();
        auto *folder = writer.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(folder && folder->isVisible());
        folder->setText("new folder");
        QTest::keyClick(folder, Qt::Key_Return);
        QTRY_VERIFY(names(observer).contains("new folder"));
        QVERIFY(names(observer).indexOf("new folder") < names(observer).indexOf(anchor));
        QCOMPARE(observed->model()->index(observed->rowAt(0), 0).data().toString(), anchor);
        QCOMPARE(observed->rowViewportPosition(observed->rowAt(0)), offset);
        QStringList selectionAfterRefresh;
        for (const auto &index : observed->selectionModel()->selectedRows())
            selectionAfterRefresh.append(index.data().toString());
        selectionAfterRefresh.sort();
        QCOMPARE(selectionAfterRefresh, selectedNames);
        auto *view = table(writer);
        const int row = names(writer).indexOf("original.txt");
        QVERIFY(row >= 0);
        view->setFocus();
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        action(writer, "renameAction")->trigger();
        auto *editor = qobject_cast<QLineEdit *>(writer.focusWidget());
        QVERIFY(editor && view->isAncestorOf(editor));
        editor->setText("renamed.txt");
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(names(observer).contains("renamed.txt"));
        QCOMPARE(address->text(), QStringLiteral("unfinished path"));
        QCOMPARE(address->selectedText(), addressSelection);
        QCOMPARE(address->cursorPosition(), addressCursor);
        QVERIFY(!names(observer).contains("original.txt"));
        QVERIFY(names(observer).contains("new folder"));
        QCOMPARE(view->currentIndex().data().toString(), QString("renamed.txt"));
    }

    void missingOuterArchiveNotifiesNestedView_data() {
        QTest::addColumn<bool>("leave");
        QTest::newRow("reopen") << false;
        QTest::newRow("leave") << true;
    }
    void missingOuterArchiveNotifiesNestedView() {
        QFETCH(bool, leave);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString outer = fixture.filePath("outer.7z");
        const QString inner = fixture.filePath("inner.zip");
        const auto writeVersion = [&](const QString &member) {
            return NativeArchiveFixture::Write(inner, {{member, "payload"}}, "zip") &&
                   NativeArchiveFixture::Write(outer, {{"inner.zip", readFile(inner)}});
        };
        QVERIFY(writeVersion("original.txt"));
        QtWorkspaceApplication application(*qApp);
        MainWindow window;
        window.show();
        window.openArchive(outer);
        QTRY_COMPARE(names(window), QStringList{"inner.zip"});
        auto *view = table(window);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(window, "openItemAction")->trigger();
        QTRY_COMPARE(names(window), QStringList{"original.txt"});
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        QVERIFY(action(window, "createFolderAction")->isEnabled());
        const QString renamed = fixture.filePath("renamed.7z");
        QVERIFY(QFile::rename(outer, renamed));
        application.FilesystemPublished({fixture.path()});
        QTRY_VERIFY(notice(window));
        QCOMPARE(names(window), QStringList{"original.txt"});
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
        if (leave) {
            window.findChild<QPushButton *>("navigationParent0")->click();
            QTRY_COMPARE(names(window), QStringList{"inner.zip"});
            QVERIFY(notice(window));
            window.findChild<QPushButton *>("navigationParent0")->click();
            QTRY_VERIFY(names(window).contains("renamed.7z"));
            QVERIFY(!notice(window));
            return;
        }
        QVERIFY(QFile::remove(inner));
        QVERIFY(writeVersion("replacement.txt"));
        window.findChild<QPushButton *>("navigationRetry0")->click();
        QTRY_COMPARE(names(window), QStringList{"replacement.txt"});
        QVERIFY(!notice(window));
        QVERIFY(action(window, "createFolderAction")->isEnabled());
    }

    void manualRefreshDetectsExternalArchiveReplacement_data() {
        QTest::addColumn<QString>("encryptedLayer");
        QTest::addColumn<bool>("deferRefresh");
        QTest::newRow("plain") << QString() << false;
        QTest::newRow("outer-password-changed") << QStringLiteral("outer") << false;
        QTest::newRow("nested-password-changed") << QStringLiteral("inner") << false;
        QTest::newRow("deferred-password-change") << QStringLiteral("outer") << true;
    }
    void manualRefreshDetectsExternalArchiveReplacement() {
        QFETCH(QString, encryptedLayer);
        QFETCH(bool, deferRefresh);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("shared.7z");
        const QString replacement = fixture.filePath("replacement.7z");
        const bool nested = encryptedLayer == "inner";
        const auto writeVersion = [&](const QString &path, const QString &name,
                                      const QString &password) {
            if (!nested)
                return NativeArchiveFixture::Write(path, {{name, "payload"}}, "7z", password);
            const QString inner = path + ".inner";
            return NativeArchiveFixture::Write(inner, {{name, "payload"}}, "7z", password) &&
                   NativeArchiveFixture::Write(path, {{"inner.7z", readFile(inner)}});
        };
        QString password = encryptedLayer.isEmpty() ? QString() : QStringLiteral("old-password");
        const QString replacementPassword =
            encryptedLayer.isEmpty() ? QString() : QStringLiteral("new-password");
        QVERIFY(writeVersion(archive, "original.txt", password));
        QVERIFY(writeVersion(replacement, "updated.txt", replacementPassword));
        struct DeferredRefreshBackend : ArchiveBackend {
            bool paused = false;
            bool busy() const override { return paused || ArchiveBackend::busy(); }
        } backend;
        MainWindow window(&backend);
        bool cancelPassword = false;
        int prompts = 0;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(dialogs_.activeDialog())) {
                ++prompts;
                if (cancelPassword)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    dialog->accept();
                }
            }
        });
        responder.start(1);
        window.show();
        window.openArchive(archive);
        if (nested) {
            QTRY_COMPARE(names(window), QStringList{"inner.7z"});
            table(window)->setCurrentCell(0, 0);
            table(window)->selectRow(0);
            QTRY_VERIFY(action(window, "openInsideAction")->isEnabled());
            action(window, "openInsideAction")->trigger();
        }
        QTRY_COMPARE(names(window), QStringList{"original.txt"});
        QVERIFY(WindowActivation::waitForActive(window));
        QVERIFY(QFile::rename(archive, fixture.filePath("original.7z")));
        QVERIFY(QFile::rename(replacement, archive));
        QCOMPARE(names(window), QStringList{"original.txt"});
        cancelPassword = !encryptedLayer.isEmpty();
        prompts = 0;
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        backend.paused = deferRefresh;
        action(window, "refreshAction")->trigger();
        if (deferRefresh) {
            QCOMPARE(prompts, 0);
            backend.paused = false;
        }
        if (cancelPassword) {
            QTRY_COMPARE(prompts, 1);
            QTRY_VERIFY(notice(window));
            QCOMPARE(names(window), QStringList{"original.txt"});
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
            cancelPassword = false;
            password = QStringLiteral("incorrect-password");
            auto *retry = window.findChild<QPushButton *>("navigationRetry0");
            retry->click();
            QTRY_VERIFY(window.statusBar()->currentMessage().contains("Wrong password",
                                                                      Qt::CaseInsensitive));
            QCOMPARE(prompts, 2);
            QCOMPARE(names(window), QStringList{"original.txt"});
            password = replacementPassword;
            backend.paused = deferRefresh;
            retry->click();
            if (deferRefresh) {
                QCOMPARE(prompts, 2);
                backend.paused = false;
            }
            QTRY_COMPARE(prompts, 3);
        }
        QTRY_COMPARE(names(window), QStringList{"updated.txt"});
        QVERIFY(!notice(window));
        QVERIFY(window.statusBar()->currentMessage().isEmpty());

        QVERIFY(QFile::rename(archive, replacement));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(notice(window));
        QCOMPARE(names(window), QStringList{"updated.txt"});
        QVERIFY(window.statusBar()->currentMessage().startsWith("Path not found:"));
        QVERIFY(QFile::rename(replacement, archive));
        backend.paused = deferRefresh;
        window.findChild<QPushButton *>("navigationRetry0")->click();
        if (deferRefresh) {
            QVERIFY(notice(window));
            backend.paused = false;
        }
        QTRY_VERIFY(!notice(window));
        QCOMPARE(names(window), QStringList{"updated.txt"});
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
        QCOMPARE(prompts, encryptedLayer.isEmpty() ? 0 : 3);
    }

    void savingNestedChangesRefreshesAnotherWindow_data() {
        QTest::addColumn<bool>("closeWriter");
        QTest::newRow("up") << false;
        QTest::newRow("close") << true;
    }

    void savingNestedChangesRefreshesAnotherWindow() {
        QFETCH(bool, closeWriter);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString inner = fixture.filePath("child.7z");
        const QString outer = fixture.filePath("parent.zip");
        QVERIFY(NativeArchiveFixture::Write(inner, {{"original.txt", "payload"}}));
        QVERIFY(NativeArchiveFixture::Write(outer, {{"child.7z", readFile(inner)}}, "zip"));
        const QByteArray original = readFile(outer);
        QtWorkspaceApplication application(*qApp);
        bool asked = false;
        bool save = false;
        bool explicitChoiceRequired = false;
        QTimer responder;
        MainWindow observer, writer;
        dialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                if (box->objectName() == "modifiedArchiveQuestion") {
                    asked = true;
                    if (save && closeWriter) {
                        QTest::keyClick(box, Qt::Key_Escape);
                        explicitChoiceRequired = box->isVisible() && !box->close();
                        if (!box->isVisible())
                            return;
                    }
                    auto *discard = box->button(QMessageBox::Discard);
                    box->button(save      ? QMessageBox::Yes
                                : discard ? QMessageBox::Discard
                                          : QMessageBox::No)
                        ->click();
                } else
                    box->accept();
            }
        });
        responder.start(1);
        for (auto *window : {&observer, &writer}) {
            window->show();
            window->openArchive(outer);
            QTRY_COMPARE(names(*window), QStringList{"child.7z"});
            table(*window)->selectRow(0);
            table(*window)->setCurrentCell(0, 0);
            QTRY_VERIFY(action(*window, "openInsideAction")->isEnabled());
            action(*window, "openInsideAction")->trigger();
            QTRY_COMPARE(names(*window), QStringList{"original.txt"});
        }
        table(observer)->sourcePanel->Set_AutoRefresh_Mode(false);
        QVERIFY(WindowActivation::waitForActive(writer));
        QTRY_VERIFY(action(writer, "createFolderAction")->isEnabled());
        action(writer, "createFolderAction")->trigger();
        auto *folder = writer.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(folder && folder->isVisible());
        folder->setText("new folder");
        QTest::keyClick(folder, Qt::Key_Return);
        QTRY_VERIFY(names(writer).contains("new folder"));
        QCOMPARE(readFile(outer), original);
        QVERIFY(!names(observer).contains("new folder"));

        QTRY_VERIFY(action(writer, "upAction")->isEnabled());
        save = true;
        if (closeWriter)
            QVERIFY(writer.close());
        else
            action(writer, "upAction")->trigger();
        QTRY_VERIFY(asked);
        if (closeWriter)
            QVERIFY(explicitChoiceRequired);
        QTRY_VERIFY(names(observer).contains("new folder"));
        QVERIFY(names(observer).contains("original.txt"));
        QVERIFY(readFile(outer) != original);
    }

    void reopenPublishedChain_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("active-nested") << 0;
        QTest::newRow("missing-child-rolls-back") << 1;
        QTest::newRow("new-password-waits-for-retry") << 2;
        QTest::newRow("dirty-spool-retained") << 3;
        QTest::newRow("dirty-spool-survives-failed-reopen-and-close") << 4;
        QTest::newRow("changed-child-format-refused") << 6;
        QTest::newRow("duplicate-child-refused") << 7;
        QTest::newRow("publication-during-reopen") << 8;
        QTest::newRow("unstarted-draft-defers-reopen") << 9;
        QTest::newRow("publication-while-navigation-parked") << 10;
        QTest::newRow("parent-opens-current-outer-after-missing-child") << 14;
        QTest::newRow("unchanged-direct-stream-releases-empty-copy-directory") << 15;
        QTest::newRow("publication-during-modified-child-question") << 16;
    }
    void reopenPublishedChain() {
        QFETCH(int, scenario);
        const bool dirty = scenario == 3 || scenario == 4 || scenario == 16;
        const bool missing = scenario == 1 || scenario == 4 || scenario == 14;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray previousRegistry = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", fixture.filePath("registry").toUtf8());
        const auto restoreRegistry =
            qScopeGuard([&] { qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", previousRegistry); });
        const QString innerName = QStringLiteral("вложенный 空.zip");
        const QString original = fixture.filePath("original.zip");
        const QString outerFormat = scenario == 15 ? QString("tar") : QString("zip");
        const QString outer = fixture.filePath("outer." + outerFormat);
        QVERIFY(NativeArchiveFixture::Write(
            original, {{"keep.txt", "original"}, {"before.txt", "before"}}, "zip"));
        QVERIFY(NativeArchiveFixture::Write(outer, {{innerName, readFile(original)}}, outerFormat));
        auto backend = std::make_unique<ArchiveBackend>();
        auto window = std::make_unique<MainWindow>(backend.get());
        window->resize(scenario == 4 ? QSize(640, 400) : QSize(980, 620));
        const auto cleanup = qScopeGuard([&] {
            window.reset();
            backend.reset();
            QtOwnedTempRegistry registry;
            for (const auto &entry : registry.Entries())
                if (auto pin = registry.PinForRead(entry)) {
                    if (QFileInfo(entry.path).isDir())
                        QDir(entry.path).removeRecursively();
                    else
                        QFile::remove(entry.path);
                }
        });
        int passwordQuestions = 0, otherQuestions = 0;
        bool allowPassword = false;
        bool parkedPublication = false;
        bool closePublication = false;
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (auto *password = dynamic_cast<CPasswordDialog *>(dialog)) {
                ++passwordQuestions;
                if (scenario == 10 && !parkedPublication) {
                    parkedPublication = true;
                    window->ArchivePublished(outer);
                }
                if (allowPassword) {
                    password->findChild<QLineEdit *>("passwordEdit")->setText("fresh-secret");
                    password->accept();
                } else
                    password->reject();
            } else if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                if (scenario == 16 && box->objectName() == "modifiedArchiveQuestion") {
                    closePublication = true;
                    window->ArchivePublished(outer);
                    box->button(QMessageBox::No)->click();
                    return;
                }
                ++otherQuestions;
                box->reject();
            }
        });
        responder.start(1);
        window->show();
        window->openArchive(outer);
        QTRY_COMPARE(table(*window)->rowCount(), 1);
        table(*window)->selectRow(0);
        table(*window)->setCurrentCell(0, 0);
        if (dirty)
            QCOMPARE(table(*window)->sourcePanel->OpenItemInArchiveInternal(0, nullptr, true),
                     S_OK);
        else
            action(*window, "openInsideAction")->trigger();
        QTRY_VERIFY(names(*window).contains("before.txt"));
        auto *source = table(*window)->sourcePanel;
        const QString spool = ToQString(fs2us(source->GetParentFolders().Back().FilePath));
        const QString spoolDirectory = QFileInfo(spool).absolutePath();
        if (scenario == 15) {
            QVERIFY(source->GetParentFolders().Back().IsVirtual);
            QVERIFY(!QFileInfo::exists(spool));
            QVERIFY(QFileInfo::exists(spoolDirectory));
        }
        QByteArray unsaved;
        if (dirty) {
            QVERIFY(QFileInfo::exists(spool));
            QVERIFY(QFile::remove(spool));
            QVERIFY(
                NativeArchiveFixture::Write(spool, {{"unsaved.txt", "not in the parent"}}, "zip"));
            unsaved = readFile(spool);
        }
        const int keep = filesystemRow(*window, "keep.txt");
        QVERIFY(keep >= 0);
        table(*window)->selectRow(keep);
        table(*window)->setCurrentCell(keep, 0);
        table(*window)->setFocus();
        const QString address = window->findChild<QLineEdit *>("archivePath")->text();
        QDialog draft;
        QLineEdit input(&draft);
        if (scenario == 9) {
            input.setText(QStringLiteral("неприменённый ввод"));
            input.setSelection(2, 5);
            QtTaskPresentation::Watch(draft, QtOperationPhase::Draft);
            draft.show();
            input.setFocus();
            QTRY_VERIFY(input.hasFocus());
            input.setSelection(2, 5);
        }
        const QString replacement = fixture.filePath("replacement.zip");
        const QString child = fixture.filePath("new-child.zip");
        QVERIFY(NativeArchiveFixture::Write(
            child, {{"keep.txt", QByteArray(200, 'n')}, {"after.txt", "after"}},
            scenario == 6 ? "7z" : "zip"));
        const QString secret = scenario == 2 ? QString("fresh-secret") : QString();
        QList<NativeArchiveFixture::Item> entries{
            {missing ? QString("different.zip") : innerName, readFile(child)}};
        if (scenario == 7)
            entries.push_back({innerName, readFile(child)});
        QVERIFY(NativeArchiveFixture::Write(replacement, entries, outerFormat, secret));
        QVERIFY(QFile::remove(outer));
        QVERIFY(QFile::rename(replacement, outer));
        QByteArray published = readFile(outer);
        QSet<QString> existingOwners;
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            existingOwners.insert(entry.key);
        const QString newest = fixture.filePath("newest.zip");
        if (scenario == 8) {
            const QString newestChild = fixture.filePath("newest-child.zip");
            QVERIFY(NativeArchiveFixture::Write(
                newestChild, {{"keep.txt", "latest"}, {"latest.txt", "latest"}}, "zip"));
            QVERIFY(
                NativeArchiveFixture::Write(newest, {{innerName, readFile(newestChild)}}, "zip"));
        }
        bool publishedAgain = false;
        QTimer interpose;
        connect(&interpose, &QTimer::timeout, this, [&] {
            for (const auto &entry : QtOwnedTempRegistry().Entries()) {
                if (existingOwners.contains(entry.key))
                    continue;
                interpose.stop();
                if (QFile::remove(outer) && QFile::rename(newest, outer)) {
                    published = readFile(outer);
                    publishedAgain = true;
                    window->ArchivePublished(outer);
                }
                return;
            }
        });
        if (scenario == 8)
            interpose.start(1);
        if (scenario == 16) {
            action(*window, "upAction")->trigger();
            QVERIFY(closePublication);
            QTRY_COMPARE(table(*window)->rowCount(), 1);
            table(*window)->selectRow(0);
            table(*window)->setCurrentCell(0, 0);
            action(*window, "openInsideAction")->trigger();
        } else if (scenario == 10) {
            const QString candidate = fixture.filePath("candidate.7z");
            QVERIFY(NativeArchiveFixture::Write(candidate, {{"candidate.txt", "unaccepted"}}, "7z",
                                                "other-secret"));
            workspace(*window)->EditLocation();
            auto *edit = window->findChild<QLineEdit *>("archivePath");
            edit->setText(candidate);
            QTest::keyClick(edit, Qt::Key_Return);
            QVERIFY(parkedPublication);
            QTRY_VERIFY(table(*window)->isEnabled());
            QCOMPARE(edit->text(), candidate);
            QVERIFY(WindowActivation::waitForActive(*window));
            QTest::keyClick(edit, Qt::Key_Escape);
            table(*window)->setFocus();
            QTRY_VERIFY(table(*window)->hasFocus());
        } else
            window->ArchivePublished(outer);
        if (scenario == 9) {
            const int caret = input.cursorPosition();
            const QString selection = input.selectedText();
            QSignalSpy committed(&input, &QLineEdit::editingFinished);
            QTest::qWait(120);
            QVERIFY(names(*window).contains("before.txt"));
            QCOMPARE(input.cursorPosition(), caret);
            QCOMPARE(input.selectedText(), selection);
            QVERIFY(committed.isEmpty());
            QApplication::setActiveWindow(window.get());
            table(*window)->setFocus();
            QTest::qWait(120);
            QVERIFY(names(*window).contains("before.txt"));
            draft.reject();
        }
        const bool pending = missing || scenario == 2 || scenario == 6 || scenario == 7;
        if (pending) {
            QTRY_VERIFY(notice(*window));
            auto *surface = window->findChild<QPushButton *>("navigationRetry0")->parentWidget();
            QVERIFY(UiInspection::fullyVisible(*surface, *window));
            for (auto *label : surface->findChildren<QLabel *>())
                QVERIFY(UiInspection::labelFits(*label));
            if (scenario == 1 || scenario == 4)
                QVERIFY(UiInspection::writeReport(
                    *surface, scenario == 4 ? "archive-published-pending-compact"
                                            : "archive-published-pending-normal"));
            QVERIFY(names(*window).contains("before.txt"));
            QCOMPARE(passwordQuestions, 0);
            QCOMPARE(otherQuestions, 0);
            if (scenario == 2) {
                allowPassword = true;
                window->findChild<QPushButton *>("navigationRetry0")->click();
                QTRY_VERIFY(names(*window).contains("after.txt"));
                QVERIFY(passwordQuestions > 0);
                QVERIFY(!notice(*window));
            }
            if (scenario == 14) {
                window->findChild<QPushButton *>("navigationParent0")->click();
                QTRY_COMPARE(table(*window)->rowCount(), 1);
                QTRY_COMPARE(names(*window).first(), QString("different.zip"));
                QVERIFY(!notice(*window));
            }
        } else {
            QTRY_VERIFY2(
                names(*window).contains(scenario == 8 ? "latest.txt" : "after.txt"),
                qPrintable(
                    QString("rows=%1; notice=%2; source=%3; outdated=%4; focus=%5")
                        .arg(names(*window).join(','), QString::number(notice(*window)),
                             ToQString(table(*window)->sourcePanel->GetCurrentFolderPrefix()),
                             QString::number(table(*window)->sourcePanel->ArchiveContextOutdated()),
                             QApplication::focusWidget()
                                 ? QApplication::focusWidget()->metaObject()->className()
                                 : "none")));
            if (scenario == 8)
                QVERIFY(publishedAgain);
            if (scenario != 16)
                QCOMPARE(table(*window)
                             ->model()
                             ->index(table(*window)->currentRow(), 0)
                             .data()
                             .toString(),
                         QString("keep.txt"));
            QVERIFY(!notice(*window));
        }
        if (scenario != 14)
            QCOMPARE(window->findChild<QLineEdit *>("archivePath")->text(), address);
        else
            QVERIFY(window->findChild<QLineEdit *>("archivePath")
                        ->text()
                        .endsWith(QFileInfo(outer).fileName()));
        if (scenario == 0) {
            action(*window, "backAction")->trigger();
            QTRY_COMPARE(table(*window)->rowCount(), 1);
            QCOMPARE(names(*window).first().normalized(QString::NormalizationForm_C), innerName);
            action(*window, "forwardAction")->trigger();
            QTRY_VERIFY(names(*window).contains("after.txt"));
        }
        QCOMPARE(readFile(outer), published);
        if (dirty)
            QCOMPARE(readFile(spool), unsaved);
        if (scenario == 15)
            QVERIFY(!QFileInfo::exists(spoolDirectory));
        QVERIFY(window->close());
        window.reset();
        QCOMPARE(readFile(outer), published);
        if (dirty)
            QCOMPARE(readFile(spool), unsaved);
        QCOMPARE(otherQuestions, 0);
    }
};

QTEST_MAIN(WorkspaceArchivePublicationTests)
#include "workspace_archive_publication_tests.moc"
