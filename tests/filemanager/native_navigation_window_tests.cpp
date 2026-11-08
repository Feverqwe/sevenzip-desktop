#include "archive/support/native_archive_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/panels/qtpanellist.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

class NativeNavigationWindowTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static void openFilesystemPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        QVERIFY(panel);
        auto *address = panel->findChild<QLineEdit *>("archivePath");
        auto *list = panel->findChild<FileTable *>("archiveTable");
        QVERIFY(address && list);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        QTRY_COMPARE(list->rowCount(), 1);
        // Finish the source address-Enter focus delivery before starting a
        // different command or explicitly choosing address focus.
        QTRY_VERIFY(
            panel->findChild<QStackedWidget *>("fileViewStack")->currentWidget()->hasFocus());
    }

  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void folderCommandPreservesPeerWhenTargetDisappears_data() {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("extraEntries");
        QTest::newRow("same-folder") << int(Qt::Key_Up) << 0;
        QTest::newRow("selected-folder-right") << int(Qt::Key_Right) << 0;
        QTest::newRow("selected-folder-left") << int(Qt::Key_Left) << 0;
        QTest::newRow("large-selected-folder") << int(Qt::Key_Right) << 257;
    }
    void folderCommandPreservesPeerWhenTargetDisappears() {
        QFETCH(int, key);
        QFETCH(int, extraEntries);
        const bool selectedFolder = key != Qt::Key_Up;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString parent = fixture.filePath("source parent");
        const QString source = QDir(parent).filePath("source folder");
        const QString moved = fixture.filePath("renamed source");
        const QString peer = fixture.filePath("peer folder");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        for (const auto &path :
             {QDir(source).filePath("source.txt"), QDir(peer).filePath("peer.txt")}) {
            QFile entry(path);
            QVERIFY(entry.open(QIODevice::WriteOnly));
        }
        for (int index = 0; index < extraEntries; ++index) {
            QFile entry(QDir(source).filePath(QString("zz-%1.txt").arg(index)));
            QVERIFY(entry.open(QIODevice::WriteOnly));
        }
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        openFilesystemPanel(window, 1, peer);
        QVERIFY(!QTest::currentTestFailed());
        auto *peerPanel = window.findChild<QWidget *>("filePanel1");
        auto *peerList = peerPanel->findChild<FileTable *>("archiveTable");
        auto *peerAddress = peerPanel->findChild<QLineEdit *>("archivePath");
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        peerList->setCurrentCell(0, 0);
        peerList->selectRow(0);
        const QString originalPeer = peerAddress->text();
        PanelKeys::focusAddress(peerAddress);
        QTRY_VERIFY(peerAddress->hasFocus());
        peerAddress->selectAll();
        QTest::keyClicks(peerAddress, "unfinished peer path");
        peerAddress->setSelection(2, 5);

        openFilesystemPanel(window, 0, selectedFolder ? parent : source);
        QVERIFY(!QTest::currentTestFailed());
        auto *sourceList =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        sourceList->sourcePanel->Set_AutoRefresh_Mode(false);
        sourceList->setCurrentCell(0, 1);
        QCOMPARE(sourceList->currentIndex().column(), 1);
        const auto command = [&] {
            PanelKeys::command(sourceList, static_cast<Qt::Key>(key),
                               Qt::AltModifier | Qt::ShiftModifier);
        };
        QtWindowWorkspace *workspace = nullptr;
        for (auto *child : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(child))
                workspace = owner;
        QVERIFY(workspace);
        QVERIFY(QDir().rename(source, moved));
        const auto draft = peerAddress->text();
        QCOMPARE(draft, QString("unfinished peer path"));
        const auto selectedText = peerAddress->selectedText();
        const int cursor = peerAddress->cursorPosition();
        if (selectedFolder)
            command();
        else
            QCOMPARE(workspace->OpenFolderInPeer(0), HRESULT(S_FALSE));
        QVERIFY(window.statusBar()->currentMessage().contains(source));
        QTRY_VERIFY(peerList->isEnabled());
        QCOMPARE(peerAddress->text(), draft);
        QCOMPARE(peerAddress->selectedText(), selectedText);
        QCOMPARE(peerAddress->cursorPosition(), cursor);
        QCOMPARE(peerList->currentIndex().data().toString(), QStringLiteral("peer.txt"));
        QCOMPARE(peerList->selectionModel()->selectedRows(),
                 QModelIndexList{peerList->currentIndex()});
        QVERIFY(sourceList->hasFocus());
        QCOMPARE(sourceList->currentIndex().column(), 1);

        QVERIFY(QDir().rename(moved, source));
        command();
        QTRY_COMPARE(peerList->rowCount(), extraEntries + 1);
        QTRY_VERIFY(peerList->isEnabled());
        QCOMPARE(QDir::cleanPath(peerAddress->text()), QDir::cleanPath(source));
        QTRY_COMPARE(peerList->model()->index(0, 0).data().toString(),
                     QStringLiteral("source.txt"));
        QTRY_VERIFY(window.statusBar()->currentMessage().isEmpty());
        QVERIFY(sourceList->hasFocus());
        QCOMPARE(sourceList->currentIndex().column(), 1);
        peerList->setFocus();
        QTRY_VERIFY(peerList->hasFocus());
        QTRY_VERIFY(action(window, "backAction")->isEnabled());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(peerAddress->text(), originalPeer);
    }
    void failedPeerArchiveOpenKeepsAddressDraft_data() {
        QTest::addColumn<QString>("attempt");
        QTest::newRow("missing-archive") << QStringLiteral("missing");
        QTest::newRow("password-cancelled") << QStringLiteral("cancel");
        QTest::newRow("wrong-password") << QStringLiteral("wrong");
        QTest::newRow("nested-wrong-password") << QStringLiteral("nested-wrong");
        QTest::newRow("renamed-nested-archive") << QStringLiteral("nested-missing");
    }
    void failedPeerArchiveOpenKeepsAddressDraft() {
        QFETCH(QString, attempt);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("source.7z");
        const bool nestedMissing = attempt == "nested-missing";
        const bool nested = nestedMissing || attempt == "nested-wrong";
        const bool wrongPassword = attempt == "wrong" || attempt == "nested-wrong";
        const QString encrypted = nested ? fixture.filePath("inner.7z") : archive;
        const QString previous = fixture.filePath("previous");
        const QString password =
            attempt == "missing" || nestedMissing ? QString() : QStringLiteral("fixture password");
        QVERIFY(
            NativeArchiveFixture::Write(encrypted, {{"source.txt", "payload"}}, "7z", password));
        if (nested) {
            QFile inner(encrypted);
            QVERIFY(inner.open(QIODevice::ReadOnly));
            QVERIFY(NativeArchiveFixture::Write(archive, {{"inner.7z", inner.readAll()}}));
        }
        QVERIFY(QDir().mkpath(previous));
        QFile entry(QDir(previous).filePath("previous.txt"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.close();
        MainWindow window;
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        openFilesystemPanel(window, 1, previous);
        QVERIFY(!QTest::currentTestFailed());
        auto *peerPanel = window.findChild<QWidget *>("filePanel1");
        auto *peer = peerPanel->findChild<FileTable *>("archiveTable");
        auto *edit = peerPanel->findChild<QLineEdit *>("archivePath");
        auto *source =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        bool openingPeer = false;
        int passwordRequests = 0;
        bool unexpectedMessage = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget())) {
                ++passwordRequests;
                if (openingPeer && attempt == "cancel")
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")
                        ->setText(openingPeer && wrongPassword ? QStringLiteral("wrong")
                                                               : password);
                    dialog->accept();
                }
            } else if (auto *message =
                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpectedMessage = true;
                message->accept();
            }
        });
        responder.start(1);
        window.openArchive(archive);
        if (nested) {
            QTRY_COMPARE(source->model()->index(0, 0).data().toString(),
                         QStringLiteral("inner.7z"));
            source->setCurrentCell(0, 0);
            source->selectRow(0);
            QTRY_VERIFY(action(window, "openInsideAction")->isEnabled());
            action(window, "openInsideAction")->trigger();
        }
        QTRY_COMPARE(source->model()->index(0, 0).data().toString(), QStringLiteral("source.txt"));
        QVERIFY(WindowActivation::waitForActive(window));
        PanelKeys::focusAddress(edit);
        QTRY_VERIFY(edit->hasFocus());
        edit->selectAll();
        QTest::keyClicks(edit, "unfinished path");
        edit->setSelection(2, 5);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        const QString selection = edit->selectedText();
        const int cursor = edit->cursorPosition();
        QVERIFY(edit->isModified());
        openingPeer = true;
        if (attempt == "missing")
            QVERIFY(QFile::rename(archive, fixture.filePath("moved.7z")));
        if (nestedMissing) {
            source->sourcePanel->Set_AutoRefresh_Mode(false);
            QFile inner(encrypted);
            QVERIFY(inner.open(QIODevice::ReadOnly));
            const QString replacement = fixture.filePath("replacement.7z");
            QVERIFY(NativeArchiveFixture::Write(replacement, {{"renamed.7z", inner.readAll()}}));
            QVERIFY(QFile::rename(archive, fixture.filePath("moved.7z")));
            QVERIFY(QFile::rename(replacement, archive));
        }
        PanelKeys::command(source, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
        if (attempt == "cancel") {
            QCOMPARE(passwordRequests, 2);
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
        } else if (wrongPassword) {
            QCOMPARE(passwordRequests, 2);
            QTRY_VERIFY2(window.statusBar()->currentMessage().contains("Wrong password",
                                                                       Qt::CaseInsensitive),
                         qPrintable(window.statusBar()->currentMessage()));
        } else if (nestedMissing) {
            const QString message = window.statusBar()->currentMessage();
            QVERIFY2(message.startsWith("Path not found:") && message.endsWith("inner.7z"),
                     qPrintable(message));
        } else {
            QTRY_VERIFY(window.statusBar()->currentMessage().contains("source.7z"));
        }
        QVERIFY(!unexpectedMessage);
        QCOMPARE(peer->model()->index(0, 0).data().toString(), QStringLiteral("previous.txt"));
        QCOMPARE(edit->text(), QStringLiteral("unfinished path"));
        QCOMPARE(edit->selectedText(), selection);
        QCOMPARE(edit->cursorPosition(), cursor);
        QCOMPARE(window.focusWidget(), static_cast<QWidget *>(source));
        if (wrongPassword) {
            openingPeer = false;
            QVERIFY(WindowActivation::waitForActive(window));
            PanelKeys::command(source, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
            QTRY_COMPARE(peer->model()->index(0, 0).data().toString(),
                         QStringLiteral("source.txt"));
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
        }
    }
    void openingParentRowInPeerRecordsPreviousFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString child = fixture.filePath("parent/child");
        const QString previous = fixture.filePath("previous");
        QVERIFY(QDir().mkpath(child));
        QVERIFY(QDir().mkpath(previous));
        QFile entry(QDir(previous).filePath("previous.txt"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.close();
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        CFmSettings{true}.Save(settings);
        SavePanelPath(1, ToUString(previous), settings);
        MainWindow window;
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *source =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        auto *peerPanel = window.findChild<QWidget *>("filePanel1");
        auto *peer = peerPanel->findChild<FileTable *>("archiveTable");
        auto *address = peerPanel->findChild<QLineEdit *>("archivePath");
        QTRY_COMPARE(peer->model()->index(1, 0).data().toString(), QStringLiteral("previous.txt"));
        const QString originalAddress = address->text();
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        window.openPath(child);
        QTRY_COMPARE(source->rowCount(), 1);
        QTRY_COMPARE(source->model()->index(0, 0).data().toString(), QStringLiteral(".."));
        source->setCurrentCell(0, 0);
        PanelKeys::command(source, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        QTRY_COMPARE(peer->model()->index(1, 0).data().toString(), QStringLiteral("child"));
        QCOMPARE(window.focusWidget(), static_cast<QWidget *>(source));
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());
        QTRY_VERIFY(action(window, "backAction")->isEnabled());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(address->text(), originalAddress);
        QTRY_COMPARE(peer->model()->index(1, 0).data().toString(), QStringLiteral("previous.txt"));
    }
    void openArchiveSubfolderInPeerKeepsParentChain() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString inner = fixture.filePath("child.7z");
        const QString outer = fixture.filePath("outer.zip");
        QVERIFY(NativeArchiveFixture::Write(inner, {{"folder/payload.txt", "payload"}}));
        QFile nested(inner);
        QVERIFY(nested.open(QIODevice::ReadOnly));
        QVERIFY(NativeArchiveFixture::Write(outer, {{"child.7z", nested.readAll()}}, "zip"));
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *source =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        auto *peer =
            window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>("archiveTable");
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        window.openArchive(outer);
        QTRY_COMPARE(source->model()->index(0, 0).data().toString(), QStringLiteral("child.7z"));
        source->setCurrentCell(0, 0);
        source->selectRow(0);
        QTRY_VERIFY(action(window, "openInsideAction")->isEnabled());
        action(window, "openInsideAction")->trigger();
        QTRY_COMPARE(source->model()->index(0, 0).data().toString(), QStringLiteral("folder"));
        QVERIFY(WindowActivation::waitForActive(window));
        source->setCurrentCell(0, 0);
        source->selectRow(0);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        PanelKeys::command(source, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        QTRY_COMPARE(peer->model()->index(0, 0).data().toString(), QStringLiteral("payload.txt"));
        QTRY_COMPARE(window.focusWidget(), static_cast<QWidget *>(source));

        auto *up = window.findChild<QPushButton *>("panelParentButton1");
        QVERIFY(up);
        QTRY_VERIFY(up->isEnabled());
        up->click();
        QTRY_COMPARE(peer->model()->index(0, 0).data().toString(), QStringLiteral("folder"));
        QTRY_VERIFY(up->isEnabled());
        up->click();
        QTRY_COMPARE(peer->model()->index(0, 0).data().toString(), QStringLiteral("child.7z"));
        QCOMPARE(source->model()->index(0, 0).data().toString(), QStringLiteral("folder"));
    }
    void parentButtonBelongsToItsPanel_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("peerRoot");
        QTest::addColumn<bool>("deferred");
        for (unsigned pane : {0u, 1u})
            for (bool root : {false, true})
                for (bool deferred : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(root).arg(deferred)))
                        << pane << root << deferred;
    }
    void parentButtonBelongsToItsPanel() {
        QFETCH(unsigned, pane);
        QFETCH(bool, peerRoot);
        QFETCH(bool, deferred);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString childName = QStringLiteral("child папка 空");
        const QString child = fixture.filePath(childName);
        QVERIFY(QDir().mkpath(child));
        QFile payload(QDir(child).filePath("protected.txt"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("protected bytes"), qint64(15));
        payload.close();
        QTemporaryDir peerFixture;
        QVERIFY(peerFixture.isValid());
        QFile peerPayload(peerFixture.filePath("peer.txt"));
        QVERIFY(peerPayload.open(QIODevice::WriteOnly));
        QCOMPARE(peerPayload.write("peer bytes"), qint64(10));
        peerPayload.close();
        if (deferred)
            for (int i = 0; i < 257; ++i) {
                QFile extra(fixture.filePath(QString("entry-%1.txt").arg(i)));
                QVERIFY(extra.open(QIODevice::WriteOnly));
            }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        openFilesystemPanel(window, pane, child);
        QVERIFY(!QTest::currentTestFailed());
        openFilesystemPanel(window, 1 - pane, peerFixture.path());
        QVERIFY(!QTest::currentTestFailed());
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *list = owner->findChild<FileTable *>("archiveTable");
        auto *peerList = peer->findChild<FileTable *>("archiveTable");
        auto *address = owner->findChild<QLineEdit *>("archivePath");
        auto *peerAddress = peer->findChild<QLineEdit *>("archivePath");
        auto *button = owner->findChild<QPushButton *>(QString("panelParentButton%1").arg(pane));
        QVERIFY(button);
        if (peerRoot) {
            action(window, "openRootAction")->trigger();
            action(window, "parentFolderAction")->trigger();
            QVERIFY(peerList->sourcePanel->IsRootFolder());
            QVERIFY(!action(window, "parentFolderAction")->isEnabled());
        }
        const UString peerPrefix = peerList->sourcePanel->GetCurrentFolderPrefix();
        const int peerRows = peerList->rowCount();
        if (!peerRoot) {
            QCOMPARE(peerRows, 1);
            peerList->setCurrentCell(0, 0);
            peerList->selectRow(0);
        }
        const QString draft = QStringLiteral("peer черновик");
        peerAddress->setText(draft);
        QVERIFY(button->isEnabled());
        if (deferred) {
            button->setFocus();
            QTRY_VERIFY(button->hasFocus());
            QTest::keyClick(button, Qt::Key_Space);
        } else
            QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
        QTRY_VERIFY(list->isEnabled());
        QTRY_COMPARE(list->rowCount(), deferred ? 258 : 1);
        QVERIFY(list->currentIndex().isValid());
        QCOMPARE(list->currentIndex().data().toString(), childName);
        QCOMPARE(peerList->sourcePanel->GetCurrentFolderPrefix(), peerPrefix);
        QCOMPARE(peerList->rowCount(), peerRows);
        QCOMPARE(peerAddress->text(), draft);
        if (!peerRoot) {
            QCOMPARE(peerList->currentIndex().data().toString(), QString("peer.txt"));
            QCOMPARE(peerList->selectionModel()->selectedRows().size(), 1);
            QVERIFY(peerPayload.open(QIODevice::ReadOnly));
            QCOMPARE(peerPayload.readAll(), QByteArray("peer bytes"));
        }
        if (peerRoot) {
            auto *rootButton =
                peer->findChild<QPushButton *>(QString("panelParentButton%1").arg(1 - pane));
            QVERIFY(rootButton && !rootButton->isEnabled());
            QVERIFY(peerList->sourcePanel->IsRootFolder());
            QCOMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
            QCOMPARE(list->rowCount(), deferred ? 258 : 1);
        }
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("protected bytes"));
    }
    void changedDirectoryKeepsBindFailure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString initial = fixture.filePath("initial");
        const QString destination = fixture.filePath(QStringLiteral("destination 空.7z"));
        const QString encrypted = fixture.filePath("encrypted.7z");
        QVERIFY(QDir().mkpath(initial));
        QVERIFY(QDir().mkpath(destination));
        NativeArchiveFixture::Item item;
        item.Path = "keep.txt";
        item.Bytes = "protected bytes";
        QVERIFY(NativeArchiveFixture::Write(encrypted, {item}, "7z", "fixture password"));
        QFile keep(QDir(initial).filePath("keep.txt"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(initial);
        auto *list = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(list->isEnabled());
        QCOMPARE(list->rowCount(), 1);
        bool attempted = false, replaced = false, passwordCancelled = false;
        const auto replacement =
            connect(list->model(), &QAbstractItemModel::rowsRemoved, &window, [&] {
                if (attempted)
                    return;
                attempted = true;
                replaced = QDir().rmdir(destination) && QFile::copy(encrypted, destination);
            });
        const auto disconnectReplacement = qScopeGuard([&] { disconnect(replacement); });
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *password = dynamic_cast<CPasswordDialog *>(modal)) {
                passwordCancelled = true;
                password->reject();
            } else if (auto *message = qobject_cast<QMessageBox *>(modal))
                message->accept();
            else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close))
                    close->click();
            }
        });
        responder.start(1);
        window.openPath(destination);
        QVERIFY(replaced);
        QVERIFY(passwordCancelled);
        QVERIFY(list->isEnabled());
        QVERIFY(list->isSortingEnabled());
        QCOMPARE(list->rowCount(), 1);
        QCOMPARE(list->model()->index(0, 0).data().toString(), QString("keep.txt"));
        QCOMPARE(QDir::cleanPath(ToQString(list->sourcePanel->GetCurrentFolderPrefix())),
                 QFileInfo(initial).canonicalFilePath());
        QVERIFY(!backend.busy());
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
    void manualArchiveRefreshKeepsScroll_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<int>("count");
        QTest::newRow("table") << false << 80;
        QTest::newRow("icons-deferred") << true << 270;
    }
    void manualArchiveRefreshKeepsScroll() {
        QFETCH(bool, icons);
        QFETCH(int, count);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("scroll.7z");
        QList<NativeArchiveFixture::Item> items;
        for (int index = 0; index < count; ++index)
            items.append({QString("file-%1.txt").arg(index, 3, 10, QChar('0')), "payload"});
        QVERIFY(NativeArchiveFixture::Write(archive, items));
        MainWindow window;
        window.resize(640, 400);
        window.show();
        window.openArchive(archive);
        auto *list = table(window);
        QTRY_COMPARE(list->rowCount(), count);
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *view = qobject_cast<QAbstractItemView *>(
            window.findChild<QStackedWidget *>("fileViewStack")->currentWidget());
        QVERIFY(view);
        list->setCurrentCell(0, 0);
        list->selectRow(0);
        view->setFocus();
        auto *scroll = view->verticalScrollBar();
        QTRY_VERIFY(scroll->maximum() > 2);
        const int position = scroll->maximum() / 2;
        scroll->setValue(position);
        QSignalSpy refreshed(list->model(), &QAbstractItemModel::rowsRemoved);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(!refreshed.isEmpty());
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(view->isEnabled());
        QTRY_COMPARE(scroll->value(), position);
        QCOMPARE(list->currentRow(), 0);
        QVERIFY(view->selectionModel()->isRowSelected(0, {}));
    }
    void archiveOpenCompletion_data() {
        QTest::addColumn<int>("origin");
        QTest::addColumn<bool>("cancel");
        for (int origin : {0, 1, 2})
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(origin).arg(cancel)))
                    << origin << cancel;
    }
    void encryptedArchivePassword_data() {
        QTest::addColumn<QString>("password");
        QTest::addColumn<bool>("accept");
        QTest::addColumn<bool>("retry");
        QTest::newRow("accept") << QStringLiteral("fixture пароль") << true << false;
        QTest::newRow("empty") << QStringLiteral("") << true << false;
        QTest::newRow("cancel") << QStringLiteral("fixture пароль") << false << false;
        QTest::newRow("wrong-then-retry") << QStringLiteral("fixture пароль") << true << true;
    }
    void encryptedArchivePassword() {
        QFETCH(QString, password);
        QFETCH(bool, accept);
        QFETCH(bool, retry);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("архив с паролем.7z"));
        const QString entry = QStringLiteral("данные 空.txt");
        QVERIFY(NativeArchiveFixture::Write(path, {{entry, "secret"}}, "7z", password));
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto original = archive.readAll();
        archive.close();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *list = window.findChild<FileTable *>("archiveTable");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QTRY_VERIFY(list->isEnabled());
        const QString previous = address->text();
        int prompts = 0;
        QString error;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget())) {
                ++prompts;
                dialog->findChild<QLineEdit *>("passwordEdit")
                    ->setText(retry && prompts == 1 ? QStringLiteral("wrong") : password);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            } else if (auto *message =
                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                error = message->text();
                message->accept();
            }
        });
        responder.start(5);
        window.openArchive(path);
        QCOMPARE(prompts, 1);
        if (retry) {
            QCOMPARE(address->text(), previous);
            QVERIFY(list->isEnabled());
            QVERIFY(!backend.busy());
            QVERIFY(!error.isEmpty() || !window.statusBar()->currentMessage().isEmpty());
            window.openArchive(path);
            QCOMPARE(prompts, 2);
        }
        QCOMPARE(QDir::cleanPath(address->text()), QDir::cleanPath(accept ? path : previous));
        QCOMPARE(list->rowCount(), 1);
        if (accept)
            QCOMPARE(list->model()->index(0, 0).data().toString(), entry);
        QVERIFY(list->isEnabled());
        QVERIFY(!backend.busy());
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), original);
    }
    void archiveOpenCompletion() {
        QFETCH(int, origin);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString plain = fixture.filePath(QStringLiteral("ordinary 文件.bin"));
        QFile input(plain);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("ordinary data"), qint64(13));
        input.close();
        const QString valid = fixture.filePath(QStringLiteral("valid archive.zip"));
        const QString entry = QStringLiteral("данные 空.txt");
        QVERIFY(NativeArchiveFixture::Write(valid, {{entry, "payload"}}, "zip"));
        const QString encrypted = fixture.filePath("encrypted.7z");
        if (cancel)
            QVERIFY(NativeArchiveFixture::Write(encrypted, {{entry, "secret"}}, "7z",
                                                QStringLiteral("fixture password")));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *list = window.findChild<FileTable *>("archiveTable");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        if (origin == 0)
            window.openPath(fixture.path());
        else {
            action(window, "openRootAction")->trigger();
            if (origin == 1)
                action(window, "parentFolderAction")->trigger();
        }
        QTRY_VERIFY(list->isEnabled());
        const QString previousPath = address->text();
        const auto names = [&] {
            QStringList result;
            for (int row = 0; row < list->rowCount(); ++row)
                result.append(list->model()->index(row, 0).data().toString());
            return result;
        };
        const QStringList previousNames = names();
        QVERIFY(!previousNames.isEmpty());
        const bool couldCreate = action(window, "createFolderAction")->isEnabled();
        bool passwordCancelled = false, refused = false;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *password = dynamic_cast<CPasswordDialog *>(modal)) {
                passwordCancelled = true;
                password->reject();
            } else if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                refused = true;
                message->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close))
                    close->click();
            }
        });
        respond.start(1);
        window.openArchive(cancel ? encrypted : plain);
        QApplication::processEvents();
        QCOMPARE(passwordCancelled, cancel);
        if (!cancel)
            QVERIFY(refused);
        QCOMPARE(address->text(), previousPath);
        QCOMPARE(names(), previousNames);
        QVERIFY(list->isEnabled());
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(action(window, "createFolderAction")->isEnabled(), couldCreate);
        action(window, "refreshAction")->trigger();
        QCOMPARE(names(), previousNames);
        window.openArchive(valid);
        QCOMPARE(list->rowCount(), 1);
        QCOMPARE(
            list->model()->index(0, 0).data().toString().normalized(QString::NormalizationForm_C),
            entry.normalized(QString::NormalizationForm_C));
        QCOMPARE(QDir::cleanPath(address->text()), QDir::cleanPath(valid));
        QVERIFY(list->isEnabled());
        QVERIFY(!backend.busy());
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("ordinary data"));
    }
    void filesystemParentKeepsKeyboardFocus_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("addressFocus");
        for (const QString &lang : {QString("-"), QString("ru")})
            for (unsigned pane : {0u, 1u})
                for (int mode : {0, 3})
                    for (bool address : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(lang)
                                                     .arg(pane)
                                                     .arg(mode)
                                                     .arg(address ? "address" : "list")))
                            << lang << pane << mode << address;
    }
    void filesystemParentKeepsKeyboardFocus() {
        QFETCH(QString, language);
        QFETCH(unsigned, pane);
        QFETCH(int, mode);
        QFETCH(bool, addressFocus);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString childName = QStringLiteral("папка space 密碼");
        const QString child = fixture.filePath(childName);
        QVERIFY(QDir().mkpath(child));
        QFile payload(QDir(child).filePath(QStringLiteral("protected файл.txt")));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        const QByteArray bytes("parent keyboard bytes");
        QCOMPARE(payload.write(bytes), qint64(bytes.size()));
        payload.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        for (unsigned index : {1 - pane, pane}) {
            openFilesystemPanel(window, index, child);
            QVERIFY(!QTest::currentTestFailed());
        }
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *table = owner->findChild<FileTable *>("archiveTable");
        auto *address = owner->findChild<QLineEdit *>("archivePath");
        auto *peerAddress = peer->findChild<QLineEdit *>("archivePath");
        const QString draft = QStringLiteral("peer черновик 密碼");
        peerAddress->setText(draft);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *view = owner->findChild<QStackedWidget *>("fileViewStack")->currentWidget();
        QWidget *const control = addressFocus ? static_cast<QWidget *>(address) : view;
        if (addressFocus)
            PanelKeys::focusAddress(address);
        else
            control->setFocus();
        QTRY_VERIFY(control->hasFocus());
        // Editable QComboBox uses a focus proxy for its line editor. Preserve
        // the actual public focus owner, rather than assuming the child owns it.
        QWidget *const expected = window.focusWidget();
        QVERIFY(expected);
        if (addressFocus)
            action(window, "parentFolderAction")->trigger();
        else
            QTest::keyClick(view, Qt::Key_Backspace);
        QTRY_COMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
        QTRY_VERIFY(table->isEnabled());
        QTRY_VERIFY(table->currentRow() >= 0);
        QTRY_COMPARE(table->model()->index(table->currentRow(), 0).data().toString(), childName);
        QTRY_COMPARE(window.focusWidget(), expected);
        QCOMPARE(peerAddress->text(), draft);
        QCOMPARE(peer->findChild<FileTable *>("archiveTable")->rowCount(), 1);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QTRY_COMPARE(window.focusWidget(), expected);
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::fullyVisible(
                *(addressFocus ? address : PanelKeys::navigationFor(address)), window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("parent-focus-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), bytes);
        QCOMPARE(backend.extractCalls, 0);
    }

    void deferredParentRespectsNewerInteraction_data() {
        QTest::addColumn<int>("interaction");
        QTest::newRow("restore-list") << 0;
        QTest::newRow("address-draft") << 1;
        QTest::newRow("peer-focus") << 2;
    }
    void deferredParentRespectsNewerInteraction() {
        QFETCH(int, interaction);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString childName = QStringLiteral("z child папка");
        const QString child = fixture.filePath(childName);
        QVERIFY(QDir().mkpath(child));
        QFile payload(QDir(child).filePath("protected.txt"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("protected"), qint64(9));
        payload.close();
        // Cross the existing filesystem publication threshold with one bounded
        // fixture, rather than expanding a large archive/performance matrix.
        for (unsigned index = 0; index < 257; ++index)
            QVERIFY(QDir().mkdir(fixture.filePath(QString("entry-%1").arg(index))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        for (unsigned pane : {1u, 0u}) {
            openFilesystemPanel(window, pane, child);
            QVERIFY(!QTest::currentTestFailed());
        }
        auto *owner = window.findChild<QWidget *>("filePanel0");
        auto *peer = window.findChild<QWidget *>("filePanel1");
        auto *list = owner->findChild<FileTable *>("archiveTable");
        auto *address = owner->findChild<QLineEdit *>("archivePath");
        auto *peerAddress = peer->findChild<QLineEdit *>("archivePath");
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        QTest::keyClick(list, Qt::Key_Backspace);
        QVERIFY(!list->isEnabled());
        QWidget *expected = list;
        const QString draft = "unsubmitted parent draft";
        if (interaction) {
            expected = interaction == 1 ? address : peerAddress;
            PanelKeys::focusAddress(expected);
            QTRY_VERIFY(expected->hasFocus());
            expected = window.focusWidget();
            QVERIFY(expected);
            if (interaction == 1) {
                address->selectAll();
                QTest::keyClicks(address, draft);
            }
        }
        QTRY_VERIFY(list->isEnabled());
        QTRY_COMPARE(list->rowCount(), 258);
        QTRY_COMPARE(list->currentIndex().data(QtPanelList::NameRole).toString(), childName);
        QVERIFY(list->verticalScrollBar()->value() > 0);
        QVERIFY(list->viewport()->rect().intersects(list->visualRect(list->currentIndex())));
        QTRY_COMPARE(window.focusWidget(), expected);
        if (interaction == 1)
            QCOMPARE(address->text(), draft);
        QCOMPARE(peerAddress->text(), QFileInfo(child).canonicalFilePath());
        QCOMPARE(peer->findChild<FileTable *>("archiveTable")->rowCount(), 1);
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("protected"));
        QCOMPARE(backend.extractCalls, 0);
    }

    void addressRefreshAndParentFocus() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString name = QStringLiteral("папка space/внутри.txt");
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("папка space"))));
        QFile payload(dir.filePath(name));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("before"), 6);
        payload.close();
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        const QString archive = dir.filePath("address.zip");
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {payload.fileName()}));
        QFile sibling(dir.filePath("000-neighbor.txt"));
        QVERIFY(sibling.open(QIODevice::WriteOnly));
        sibling.close();
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(dir.path());
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *refresh = window.findChild<QAction *>("refreshAction");
        QVERIFY(table && address && refresh);
        action(window, "viewMode3Action")->trigger();
        QTRY_VERIFY(table->isEnabled() &&
                    filesystemRow(window, QStringLiteral("address.zip")) >= 0);
        const int archiveRow = filesystemRow(window, QStringLiteral("address.zip"));
        table->setCurrentCell(archiveRow, 0);
        table->selectRow(archiveRow);
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keyClick(table, Qt::Key_Return);
        QTRY_COMPARE(address->text(), archive);
        QTRY_COMPARE(table->rowCount(), 1);
        // Native binding clamps a missing suffix to the deepest existing folder.
        PanelKeys::focusAddress(address);
        address->setText(archive + QStringLiteral(" / missing/deeper"));
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(address->text(), archive);
        table->selectRow(0);
        QVERIFY(payload.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(payload.write("after refresh with a different size"), 35);
        payload.close();
        QProcess update;
        update.setWorkingDirectory(QFileInfo(payload.fileName()).absolutePath());
        const QString executable = QStandardPaths::findExecutable("7z");
        QVERIFY(!executable.isEmpty());
        update.start(executable,
                     {"u", "-y", "--", archive, QFileInfo(payload.fileName()).fileName()});
        QVERIFY(update.waitForFinished(30000));
        QCOMPARE(update.exitCode(), 0);
        refresh->trigger();
        QCOMPARE(table->rowCount(), 1);
        QTRY_COMPARE(table->model()->index(0, 2).data(QtPanelList::SortValueRole).toULongLong(),
                     35ull);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "viewMode3Action")->trigger();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keyClick(table, Qt::Key_Backspace);
        QTRY_COMPARE(address->text(), QFileInfo(dir.path()).canonicalFilePath());
        QTRY_COMPARE(table->currentIndex().data().toString(), QStringLiteral("address.zip"));
        QTRY_VERIFY(table->hasFocus());
        QTest::keyClick(table, Qt::Key_Return);
        QTRY_COMPARE(address->text(), archive);
        QTRY_COMPARE(table->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 2).data(QtPanelList::SortValueRole).toULongLong(), 35ull);
    }
};
QTEST_MAIN(NativeNavigationWindowTests)
#include "native_navigation_window_tests.moc"
