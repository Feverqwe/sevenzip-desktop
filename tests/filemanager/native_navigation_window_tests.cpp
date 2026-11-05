#include "archive/support/native_archive_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/progress_dialog.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
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
        address->setFocus();
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
        for (unsigned index : {pane, 1 - pane}) {
            openFilesystemPanel(window, index, child);
            QVERIFY(!QTest::currentTestFailed());
        }
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
        if (peerRoot) {
            auto *rootButton =
                peer->findChild<QPushButton *>(QString("panelParentButton%1").arg(1 - pane));
            QVERIFY(rootButton && rootButton->isEnabled());
            QTest::mouseClick(rootButton, Qt::LeftButton);
            QVERIFY(peerList->sourcePanel->IsRootFolder());
            QCOMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
            QCOMPARE(list->rowCount(), deferred ? 258 : 1);
        }
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("protected bytes"));
    }
    void changedDirectoryKeepsBindFailure_data() {
        QTest::addColumn<bool>("flat");
        QTest::newRow("normal") << false;
        QTest::newRow("flat") << true;
    }
    void changedDirectoryKeepsBindFailure() {
        QFETCH(bool, flat);
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
        list->sourcePanel->SetFlatModeForDisk(flat);
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
        QCOMPARE(list->rowCount(), 0);
        QVERIFY(!backend.busy());
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
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
        QTest::newRow("accept") << QStringLiteral("fixture пароль") << true;
        QTest::newRow("empty") << QStringLiteral("") << true;
        QTest::newRow("cancel") << QStringLiteral("fixture пароль") << false;
    }
    void encryptedArchivePassword() {
        QFETCH(QString, password);
        QFETCH(bool, accept);
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
        bool observed = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget())) {
                observed = true;
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            }
        });
        responder.start(5);
        window.openArchive(path);
        QVERIFY(observed);
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
                for (int mode : {0, 1, 2, 3})
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
            QVERIFY(UiInspection::fullyVisible(*address, window));
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
        const QString child = fixture.filePath(QStringLiteral("child папка"));
        QVERIFY(QDir().mkpath(child));
        QFile payload(QDir(child).filePath("protected.txt"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("protected"), qint64(9));
        payload.close();
        // Cross the existing filesystem publication threshold with one bounded
        // fixture, rather than expanding a large archive/performance matrix.
        for (unsigned index = 0; index < 257; ++index) {
            QFile file(fixture.filePath(QString("entry-%1.txt").arg(index)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
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
            QTest::mouseClick(expected, Qt::LeftButton);
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
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *refresh = window.findChild<QAction *>("refreshAction");
        auto *parent = window.findChild<QAction *>("parentFolderAction");
        QVERIFY(table && address && refresh && parent);
        QCOMPARE(table->rowCount(), 1);
        // Native binding clamps a missing suffix to the deepest existing folder.
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
        // OnReload loads the bound proxy; physical changes require reopening.
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("6"));
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "viewMode3Action")->trigger();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        parent->trigger();
        QCOMPARE(address->text(), QFileInfo(dir.path()).canonicalFilePath());
        QCOMPARE(table->model()->index(table->currentRow(), 0).data().toString(),
                 QStringLiteral("address.zip"));
        QTRY_VERIFY(table->hasFocus());
        window.openArchive(archive);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("35"));
    }
};
QTEST_MAIN(NativeNavigationWindowTests)
#include "native_navigation_window_tests.moc"
