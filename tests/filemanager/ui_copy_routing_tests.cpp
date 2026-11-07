#include "archive/support/native_archive_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QProcess>
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
                       bool &dialogSeen, QString &errors) {
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (dialog && dialog->isVisible() && !dialogSeen) {
                dialogSeen = true;
                sourceView.selectRow(secondRow);
                sourceView.setCurrentCell(secondRow, 0);
                dialog->accept();
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
    }
    void outsidePeerStillRefreshesDestination() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(bool, large);
        QFETCH(QString, outcome);
        const bool cancel = outcome == "dialog-cancel";
        const bool overwrite = outcome == "overwrite-cancel";
        const bool move = outcome == "move";
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        const QString peer = fixture.filePath("peer");
        const QString output = fixture.filePath("output");
        for (const QString &path : {input, peer, output})
            QVERIFY(QDir().mkpath(path));
        const QString name = QStringLiteral("payload δ.txt");
        write(QDir(input).filePath(name), "control bytes");
        write(QDir(peer).filePath("seed.txt"), "seed control");
        if (overwrite)
            write(QDir(output).filePath(name), "existing control");
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
        QVERIFY(WindowActivation::waitForActive(window));
        sourceView->setFocus();
        QTRY_VERIFY(sourceView->hasFocus());
        bool seen = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (dialog && dialog->isVisible() && !seen) {
                seen = true;
                write(QDir(peer).filePath("new while guarded.txt"), "new control");
                dialog->findChild<QLineEdit *>("copyDestination")->setText(output + '/');
                cancel ? dialog->reject() : dialog->accept();
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                if (auto *box = dynamic_cast<COverwriteDialog *>(widget))
                    box->findChild<QPushButton *>("overwriteCancel")->click();
                else if (auto *box = qobject_cast<QMessageBox *>(widget))
                    box->accept();
                else
                    TestProgressDialog::DismissVisibleResult(widget);
            }
        });
        driver.start(1);
        QTest::keyClick(sourceView, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(seen);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(peerView->rowCount(), originalRows + (cancel ? 0 : 1), 1000);
        QCOMPARE(QFileInfo::exists(QDir(output).filePath(name)), !cancel);
        QCOMPARE(read(QDir(output).filePath(name)), cancel      ? QByteArray()
                                                    : overwrite ? QByteArray("existing control")
                                                                : QByteArray("control bytes"));
        QCOMPARE(read(QDir(input).filePath(name)),
                 move ? QByteArray() : QByteArray("control bytes"));
        QCOMPARE(peerView->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(peerView->model()
                     ->index(peerView->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QString("seed.txt"));
        inspectCompletedPanels(
            window, *sourceView, *peerView,
            QString("copy-outside-peer-%1-%2-%3").arg(sourceIndex).arg(large).arg(outcome));
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
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            auto *path = dialog->findChild<QLineEdit *>("copyDestination");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("copyButtons");
            auto *ok = buttons->button(QDialogButtonBox::Ok);
            path->setText(blocked);
            if (!ok->isEnabled()) {
                dialog->accept();
                stayedOpen = dialog->isVisible() && path->text() == blocked;
            }
            path->setText(output);
            corrected = ok->isEnabled();
            corrected ? dialog->accept() : dialog->reject();
        });
        QTest::keyClick(view, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(stayedOpen);
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
                                << QString::fromLatin1(language) << sourceIndex << false;
        for (unsigned sourceIndex : {0u, 1u}) {
            QTest::newRow(qPrintable(QString("large-fs-to-fs-move-%1").arg(sourceIndex)))
                << false << false << true << QString("ru") << sourceIndex << true;
            QTest::newRow(qPrintable(QString("large-fs-to-archive-move-%1").arg(sourceIndex)))
                << false << true << true << QString("ru") << sourceIndex << true;
        }
    }
    void capturedItemsSurviveModalSelectionChange() {
        QFETCH(bool, sourceArchive);
        QFETCH(bool, targetArchive);
        QFETCH(bool, move);
        QFETCH(QString, language);
        QFETCH(unsigned, sourceIndex);
        QFETCH(bool, largeLists);
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
        ArchiveBackend backend;
        MainWindow window(&backend);
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
        submit(window, *sourceView, secondRow, move, dialogSeen, errors);
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
        QTRY_COMPARE(targetView->rowCount(), (largeLists ? 301 : 1) + 1);
        QVERIFY(row(*targetView, second) < 0);
        QCOMPARE(read(QDir(input).filePath(second)), secondBytes);
        if (sourceArchive) {
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
