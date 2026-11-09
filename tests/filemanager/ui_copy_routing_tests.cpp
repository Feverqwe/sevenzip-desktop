#include "archive/support/native_archive_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

class UiCopyRoutingTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    static void createArchive(const QString &path, const QStringList &files) {
        QVERIFY(NativeArchiveFixture::WriteFiles(path, files));
    }
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
                return i;
        return -1;
    }
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = panel->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return panel->findChild<FileTable *>("archiveTable");
    }
    static QString inspectAndCancel(MainWindow &window, FileTable &view, bool move, bool menu,
                                    const QSize &size, const QString &tag) {
        bool seen = false;
        bool inspected = false;
        QString summary;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            seen = true;
            dialog->resize(size);
            dialog->layout()->activate();
            summary = ToQString(static_cast<CopyDialog *>(dialog)->Info)
                          .normalized(QString::NormalizationForm_C);
            inspected = UiInspection::writeReport(*dialog, tag);
            dialog->reject();
        });
        if (menu)
            action(window, move ? "moveAction" : "copyAction")->trigger();
        else
            QTest::keyClick(&view, move ? Qt::Key_F6 : Qt::Key_F5);
        QTest::qVerify(seen, "seen", "Copy dialog opened", __FILE__, __LINE__);
        QTest::qVerify(inspected, "inspected", "Widget JSON written", __FILE__, __LINE__);
        return summary;
    }
    static void submit(MainWindow &window, FileTable &sourceView, int secondRow, bool move,
                       bool &dialogSeen, QString &errors, const QString &destination) {
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (dialog && dialog->isVisible() && !dialogSeen) {
                dialogSeen = true;
                sourceView.selectRow(secondRow);
                sourceView.setCurrentCell(secondRow, 0);
                if (!destination.isEmpty())
                    dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
                if (dialog->isVisible())
                    dialog->reject();
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(widget))
                    ReplyDelete(*confirmation);
                else if (auto *result = TestProgressDialog::ResultWindow(widget)) {
                    errors += TestProgressDialog::ReportText(*result);
                    TestProgressDialog::CloseResult(*result);
                } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    if (box->icon() == QMessageBox::Question)
                        box->button(QMessageBox::Yes)->click();
                    else {
                        errors += box->text();
                        box->accept();
                    }
                }
            }
        });
        driver.start(1);
        QTest::keyClick(&sourceView, move ? Qt::Key_F6 : Qt::Key_F5);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
    }
    static void inspectCompletedPanels(MainWindow &window, FileTable &source, FileTable &target,
                                       const QString &tag) {
        for (const QSize size : {QSize(1100, 640), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(source, window));
            QVERIFY(UiInspection::fullyVisible(target, window));
            QVERIFY(UiInspection::writeReport(window, tag + '-' + QString::number(size.width())));
        }
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void unavailablePeerIsNotProposed_data() {
        QTest::addColumn<QString>("state");
        QTest::newRow("read-only") << QString("read-only");
        QTest::newRow("replaced-by-file") << QString("file");
#ifdef Q_OS_UNIX
        QTest::newRow("replaced-by-link") << QString("link");
#endif
    }
    void unavailablePeerIsNotProposed() {
        QFETCH(QString, state);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString peer = fixture.filePath("read only");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(peer));
        write(source + "/file.txt", "source bytes");
        write(peer + "/peer.txt", "peer bytes");
        const auto restore = qScopeGuard([&] {
            if (state == "read-only")
                QFile::setPermissions(peer, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        });
        if (state == "read-only" &&
            (!QFile::setPermissions(peer, QFile::ReadOwner | QFile::ExeOwner) ||
             QFileInfo(peer).isWritable()))
            QSKIP("Cannot make the peer folder read-only");

        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *target = openPanel(window, 1, peer);
        QTRY_VERIFY(target->isEnabled() && row(*target, "peer.txt") >= 0);
        auto *files = openPanel(window, 0, source);
        QTRY_VERIFY(files->isEnabled() && row(*files, "file.txt") >= 0);
        if (state != "read-only") {
            target->sourcePanel->Set_AutoRefresh_Mode(false);
            const QString parked = fixture.filePath("parked peer");
            QVERIFY(QDir().rename(peer, parked));
            if (state == "file")
                write(peer, "replacement");
            else
                QVERIFY(QFile::link(parked, peer));
        }
        files->setCurrentCell(row(*files, "file.txt"), 0);
        files->selectRow(row(*files, "file.txt"));
        files->setFocus();
        QVERIFY(action(window, "copyAction")->isEnabled());

        const QString output = fixture.filePath("manual output");
        QVERIFY(QDir().mkpath(output));
        const QString destination = state == "file" ? peer : output + '/';
        bool shown = false;
        int overwritePrompts = 0;
        QTimer overwrite;
        connect(&overwrite, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *box = dynamic_cast<COverwriteDialog *>(widget); box && box->isVisible()) {
                    ++overwritePrompts;
                    box->findChild<QPushButton *>("overwriteYes")->click();
                } else
                    TestProgressDialog::DismissVisibleResult(widget);
            }
        });
        overwrite.start(5);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            shown = true;
            const auto cancel = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *path = dialog->findChild<QLineEdit *>("copyDestination");
            auto *otherPanel = dialog->findChild<QPushButton *>("copyPeerDestination");
            QVERIFY(path && otherPanel && otherPanel->isVisible());
            QCOMPARE(QDir::cleanPath(path->text()), QFileInfo(source).canonicalFilePath());
            QVERIFY(!otherPanel->isEnabled());
            path->setText(destination);
            auto *ok =
                dialog->findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
            QVERIFY(ok->isEnabled());
            dialog->accept();
        });
        action(window, "copyAction")->trigger();
        QVERIFY(shown);
        QTRY_COMPARE(read(state == "file" ? peer : output + "/file.txt"),
                     QByteArray("source bytes"));
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(overwritePrompts, state == "file" ? 1 : 0);
        QCOMPARE(read(source + "/file.txt"), QByteArray("source bytes"));
        QVERIFY(!QFileInfo::exists(peer + "/file.txt"));
    }
    void sourceReloadPublishesNamespace() {
        ArchiveBackend backend;
        MainWindow window(&backend);
        action(window, "openRootAction")->trigger();
        auto *view = static_cast<FileTable *>(table(window));
        QVERIFY(view->sourcePanel->IsFSDrivesFolder());
        const auto names = [view] {
            QStringList result;
            for (int i = 0; i < view->rowCount(); ++i)
                result.append(view->model()->index(i, 0).data().toString());
            return result;
        };
        const auto before = names();
        QVERIFY(!before.isEmpty());
        view->clearRows();
        view->sourcePanel->OnReload();
        QCOMPARE(names(), before);
    }
    void outsidePeerStillRefreshesDestination_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<bool>("large");
        QTest::addColumn<QString>("outcome");
        for (unsigned sourceIndex : {0u, 1u})
            for (bool large : {false, true})
                for (const char *outcome : {"copy", "move", "dialog-cancel", "overwrite-cancel"})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(sourceIndex).arg(large).arg(outcome)))
                        << sourceIndex << large << QString::fromLatin1(outcome);
        QTest::newRow("relative-copy") << 0u << false << QString("relative-copy");
        QTest::newRow("relative-move") << 1u << false << QString("relative-move");
        QTest::newRow("new-batch-folder") << 0u << false << QString("new-batch-folder");
        QTest::newRow("overwrite-cancel-copy") << 1u << false << QString("overwrite-cancel-copy");
        QTest::newRow("overwrite-cancel-move") << 0u << false << QString("overwrite-cancel-move");
        QTest::newRow("overwrite-skip-move") << 0u << false << QString("overwrite-skip-move");
        QTest::newRow("overwrite-skip-copy") << 0u << false << QString("overwrite-skip-copy");
        QTest::newRow("missing-copy") << 0u << false << QString("missing-copy");
        QTest::newRow("missing-move") << 1u << false << QString("missing-move");
        QTest::newRow("link-copy") << 0u << false << QString("link-copy");
    }
    void outsidePeerStillRefreshesDestination() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(bool, large);
        QFETCH(QString, outcome);
        const bool cancel = outcome == "dialog-cancel";
        const bool skip = outcome.startsWith("overwrite-skip-");
        const bool cancelBatch = outcome.startsWith("overwrite-cancel-");
        const bool missing = outcome.startsWith("missing-");
        const bool overwrite = skip || outcome.startsWith("overwrite-cancel");
        const bool move = outcome.endsWith("move");
        const bool relative = outcome.startsWith("relative-");
        const bool newBatchFolder = outcome == "new-batch-folder";
        const bool linked = outcome == "link-copy";
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        const QString peer = fixture.filePath("peer");
        const QString output = fixture.filePath("output");
        for (const QString &path : {input, peer})
            QVERIFY(QDir().mkpath(path));
        if (!newBatchFolder)
            QVERIFY(QDir().mkpath(output));
        const QString link = fixture.filePath("linked destination");
        const QString alias = fixture.filePath("linked parent");
        if (linked) {
#ifdef Q_OS_WIN
            QSKIP("The directory symlink fixture requires POSIX");
#else
            QVERIFY(QFile::link(output, link));
            QVERIFY(QFile::link(fixture.path(), alias));
#endif
        }
        const QString name = QStringLiteral("payload δ.txt");
        const QString other = QStringLiteral("another file.txt");
        write(QDir(input).filePath(name), "control bytes");
        if (skip || missing || newBatchFolder || cancelBatch)
            write(QDir(input).filePath(other), "move these bytes");
        write(QDir(peer).filePath("seed.txt"), "seed control");
        if (overwrite)
            write(QDir(output).filePath(name), "existing control");
        if (cancelBatch)
            write(QDir(output).filePath(other), "other existing bytes");
        const int originalRows = large ? 301 : 1;
        for (int i = 1; i < originalRows; ++i)
            write(QDir(peer).filePath(QString("item-%1").arg(i)), "control");
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *sourceView = openPanel(window, sourceIndex, input);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        auto *peerView = openPanel(window, 1 - sourceIndex, peer);
        QTRY_COMPARE(peerView->rowCount(), originalRows);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        for (auto *view : {sourceView, peerView})
            static_cast<FileTable *>(view)->sourcePanel->Set_AutoRefresh_Mode(false);
        peerView->selectRow(row(*peerView, "seed.txt"));
        sourceView->selectRow(row(*sourceView, name));
        if (skip || missing || newBatchFolder || cancelBatch)
            sourceView->selectionModel()->select(
                sourceView->model()->index(row(*sourceView, other), 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QVERIFY(WindowActivation::waitForActive(window));
        sourceView->setFocus();
        QTRY_VERIFY(sourceView->hasFocus());
        bool seen = false;
        bool removed = false;
        bool reviewed = false;
        int refusedDestinations = 0;
        int overwritePrompts = 0;
        bool retry = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (dialog && dialog->isVisible() && !seen) {
                seen = true;
                write(QDir(peer).filePath("new while guarded.txt"), "new control");
                auto *path = dialog->findChild<QLineEdit *>("copyDestination");
                if (linked) {
                    const auto *buttons = dialog->findChild<QDialogButtonBox *>("copyButtons");
                    auto *ok = buttons->button(QDialogButtonBox::Ok);
                    for (const QString &blocked :
                         {link, alias + "/input/", alias + "/input/" + name}) {
                        path->setText(blocked);
                        if (!ok->isEnabled())
                            ++refusedDestinations;
                    }
                }
#ifdef Q_OS_WIN
                if (relative) {
                    path->setText("../wrong?/output/");
                    dialog->accept();
                    if (dialog->isVisible())
                        ++refusedDestinations;
                }
#endif
                if (relative)
                    path->setText(move ? "../unneeded/../output/" : "../unneeded/../output");
                else
                    path->setText(newBatchFolder ? output : output + '/');
                if (missing)
                    removed = QFile::remove(QDir(input).filePath(other));
                if (cancel && !retry)
                    QTest::keyClick(dialog->findChild<QLineEdit *>("copyDestination"),
                                    Qt::Key_Escape);
                else
                    dialog->accept();
                if (dialog->isVisible())
                    dialog->reject();
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                if (auto *box = dynamic_cast<COverwriteDialog *>(widget)) {
                    ++overwritePrompts;
                    if (retry || (cancelBatch && overwritePrompts == 1))
                        box->findChild<QPushButton *>("overwriteYes")->click();
                    else if (cancelBatch)
                        QTest::keyClick(box, Qt::Key_Escape);
                    else
                        box->findChild<QPushButton *>(skip ? "overwriteNo" : "overwriteCancel")
                            ->click();
                } else if (auto *box = qobject_cast<QMessageBox *>(widget))
                    box->accept();
                else
                    reviewed |= TestProgressDialog::DismissVisibleResult(widget);
            }
        });
        driver.start(1);
        QTest::keyClick(sourceView, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(seen);
        if (linked)
            QCOMPARE(refusedDestinations, 3);
#ifdef Q_OS_WIN
        if (relative)
            QCOMPARE(refusedDestinations, 1);
#endif
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        if (cancel || overwrite || missing) {
            QTRY_COMPARE(window.focusWidget(), sourceView);
            QCOMPARE(sourceView->selectionModel()->selectedRows().size(),
                     cancelBatch || (skip && !move) ? 2 : 1);
            QVERIFY(sourceView->selectionModel()->isRowSelected(row(*sourceView, name), {}));
        }
        QTRY_COMPARE_WITH_TIMEOUT(peerView->rowCount(), originalRows + (cancel ? 0 : 1), 1000);
        QCOMPARE(QFileInfo::exists(QDir(output).filePath(name)), !cancel && !missing);
        QVERIFY(!QFileInfo::exists(fixture.filePath("unneeded")));
        QCOMPARE(read(QDir(output).filePath(name)), cancel || missing ? QByteArray()
                                                    : overwrite ? QByteArray("existing control")
                                                                : QByteArray("control bytes"));
        QCOMPARE(read(QDir(input).filePath(name)),
                 move && !overwrite && !missing ? QByteArray() : QByteArray("control bytes"));
        if (missing) {
            QVERIFY(removed && reviewed);
            QVERIFY(!QFileInfo::exists(QDir(output).filePath(other)));
        }
        if (skip) {
            QCOMPARE(QFileInfo::exists(QDir(input).filePath(other)), !move);
            QCOMPARE(read(QDir(output).filePath(other)), QByteArray("move these bytes"));
            if (move) {
                retry = true;
                seen = false;
                QTest::keyClick(window.focusWidget(), Qt::Key_F6);
                QVERIFY(seen);
                QTRY_COMPARE(read(QDir(output).filePath(name)), QByteArray("control bytes"));
                QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
                QCOMPARE(overwritePrompts, 2);
                QVERIFY(!QFileInfo::exists(QDir(input).filePath(name)));
                QCOMPARE(read(QDir(output).filePath(other)), QByteArray("move these bytes"));
            }
        }
        if (cancelBatch) {
            QCOMPARE(overwritePrompts, 2);
            QVERIFY(sourceView->selectionModel()->isRowSelected(row(*sourceView, other), {}));
            QCOMPARE(read(QDir(input).filePath(other)), QByteArray("move these bytes"));
            QCOMPARE(read(QDir(output).filePath(other)), QByteArray("other existing bytes"));
        }
        if (newBatchFolder) {
            QCOMPARE(read(QDir(output).filePath(other)), QByteArray("move these bytes"));
            QCOMPARE(read(QDir(input).filePath(other)), QByteArray("move these bytes"));
        }
        QCOMPARE(peerView->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(peerView->model()
                     ->index(peerView->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QString("seed.txt"));
        inspectCompletedPanels(
            window, *sourceView, *peerView,
            QString("copy-outside-peer-%1-%2-%3").arg(sourceIndex).arg(large).arg(outcome));
        if (cancel || (overwrite && !skip)) {
            const int promptsBeforeRetry = overwritePrompts;
            retry = true;
            seen = false;
            QTest::keyClick(window.focusWidget(), move ? Qt::Key_F6 : Qt::Key_F5);
            QVERIFY(seen);
            QTRY_COMPARE(read(QDir(output).filePath(name)), QByteArray("control bytes"));
            QTRY_COMPARE(peerView->rowCount(), originalRows + 1);
            QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
            QCOMPARE(overwritePrompts - promptsBeforeRetry, cancelBatch ? 2 : int(overwrite));
            if (cancelBatch) {
                QCOMPARE(QFileInfo::exists(QDir(input).filePath(name)), !move);
                QCOMPARE(QFileInfo::exists(QDir(input).filePath(other)), !move);
                if (!move)
                    QCOMPARE(read(QDir(input).filePath(other)), QByteArray("move these bytes"));
                QCOMPARE(read(QDir(output).filePath(other)), QByteArray("move these bytes"));
            } else {
                QCOMPARE(read(QDir(input).filePath(name)), QByteArray("control bytes"));
            }
            QTRY_COMPARE(window.focusWidget(), sourceView);
        }
        if (outcome == "copy" || outcome == "move") {
            QTRY_COMPARE(window.focusWidget(), sourceView);
            QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
            QTRY_COMPARE(window.focusWidget(), peerView);
        }
    }
    void unselectedItemsReachSourceSmartCopy_data() {
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("menu");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (bool archive : {false, true})
            for (bool move : {false, true})
                for (bool menu : {false, true})
                    for (const char *language : {"-", "ru"})
                        for (const QSize size : {QSize(440, 250), QSize(360, 200)})
                            QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5")
                                                         .arg(archive)
                                                         .arg(move)
                                                         .arg(menu)
                                                         .arg(language)
                                                         .arg(size.width())))
                                << archive << move << menu << QString::fromLatin1(language) << size;
    }
    void unselectedItemsReachSourceSmartCopy() {
        QFETCH(bool, archive);
        QFETCH(bool, move);
        QFETCH(bool, menu);
        QFETCH(QString, language);
        QFETCH(QSize, size);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        QVERIFY(QDir().mkpath(input));
        const QString first = QStringLiteral("первый δ.txt");
        const QString second = QStringLiteral("второй 文件.txt");
        const QByteArray bytes("unchanged\0control", 17);
        for (const QString &name : {first, second})
            write(QDir(input).filePath(name), bytes);
        const QString source = archive ? fixture.filePath("source archive.zip") : input;
        if (archive)
            createArchive(source, {QDir(input).filePath(first), QDir(input).filePath(second)});
        const QByteArray archiveBefore = archive ? read(source) : QByteArray();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        FileTable *view = openPanel(window, 0, source);
        QTRY_COMPARE(static_cast<FileTable *>(view)->sourcePanel->GetParentFolders().Size(),
                     archive ? 1u : 0u);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(view->rowCount(), 2);
        QVERIFY(WindowActivation::waitForActive(window));
        view->setCurrentCell(0, 0);
        view->clearSelection();
        static_cast<FileTable *>(view)->sourcePanel->KillSelection();
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QVERIFY(view->selectionModel()->selectedRows().isEmpty());
        QVERIFY(action(window, "copyAction")->isEnabled());
        QVERIFY(action(window, "moveAction")->isEnabled());
        const QString summary = inspectAndCancel(window, *view, move, menu, size,
                                                 QString("copy-smart-%1-%2-%3-%4-%5")
                                                     .arg(archive)
                                                     .arg(move)
                                                     .arg(menu)
                                                     .arg(language)
                                                     .arg(size.width()));
        QVERIFY(summary.contains(first));
        QVERIFY(summary.contains(second));
        for (const QString &name : {first, second})
            QCOMPARE(read(QDir(input).filePath(name)), bytes);
        if (archive)
            QCOMPARE(read(source), archiveBefore);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
                 QStringList{"input"});
    }
    void rejectsDestinationInsideSourceFolder_data() {
        QTest::addColumn<bool>("move");
        QTest::newRow("copy") << false;
        QTest::newRow("move") << true;
    }

    void rejectsDestinationInsideSourceFolder() {
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("source folder δ");
        const QString source = fixture.filePath(name);
        const QString blocked = source + "/new folder/";
        const QString blockedInput = move ? name + "/new folder/" : blocked;
        const QString output = fixture.filePath("source folder δ copy");
        QVERIFY(QDir().mkpath(source));
        write(source + "/file.txt", "control bytes");

        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(row(*view, name), 0);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        view->selectRow(0);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());

        bool stayedOpen = false;
        bool corrected = false;
        QString diagnostic;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            auto *path = dialog->findChild<QLineEdit *>("copyDestination");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("copyButtons");
            auto *ok = buttons->button(QDialogButtonBox::Ok);
            path->setText(blockedInput);
            diagnostic = dialog->findChild<QLabel *>("copyDestinationMessage")->text();
            if (!ok->isEnabled()) {
                dialog->accept();
                stayedOpen = dialog->isVisible() && path->text() == blockedInput;
            }
            path->setText(output);
            corrected = ok->isEnabled();
            corrected ? dialog->accept() : dialog->reject();
        });
        QTest::keyClick(view, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(stayedOpen);
        QCOMPARE(diagnostic, QString("Destination is inside the source folder: %1").arg(blocked));
        QVERIFY(corrected);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(!QFileInfo::exists(blocked));
        QCOMPARE(read(output + "/file.txt"), QByteArray("control bytes"));
        QCOMPARE(QFileInfo::exists(source), !move);
    }

    void capturedItemsSurviveModalSelectionChange_data() {
        QTest::addColumn<bool>("sourceArchive");
        QTest::addColumn<bool>("targetArchive");
        QTest::addColumn<bool>("move");
        QTest::addColumn<QString>("language");
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<bool>("largeLists");
        QTest::addColumn<bool>("typedArchivePath");
        for (bool sourceArchive : {false, true})
            for (bool targetArchive : {false, true})
                for (bool move : {false, true})
                    for (const char *language : {"-", "ru"})
                        for (unsigned sourceIndex : {0u, 1u})
                            QTest::newRow(qPrintable(QString("%1-to-%2-%3-%4-%5")
                                                         .arg(sourceArchive ? "archive" : "fs")
                                                         .arg(targetArchive ? "archive" : "fs")
                                                         .arg(move ? "move" : "copy")
                                                         .arg(language)
                                                         .arg(sourceIndex)))
                                << sourceArchive << targetArchive << move
                                << QString::fromLatin1(language) << sourceIndex << false << false;
        for (unsigned sourceIndex : {0u, 1u}) {
            QTest::newRow(qPrintable(QString("large-fs-to-fs-move-%1").arg(sourceIndex)))
                << false << false << true << QString("ru") << sourceIndex << true << false;
            QTest::newRow(qPrintable(QString("large-fs-to-archive-move-%1").arg(sourceIndex)))
                << false << true << true << QString("ru") << sourceIndex << true << false;
        }
        QTest::newRow("typed-archive-copy")
            << false << true << false << QString("-") << 0u << false << true;
        QTest::newRow("typed-archive-move")
            << false << true << true << QString("-") << 1u << false << true;
    }
    void capturedItemsSurviveModalSelectionChange() {
        QFETCH(bool, sourceArchive);
        QFETCH(bool, targetArchive);
        QFETCH(bool, move);
        QFETCH(QString, language);
        QFETCH(unsigned, sourceIndex);
        QFETCH(bool, largeLists);
        QFETCH(bool, typedArchivePath);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(input));
        QVERIFY(QDir().mkpath(output));
        const QString first = QStringLiteral("первый δ.txt");
        const QString second = QStringLiteral("второй 文件.txt");
        const QByteArray firstBytes("first\0control", 13);
        const QByteArray secondBytes("second control", 14);
        write(QDir(input).filePath(first), firstBytes);
        write(QDir(input).filePath(second), secondBytes);
        QStringList targetFiles;
        if (largeLists)
            for (int i = 0; i < 300; ++i) {
                const QString name = QString("filler-%1.txt").arg(i, 3, 10, QChar('0'));
                write(QDir(input).filePath(name), "source filler");
                const QString path = QDir(output).filePath(name);
                write(path, "target filler");
                targetFiles << path;
            }
        const QString source = sourceArchive ? fixture.filePath("source archive.zip") : input;
        const QString target = targetArchive ? fixture.filePath("target archive.zip") : output;
        if (sourceArchive)
            createArchive(source, {QDir(input).filePath(first), QDir(input).filePath(second)});
        const QString seed = fixture.filePath("seed.txt");
        write(seed, "seed control");
        targetFiles << seed;
        if (targetArchive)
            createArchive(target, targetFiles);
        else
            write(QDir(output).filePath("seed.txt"), "seed control");
        const QByteArray sourceBefore = sourceArchive ? read(source) : QByteArray();
        QtWorkspaceApplication application(*qApp);
        ArchiveBackend backend, observerBackend, sourceObserverBackend;
        MainWindow window(&backend);
        std::unique_ptr<MainWindow> observer;
        if ((targetArchive || sourceArchive) && !typedArchivePath) {
            observer = std::make_unique<MainWindow>(&observerBackend);
            observer->show();
            if (targetArchive)
                observer->openArchive(target);
            else
                observer->openPath(target);
            QTRY_VERIFY(row(*table(*observer), "seed.txt") >= 0);
            table(*observer)->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        std::unique_ptr<MainWindow> sourceObserver;
        if (sourceArchive && move && !typedArchivePath) {
            sourceObserver = std::make_unique<MainWindow>(&sourceObserverBackend);
            sourceObserver->show();
            sourceObserver->openArchive(source);
            QTRY_VERIFY(row(*table(*sourceObserver), first) >= 0);
            table(*sourceObserver)->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(action(window, "twoPanelsAction")->isChecked());
        FileTable *sourceView = openPanel(window, sourceIndex, source);
        QTRY_COMPARE(static_cast<FileTable *>(sourceView)->sourcePanel->GetParentFolders().Size(),
                     sourceArchive ? 1u : 0u);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(sourceView->rowCount(), largeLists ? 302 : 2);
        FileTable *targetView = openPanel(window, 1 - sourceIndex, target);
        QTRY_COMPARE(static_cast<FileTable *>(targetView)->sourcePanel->GetParentFolders().Size(),
                     targetArchive ? 1u : 0u);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(targetView->rowCount(), largeLists ? 301 : 1);
        CMyComPtr<IFolderFolder> sourceProvider =
            static_cast<FileTable *>(sourceView)->sourcePanel->GetFolder();
        CMyComPtr<IFolderFolder> targetProvider =
            static_cast<FileTable *>(targetView)->sourcePanel->GetFolder();
        targetView->selectRow(row(*targetView, "seed.txt"));
        QVERIFY(WindowActivation::waitForActive(window));
        const int firstRow = row(*sourceView, first);
        const int secondRow = row(*sourceView, second);
        QVERIFY(firstRow >= 0 && secondRow >= 0);
        sourceView->selectRow(firstRow);
        sourceView->setCurrentCell(firstRow, 0);
        sourceView->setFocus();
        QTRY_VERIFY(sourceView->hasFocus());
        bool dialogSeen = false;
        QString errors;
        const QString destination = typedArchivePath ? ".././target archive.zip/" : QString();
        submit(window, *sourceView, secondRow, move, dialogSeen, errors, destination);
        QVERIFY(dialogSeen);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        if (!sourceArchive)
            QCOMPARE(static_cast<FileTable *>(sourceView)->sourcePanel->GetFolder(),
                     sourceProvider.Interface());
        if (!targetArchive)
            QCOMPARE(static_cast<FileTable *>(targetView)->sourcePanel->GetFolder(),
                     targetProvider.Interface());
        if (targetArchive) {
            const QString engine = QStandardPaths::findExecutable("7z");
            QVERIFY(!engine.isEmpty());
            QProcess verifier;
            verifier.start(engine, {"x", "-y", "-o" + output, "--", target});
            QVERIFY(verifier.waitForFinished(10000));
            QCOMPARE(verifier.exitCode(), 0);
            QCOMPARE(read(QDir(output).filePath("seed.txt")), QByteArray("seed control"));
        }
        QCOMPARE(read(QDir(output).filePath(first)), firstBytes);
        QVERIFY(!QFileInfo::exists(QDir(output).filePath(second)));
        QTRY_VERIFY(row(*targetView, first) >= 0);
        if (observer)
            QTRY_VERIFY(row(*table(*observer), first) >= 0);
        QTRY_COMPARE(targetView->rowCount(), (largeLists ? 301 : 1) + 1);
        QVERIFY(row(*targetView, second) < 0);
        QCOMPARE(read(QDir(input).filePath(second)), secondBytes);
        if (sourceArchive) {
            if (sourceObserver) {
                QTRY_COMPARE(row(*table(*sourceObserver), first), -1);
                QVERIFY(row(*table(*sourceObserver), second) >= 0);
            }
            if (!move)
                QCOMPARE(read(source), sourceBefore);
            QTRY_COMPARE(sourceView->rowCount(), move ? 1 : 2);
            QVERIFY(row(*sourceView, second) >= 0);
            QCOMPARE(row(*sourceView, first) >= 0, !move);
        } else {
            QCOMPARE(QFileInfo::exists(QDir(input).filePath(first)), !move);
            QTRY_COMPARE(sourceView->rowCount(), (largeLists ? 302 : 2) - int(move));
            QCOMPARE(row(*sourceView, first) >= 0, !move);
        }
        QTRY_VERIFY_WITH_TIMEOUT(sourceView->selectionModel()->selectedRows().isEmpty(), 1000);
        // The offscreen plugin loses its active window on modal close. Restore
        // activation only; the source continuation must have remembered focus.
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY_WITH_TIMEOUT(sourceView->hasFocus(), 1000);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(window.windowTitle())),
                 QDir::cleanPath(QDir::fromNativeSeparators(source)));
        QVERIFY(sourceView->currentIndex().isValid());
        QCOMPARE(
            sourceView->currentIndex().data().toString().normalized(QString::NormalizationForm_C),
            second);
        QCOMPARE(targetView->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(targetView->model()
                     ->index(targetView->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QString("seed.txt"));
        QCOMPARE(read(QDir(output).filePath("seed.txt")), QByteArray("seed control"));
        inspectCompletedPanels(window, *sourceView, *targetView,
                               QString("copy-completed-%1-%2-%3-%4-%5")
                                   .arg(sourceArchive)
                                   .arg(targetArchive)
                                   .arg(move)
                                   .arg(language)
                                   .arg(sourceIndex));
    }
};

QTEST_MAIN(UiCopyRoutingTests)
#include "ui_copy_routing_tests.moc"
