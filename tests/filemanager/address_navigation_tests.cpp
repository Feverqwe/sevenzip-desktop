#include "archive/support/native_archive_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/support/native_link_fixture.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include <QClipboard>
#include <QScrollBar>

class AddressNavigationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static bool writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void emptyAddressOpensLocationsAndBackRestoresTheFile() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(writeFile(fixture.filePath("selected.txt"), "source bytes"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 1);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        QVERIFY(writeFile(fixture.filePath("added.txt"), "new bytes"));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(".");
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(view->hasFocus() && view->sourcePanel->IsFSFolder());
        QTRY_COMPARE(view->rowCount(), 2);
        QCOMPARE(view->currentIndex().data().toString(), QString("selected.txt"));
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->clear();
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(view->hasFocus() && view->sourcePanel->IsRootFolder());
        QVERIFY(address->text().isEmpty());
        QVERIFY(view->rowCount() > 0);
        QTest::keySequence(view, action(window, "backAction")->shortcut());
        QTRY_VERIFY(view->hasFocus() && view->sourcePanel->IsFSFolder());
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())),
                 QFileInfo(fixture.path()).canonicalFilePath());
        QCOMPARE(view->currentIndex().data().toString(), QString("selected.txt"));
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
    }
    void cancelledPasswordKeepsAddressReadyForRetry() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("архив с паролем.7z"));
        const QString password = QStringLiteral("fixture secret");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"payload.txt", "payload"}}, "7z", password,
                                            true));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(filesystemRow(window, QFileInfo(archive).fileName()) >= 0);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(archive);
        address->setSelection(archive.size() - 3, 3);
        int prompts = 0;
        bool accept = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            ++prompts;
            if (accept) {
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                dialog->accept();
            } else
                dialog->reject();
        });
        responder.start(5);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(prompts, 1);
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(address->hasFocus());
        QCOMPARE(address->text(), archive);
        QCOMPARE(address->selectedText(), QString(".7z"));
        QVERIFY(table(window)->sourcePanel->IsFSFolder());
        accept = true;
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(rowWithPath(window, "payload.txt") >= 0);
        QCOMPARE(prompts, 2);
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(table(window)->hasFocus());
    }
    void archiveLinkUsesNativeOpening_data() {
        QTest::addColumn<QString>("route");
        QTest::addColumn<QString>("kind");
        for (const QString &route :
             {QString("explicit"), QString("path"), QString("address"), QString("item")})
            for (const QString &kind :
                 {QString("archive"), QString("ordinary"), QString("missing")})
                QTest::newRow(qPrintable(route + '-' + kind)) << route << kind;
    }
    void archiveLinkUsesNativeOpening() {
        QFETCH(QString, route);
        QFETCH(QString, kind);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath(QStringLiteral("источник 空.zip"));
        const QString link = fixture.filePath(QStringLiteral("ссылка space 空.zip"));
        const QByteArray payload("archive through link");
        if (kind == "archive") {
            NativeArchiveFixture::Item item;
            item.Path = QStringLiteral("данные 空.txt");
            item.Bytes = payload;
            QVERIFY(NativeArchiveFixture::Write(target, {item}, "zip"));
        } else if (kind == "ordinary")
            QVERIFY(writeFile(target, "ordinary file bytes"));
        QVERIFY(CreateNativeSymbolicLink(link, QFileInfo(target).fileName()));
        const auto sourceBytes = [&] {
            QFile source(target);
            return source.open(QIODevice::ReadOnly) ? source.readAll() : QByteArray();
        };
        const QByteArray original = sourceBytes();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled());
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->accept();
        });
        dismiss.start(5);
        if (route == "explicit")
            window.openArchive(link);
        else if (route == "path")
            window.openPath(link);
        else if (route == "address") {
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            address->setText(link);
            address->setModified(true);
            QTest::keyClick(address, Qt::Key_Return);
        } else {
            const int row = filesystemRow(window, QFileInfo(link).fileName());
            QVERIFY(row >= 0);
            view->selectRow(row);
            view->setCurrentCell(row, 0);
            action(window, "openInsideAction")->trigger();
        }
        QTRY_VERIFY(view->isEnabled());
        QCOMPARE(view->sourcePanel->IsArcFolder(), kind == "archive");
        if (kind == "archive") {
            QTRY_COMPARE(view->rowCount(), 1);
            QCOMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("данные 空.txt"));
            QVERIFY(address->text().contains(QFileInfo(link).fileName()));
            const QString output = fixture.filePath("output");
            window.extractTo(output);
            QFile extracted(QDir(output).filePath(QStringLiteral("данные 空.txt")));
            QTRY_VERIFY(QFileInfo::exists(extracted.fileName()));
            QVERIFY(extracted.open(QIODevice::ReadOnly));
            QCOMPARE(extracted.readAll(), payload);
            action(window, "parentFolderAction")->trigger();
            QTRY_VERIFY(view->isEnabled());
            QCOMPARE(QDir::cleanPath(address->text()), fixture.path());
        }
        dismiss.stop();
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(sourceBytes(), original);
        QCOMPARE(QFileInfo(target).exists(), kind != "missing");
        if (route == "address" && kind == "ordinary") {
            QVERIFY(WindowActivation::waitForActive(window));
            QTRY_VERIFY(address->hasFocus());
            QCOMPARE(address->text(), link);
            const QString repaired = fixture.filePath(QStringLiteral("corrected 空.zip"));
            QVERIFY(NativeArchiveFixture::Write(repaired, {{"recovered.txt", payload}}, "zip"));
            address->setText(repaired);
            address->setModified(true);
            QTest::keyClick(window.focusWidget(), Qt::Key_Return);
            QTRY_VERIFY(view->sourcePanel->IsArcFolder());
            QTRY_COMPARE(view->model()->index(0, 0).data().toString(), QString("recovered.txt"));
            QVERIFY(WindowActivation::waitForActive(window));
            QTRY_VERIFY(view->hasFocus());
        }
    }
    void rejectedArchiveAddressKeepsEditingFocus_data() {
        QTest::addColumn<qint32>("result");
        QTest::addColumn<bool>("virtualPath");
        QTest::addColumn<bool>("refreshing");
        QTest::newRow("refused") << qint32(S_FALSE) << false << false;
        QTest::newRow("failed") << qint32(E_FAIL) << false << false;
        QTest::newRow("cancelled") << qint32(E_ABORT) << false << false;
        QTest::newRow("virtual-failed") << qint32(E_FAIL) << true << false;
        QTest::newRow("virtual-cancelled") << qint32(E_ABORT) << true << false;
        QTest::newRow("cancelled-during-refresh") << qint32(E_ABORT) << false << true;
        QTest::newRow("virtual-failed-during-refresh") << qint32(E_FAIL) << true << true;
    }
    void rejectedArchiveAddressKeepsEditingFocus() {
        QFETCH(qint32, result);
        QFETCH(bool, virtualPath);
        QFETCH(bool, refreshing);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("архив с ошибкой 空.zip"));
        const QString draft = virtualPath ? path + QStringLiteral("/вложенная папка") : path;
        const QByteArray bytes("unchanged archive bytes");
        QVERIFY(writeFile(path, bytes));
        if (refreshing)
            for (int i = 0; i < 270; ++i)
                QVERIFY(writeFile(fixture.filePath(QString("item-%1.txt").arg(i)), "file"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled());
        int scroll = 0;
        const QString selected = QStringLiteral("item-200.txt");
        if (refreshing) {
            const int row = filesystemRow(window, selected);
            QVERIFY(row >= 0);
            view->selectRow(row);
            view->setCurrentCell(row, 0);
            view->scrollTo(view->currentIndex(), QAbstractItemView::PositionAtCenter);
            scroll = view->verticalScrollBar()->value();
            QVERIFY(scroll > 0);
            action(window, "refreshAction")->trigger();
            QVERIFY(!view->isEnabled());
        }
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        backend.queueListAction([&] { backend.fail("Archive opening refused", HRESULT(result)); });
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                warning->accept();
        });
        dismiss.start(5);
        address->selectAll();
        QApplication::clipboard()->setText(draft);
        QTest::keySequence(address, QKeySequence::Paste);
        QApplication::clipboard()->clear();
        QCOMPARE(address->text(), draft);
        const int selectionStart = draft.lastIndexOf(QStringLiteral("архив"));
        QVERIFY(selectionStart >= 0);
        address->setSelection(selectionStart + 5, -5);
        QTest::keyClick(address, Qt::Key_Return);
        dismiss.stop();
        QApplication::processEvents();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QCOMPARE(backend.listCalls, 1);
        QTRY_VERIFY(address->hasFocus());
        QTRY_VERIFY(view->isEnabled());
        if (refreshing) {
            QCOMPARE(view->currentIndex().siblingAtColumn(0).data().toString(), selected);
            QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
            QTRY_COMPARE(view->verticalScrollBar()->value(), scroll);
        }
        QVERIFY(address->isVisible());
        QCOMPARE(address->text(), draft);
        QCOMPARE(address->selectedText(), QStringLiteral("архив"));
        QCOMPARE(address->cursorPosition(), selectionStart);
        if (virtualPath && result != E_ABORT)
            QVERIFY(window.statusBar()->currentMessage().contains(draft));
        if (virtualPath && result == E_ABORT)
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(view->isEnabled());
        QCOMPARE(address->text(), draft);
        QCOMPARE(address->selectedText(), QStringLiteral("архив"));
        QVERIFY(action(window, "twoPanelsAction")->isEnabled());
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), bytes);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(view->hasFocus());
        QCOMPARE(backend.listCalls, 1);
    }
    void completedAddressNavigationFocusesLargeList_data() {
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("newInput");
        QTest::newRow("filesystem") << false << false;
        QTest::newRow("archive") << true << false;
        QTest::newRow("filesystem-new-input") << false << true;
        QTest::newRow("archive-new-input") << true << true;
    }
    void completedAddressNavigationFocusesLargeList() {
        QFETCH(bool, archive);
        QFETCH(bool, newInput);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = archive ? fixture.filePath("large.zip") : fixture.path();
        ControlledBackend backend;
        QList<ArchiveEntry> entries;
        for (int i = 0; i < 1024; ++i) {
            const QString name = QStringLiteral("файл 空 %1.txt").arg(i);
            if (archive)
                entries.append(ArchiveEntry{name, 1, 1});
            else
                QVERIFY(writeFile(fixture.filePath(name), "x"));
        }
        if (archive) {
            QVERIFY(backend.prepareArchiveFixture(target));
            backend.queueList(entries);
        }
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(target);
        QTest::keyClick(address, Qt::Key_Return);
        if (newInput) {
            address->selectAll();
            QTest::keyClicks(address, "new draft");
        }
        QTRY_VERIFY(view->isEnabled() && view->rowCount() >= 1024);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (newInput) {
            QTRY_VERIFY(address->hasFocus());
            QCOMPARE(address->text(), QString("new draft"));
        } else {
            QTRY_VERIFY(view->hasFocus());
        }
    }
    void boundReloadPreservesTimerDraft_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("timer");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool archive : {false, true})
                    for (bool timer : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(index)
                                                     .arg(language)
                                                     .arg(archive)
                                                     .arg(timer)))
                            << index << language << archive << timer;
    }
    void boundReloadPreservesTimerDraft() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, archive);
        QFETCH(bool, timer);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("entry 文件.txt");
        const QString zip = fixture.filePath(QStringLiteral("archive space δ.zip"));
        QVERIFY(writeFile(fixture.filePath(entry), "archive control"));
        if (archive)
            QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), zip, entry));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        PanelKeys::focusAddress(address);
        if (archive) {
            address->setText(zip);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(view->rowCount(), 1);
            QVERIFY(view->sourcePanel->IsArcFolder());
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            QApplication::processEvents();
        } else {
            action(window, "openRootAction")->trigger();
            action(window, "parentFolderAction")->trigger();
            QVERIFY(view->sourcePanel->IsRootFolder());
        }
        const int count = view->rowCount();
        const QString published = address->text();
        const QString title = window.windowTitle();
        const UString prefix = view->sourcePanel->GetCurrentFolderPrefix();
        CMyComPtr<IFolderFolder> folder = view->sourcePanel->GetFolder();
        const QString draft = QStringLiteral("draft 文件/not opened");
        PanelKeys::focusAddress(address);
        QVERIFY(address->hasFocus());
        address->setText(draft);
        address->setSelection(2, 5);
        const int cursor = address->cursorPosition();
        const QString selected = address->selectedText();
        view->sourcePanel->OnReload(timer);
        QCOMPARE(view->rowCount(), count);
        QCOMPARE(address->text(), timer ? draft : published);
        if (timer) {
            QCOMPARE(address->selectionStart(), 2);
            QCOMPARE(address->selectedText(), selected);
            QCOMPARE(address->cursorPosition(), cursor);
        }
        QVERIFY(address->hasFocus());
        QCOMPARE(view->sourcePanel->GetFolder(), (IFolderFolder *)folder);
        QVERIFY(view->sourcePanel->GetCurrentFolderPrefix() == prefix);
        QCOMPARE(window.windowTitle(), title);
        QVERIFY(view->sourcePanel->ShouldProcessNotifications());
        if (archive)
            QCOMPARE(ExternalEditorFixture::contents(zip, entry), QByteArray("archive control"));
    }
    void reloadPreservesTimerDraft_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("timer");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (int mode : {0, 1})
                    for (bool timer : {false, true})
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(index).arg(language).arg(mode).arg(timer)))
                            << index << language << mode << timer;
    }
    void reloadPreservesTimerDraft() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(int, mode);
        QFETCH(bool, timer);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString folder = fixture.filePath(QStringLiteral("folder space δ"));
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(folder) && QDir().mkpath(peer));
        QVERIFY(writeFile(QDir(peer).filePath("peer.txt"), "peer control"));
        const int count = mode == 1 ? 301 : 1;
        for (int i = 0; i < count; ++i)
            QVERIFY(writeFile(QDir(folder).filePath(QString("control-%1.txt").arg(i)), "control"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourceIndex));
        auto *peerAddress = peerUi->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(peerAddress);
        peerAddress->setText(peer);
        QTest::keyClick(peerAddress, Qt::Key_Return);
        auto *peerView = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(peerView->rowCount(), 1);
        peerView->selectRow(0);
        CMyComPtr<IFolderFolder> peerFolder = peerView->sourcePanel->GetFolder();
        peerView->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        PanelKeys::focusAddress(address);
        address->setText(folder);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), count);
        QTRY_VERIFY(view->sourcePanel->ShouldProcessNotifications());
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> sourceFolder = panel.GetFolder();
        const QString draft = QStringLiteral("../draft 文件/not opened");
        PanelKeys::focusAddress(address);
        address->setText(draft);
        address->setSelection(3, 7);
        const int cursor = address->cursorPosition();
        const QString selected = address->selectedText();
        QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("new 文件.txt")), "new control"));
        if (timer) {
            panel.Set_AutoRefresh_Mode(true);
            panel.MarkPortableFolderChanged();
            panel.OnTimer();
            panel.Set_AutoRefresh_Mode(false);
        } else
            action(window, "refreshAction")->trigger();
        QTRY_COMPARE(view->rowCount(), count + 1);
        QTRY_VERIFY(panel.ShouldProcessNotifications());
        if (timer) {
            QCOMPARE(address->text(), draft);
            QCOMPARE(address->selectionStart(), 3);
            QCOMPARE(address->selectedText(), selected);
            QCOMPARE(address->cursorPosition(), cursor);
        } else
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), folder);
        QVERIFY(address->hasFocus());
        if (mode == 0)
            QCOMPARE(panel.GetFolder(), (IFolderFolder *)sourceFolder);
        UInt32 loadedCount = 0;
        QCOMPARE(panel.GetFolder()->GetNumberOfItems(&loadedCount), S_OK);
        QCOMPARE(loadedCount, UInt32(count + 1));
        QCOMPARE(
            QDir::cleanPath(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix()))),
            folder);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(window.windowTitle())), folder);
        QCOMPARE(peerView->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peerView->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(peerAddress->text(), peer);
        for (const auto &pair :
             {qMakePair(QDir(folder).filePath("control-0.txt"), QByteArray("control")),
              qMakePair(QDir(peer).filePath("peer.txt"), QByteArray("peer control")),
              qMakePair(QDir(folder).filePath(QStringLiteral("new 文件.txt")),
                        QByteArray("new control"))}) {
            QFile file(pair.first);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), pair.second);
        }
        QCOMPARE(backend.listCalls, 0);
    }
    void missingAddressUsesSourceAncestor_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("relative");
        QTest::addColumn<bool>("trailing");
        QTest::addColumn<bool>("quoted");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool relative : {false, true})
                    for (bool trailing : {false, true}) {
                        const QString tag = QString("%1-%2-%3-%4")
                                                .arg(index)
                                                .arg(language)
                                                .arg(relative)
                                                .arg(trailing);
                        QTest::newRow(qPrintable(tag))
                            << index << language << relative << trailing << false;
#ifdef Q_OS_WIN
                        QTest::newRow(qPrintable(tag + "-quoted"))
                            << index << language << relative << trailing << true;
#endif
                    }
    }
    void missingAddressUsesSourceAncestor() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, relative);
        QFETCH(bool, trailing);
        QFETCH(bool, quoted);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
#ifdef Q_OS_UNIX
        const QString parent = fixture.filePath(QStringLiteral(" ancestor space δ "));
        const QString child = QDir(parent).filePath(QStringLiteral(" inside 文件 "));
#else
        const QString parent = fixture.filePath(QStringLiteral("ancestor space δ"));
        const QString child = QDir(parent).filePath(QStringLiteral("inside 文件"));
#endif
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(child) && QDir().mkpath(peer));
        const QString control = QDir(parent).filePath(QStringLiteral("control δ.txt"));
        const QString peerControl = QDir(peer).filePath("peer.txt");
        QVERIFY(writeFile(control, "ancestor control"));
        QVERIFY(writeFile(QDir(child).filePath("old.txt"), "old control"));
        QVERIFY(writeFile(peerControl, "peer control"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourceIndex));
        auto *peerAddress = peerUi->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(peerAddress);
        peerAddress->setText(peer);
        QTest::keyClick(peerAddress, Qt::Key_Return);
        auto *peerView = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(peerView->rowCount(), 1);
        peerView->selectRow(0);
        CMyComPtr<IFolderFolder> peerFolder = peerView->sourcePanel->GetFolder();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        PanelKeys::focusAddress(address);
        address->selectAll();
        QApplication::clipboard()->setText(child);
        QTest::keySequence(address, QKeySequence::Paste);
        QCOMPARE(address->text(), child);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 1);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), child);
        QString input = relative ? QStringLiteral("../missing 文件/deeper")
                                 : QDir(parent).filePath(QStringLiteral("missing 文件/deeper"));
        if (trailing)
            input += '/';
        if (quoted)
            input = '"' + input + '"';
        PanelKeys::focusAddress(address);
        address->setText(input);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 2);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), parent);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                     ToQString(view->sourcePanel->GetCurrentFolderPrefix()))),
                 parent);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(window.windowTitle())), parent);
        QTRY_VERIFY(view->hasFocus());
        QVERIFY(view->sourcePanel->ShouldProcessNotifications());
        QCOMPARE(peerView->rowCount(), 1);
        QCOMPARE(peerView->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peerView->selectionModel()->selectedRows().size(), 1);
        for (const auto &pair : {qMakePair(control, QByteArray("ancestor control")),
                                 qMakePair(peerControl, QByteArray("peer control"))}) {
            QFile file(pair.first);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), pair.second);
        }
        QVERIFY(!QFileInfo::exists(QDir(parent).filePath(QStringLiteral("missing 文件"))));
        QCOMPARE(backend.listCalls, 0);
    }
};

QTEST_MAIN(AddressNavigationTests)
#include "address_navigation_tests.moc"
