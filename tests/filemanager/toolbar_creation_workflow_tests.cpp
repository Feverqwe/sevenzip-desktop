#include "Common/MyTypes.h"
#include "filemanager/views/filetable.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "operations/qtoperationlauncher.h"
#include "settings/RegistryUtils.h"
#include "support/operation_temp_cleanup.h"
#include "support/panel_keys.h"
#include "support/scoped_environment.h"
#include "support/window_activation.h"
#include <QScrollBar>

namespace {
QAction *FindShellVerb(QMenu &menu, const QString &verb) {
    for (auto *item : menu.actions()) {
        if (item->property("shellVerb").toString() == verb)
            return item;
        if (item->menu())
            if (auto *found = FindShellVerb(*item->menu(), verb))
                return found;
    }
    return nullptr;
}
class CreationBackend final : public ArchiveBackend {
  public:
    QList<QtOperationResult> Results;
    QList<QtOperationJob> Requests;
    QString Target;
    bool Cancel = false;
    bool Hold = false;
    QPointer<QtOperationLauncher> Held;
    QtOperationLauncher *startGuiCompress(const QString &archive, const QString &type,
                                          bool addExtension, const QStringList &paths, bool email,
                                          bool showDialog, QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Compress;
        job.Paths = paths;
        job.ArchivePath = archive;
        job.ArchiveType = type;
        job.AddExtension = addExtension;
        job.EMail = email;
        job.ShowDialog = showDialog;
        Requests.append(job);
        if (Hold) {
            Held = new QtOperationLauncher(this);
            return Held;
        }
        ScopedTestEnvironment decision("SEVENZIP_OPERATION_CREATE_DECISION",
                                       Cancel ? "cancel" : "accept");
        ScopedTestEnvironment target("SEVENZIP_OPERATION_CREATE_TARGET", Target);
        ScopedTestEnvironment format("SEVENZIP_OPERATION_CREATE_FORMAT", "zip");
        auto *operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        if (operation)
            connect(operation, &QtOperationLauncher::finished, this,
                    [this](const QtOperationResult &result) { Results.append(result); });
        return operation;
    }
};
} // namespace

class ToolbarCreationWorkflowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CFmSettings preferences;
        preferences.Load(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        preferences.ShowDots = false;
        preferences.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void pendingOutputFollowsNavigation_data() {
        QTest::addColumn<QtOperationResult::Outcome>("outcome");
        QTest::newRow("success") << QtOperationResult::Outcome::Success;
        QTest::newRow("partial failure") << QtOperationResult::Outcome::Failure;
        QTest::newRow("partial cancellation") << QtOperationResult::Outcome::Cancelled;
    }
    void pendingOutputFollowsNavigation() {
        QFETCH(QtOperationResult::Outcome, outcome);
        QTemporaryDir input, output, elsewhere;
        QVERIFY(input.isValid() && output.isValid() && elsewhere.isValid());
        const QString source = input.filePath("payload 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(source, "selected bytes"));
        QVERIFY(SourceExtractFixture::writeFile(elsewhere.filePath("other.txt"), "other bytes"));
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        CreationBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Hold = true;
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, output.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        auto *selected = openPanel(window, 0, input.path());
        QTRY_COMPARE(selected->rowCount(), 1);
        selected->sourcePanel->Set_AutoRefresh_Mode(false);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        selected->selectRow(0);
        action(window, "toolbarAddAction")->trigger();
        QVERIFY(backend.Held);
        QVERIFY(QFile::remove(source));
        QVERIFY(SourceExtractFixture::writeFile(output.filePath("partial 空.txt"), "output bytes"));
        QtOperationResult result;
        result.Status = outcome;
        result.ChangedDirectories = {input.path(), output.path()};
        emit backend.Held->finished(result);
        // Navigate before deferred delivery. A child result must refresh the
        // current providers even when it reports partial/cancelled work.
        openPanel(window, 1, elsewhere.path());
        QTRY_COMPARE(peer->rowCount(), 1);
        QTRY_COMPARE(selected->rowCount(), 0);
        QCOMPARE(peer->model()->index(0, 0).data().toString(), QString("other.txt"));
        QCOMPARE(QDir::cleanPath(window.findChild<QWidget *>("filePanel1")
                                     ->findChild<QLineEdit *>("archivePath")
                                     ->text()),
                 QDir::cleanPath(elsewhere.path()));
        QCOMPARE(SourceExtractFixture::readFile(output.filePath("partial 空.txt")),
                 QByteArray("output bytes"));
    }
    void selectedFilesystemAddRefreshesActualInputAndOutput_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("cancel");
        for (unsigned pane : {0u, 1u})
            for (bool cancel : {false, true})
                QTest::newRow(
                    qPrintable(QString("pane %1 %2").arg(pane).arg(cancel ? "cancel" : "create")))
                    << pane << cancel;
    }
    void selectedFilesystemAddRefreshesActualInputAndOutput() {
        QFETCH(unsigned, pane);
        QFETCH(bool, cancel);
        QTemporaryDir input;
        QVERIFY(input.isValid());
        const QDir output(input.filePath("output"));
        QVERIFY(QDir().mkpath(output.path()));
        const QString source = input.filePath("payload 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(source, "manager selection payload"));
        const QString extra = input.filePath("second.txt");
        QVERIFY(SourceExtractFixture::writeFile(extra, "second payload"));
        QVERIFY(SourceExtractFixture::writeFile(input.filePath("unselected.txt"), "untouched"));
        const int fillerCount = 60;
        for (int row = 0; row < fillerCount; ++row)
            QVERIFY(SourceExtractFixture::writeFile(
                input.filePath(QString("view-%1.txt").arg(row, 3, 10, QChar('0'))), "filler"));
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        QCOMPARE(catalog.result(), S_OK);
        CreationBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Target = output.filePath("chosen archive 空.zip");
        backend.Cancel = cancel;
        MainWindow window(&backend);
        window.resize(700, 440);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, output.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        auto *selected = openPanel(window, pane, input.path());
        QTRY_COMPARE(selected->rowCount(), fillerCount + 4);
        selected->sourcePanel->Set_AutoRefresh_Mode(false);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        if (cancel) {
            for (int row = 0; row < selected->rowCount(); ++row)
                if (selected->model()->index(row, 0).data().toString() == "second.txt") {
                    selected->setCurrentCell(row, 0);
                    selected->selectRow(row);
                    break;
                }
            selected->setFocus();
            QTRY_VERIFY(selected->hasFocus());
            const QString parked = output.filePath("parked.txt");
            QVERIFY(QFile::rename(extra, parked));
            action(window, "toolbarAddAction")->trigger();
            QVERIFY(backend.Requests.isEmpty());
            QVERIFY(window.statusBar()->currentMessage().contains("second.txt"));
            QTRY_COMPARE(window.focusWidget(), selected);
            QVERIFY(selected->selectionModel()->isRowSelected(selected->currentRow(), {}));
            QVERIFY(QFile::rename(parked, extra));
        }
        for (int row = 0; row < selected->rowCount(); ++row) {
            const auto item = selected->model()->index(row, 0);
            if (item.data().toString() == "unselected.txt")
                selected->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
            else if (item.data().toString() == "second.txt" ||
                     item.data().toString().normalized(QString::NormalizationForm_C) ==
                         QStringLiteral("payload 空.txt"))
                selected->selectionModel()->select(item, QItemSelectionModel::Select |
                                                             QItemSelectionModel::Rows);
        }
        selected->setFocus();
        QTRY_VERIFY(selected->hasFocus());
        auto *scroll = selected->verticalScrollBar();
        QTRY_VERIFY(scroll->maximum() > 0);
        scroll->setValue(scroll->maximum() / 2);
        const int topRow = selected->rowAt(0);
        QVERIFY(topRow > selected->currentRow());
        const QString anchor = selected->model()->index(topRow, 0).data().toString();
        const int offset = selected->rowViewportPosition(topRow);
        const QString newEntry = QStringLiteral("a new file.txt");
        QVERIFY(SourceExtractFixture::writeFile(input.filePath(newEntry), "new input"));
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        action(window, "toolbarAddAction")->trigger();
        if (cancel)
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.first().Status, cancel ? QtOperationResult::Outcome::Cancelled
                                                        : QtOperationResult::Outcome::Success);
        if (cancel) {
            QCOMPARE(SourceExtractFixture::readFile(source),
                     QByteArray("manager selection payload"));
            QVERIFY(!QFileInfo::exists(backend.Target));
            QCOMPARE(selected->rowCount(), fillerCount + 4);
            QCOMPARE(peer->rowCount(), 0);
            QCOMPARE(selected->selectionModel()->selectedRows().size(), 2);
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            QTRY_VERIFY(selected->hasFocus());
            backend.Cancel = false;
            action(window, "toolbarAddAction")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 2, 30000);
            QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        }
        QTRY_COMPARE(selected->rowCount(), fillerCount + 5);
        QTRY_COMPARE(peer->rowCount(), 1);
        QCOMPARE(selected->rowAt(0), topRow + 1);
        QCOMPARE(selected->model()->index(selected->rowAt(0), 0).data().toString(), anchor);
        QCOMPARE(selected->rowViewportPosition(selected->rowAt(0)), offset);
        QStringList retainedSelection;
        for (const auto &item : selected->selectionModel()->selectedRows())
            retainedSelection.append(
                item.data().toString().normalized(QString::NormalizationForm_C));
        retainedSelection.sort();
        QCOMPARE(retainedSelection, (QStringList{QStringLiteral("payload 空.txt"), "second.txt"}));
        QCOMPARE(selected->currentIndex().data().toString(), QString("unselected.txt"));
        QTRY_COMPARE(window.focusWidget(), selected);
        QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
        QTRY_COMPARE(window.focusWidget(), peer);
        QCOMPARE(
            peer->model()->index(0, 0).data().toString().normalized(QString::NormalizationForm_C),
            QString("chosen archive 空.zip"));
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(peer->hasFocus());
        QTest::keyClick(peer, Qt::Key_Home);
        QCOMPARE(peer->currentRow(), 0);
        QTest::keySequence(peer, QKeySequence::SelectAll);
        QVERIFY(peer->selectionModel()->isRowSelected(0, {}));
        PanelKeys::openSelection(window.focusWidget());
        QTRY_VERIFY(peer->isEnabled() && peer->sourcePanel->IsArcFolder());
        QStringList archived;
        for (int row = 0; row < peer->rowCount(); ++row)
            archived.append(peer->model()->index(row, 0).data().toString().normalized(
                QString::NormalizationForm_C));
        archived.sort();
        QCOMPARE(archived, retainedSelection);
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(peer->hasFocus());
        QTest::keySequence(peer, action(window, "backAction")->shortcut());
        QTRY_VERIFY(peer->hasFocus() && peer->sourcePanel->IsFSFolder());
        QCOMPARE(peer->currentIndex().data().toString().normalized(QString::NormalizationForm_C),
                 QString("chosen archive 空.zip"));
        const QString verified = input.filePath("verified");
        QProcess extract;
        extract.start(QStandardPaths::findExecutable("7z"),
                      {"x", "-y", "-o" + verified, "--", backend.Target});
        QVERIFY(extract.waitForFinished(10000));
        QCOMPARE(extract.exitCode(), 0);
        QCOMPARE(
            SourceExtractFixture::readFile(QDir(verified).filePath(QFileInfo(source).fileName())),
            QByteArray("manager selection payload"));
        QCOMPARE(
            SourceExtractFixture::readFile(QDir(verified).filePath(QFileInfo(extra).fileName())),
            QByteArray("second payload"));
        QVERIFY(!QFileInfo::exists(QDir(verified).filePath("unselected.txt")));
    }
    void filesystemShellCompressionUsesChild_data() {
        QTest::addColumn<QString>("verb");
        for (const QString &verb :
             {QString("Compress"), QString("CompressTo7z"), QString("CompressToZip")})
            QTest::newRow(qPrintable(verb)) << verb;
    }
    void updateRefreshesAnAlreadyOpenedPeerWithoutMovingFocus() {
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString archive = SourceExtractFixture::makeArchive(output.path(), "old member");
        QVERIFY(!archive.isEmpty());
        const QString source = input.filePath("new member 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(source, "new member payload"));
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        CreationBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Target = archive;
        const QString reportPath = output.filePath("child-result");
        ScopedTestEnvironment report("SEVENZIP_OPERATION_REPORT", reportPath);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, archive);
        QTRY_COMPARE(peer->rowCount(), 1);
        auto *peerAddress =
            window.findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
        const QString peerPath = peerAddress->text();
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *selected = openPanel(window, 0, input.path());
        QTRY_COMPARE(selected->rowCount(), 1);
        selected->sourcePanel->Set_AutoRefresh_Mode(false);
        selected->selectRow(0);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        selected->setFocus();
        QTRY_VERIFY(selected->hasFocus());
        action(window, "toolbarAddAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QVERIFY2(backend.Results.first().Status == QtOperationResult::Outcome::Success,
                 SourceExtractFixture::readFile(reportPath + ".diagnostic").constData());
        QVERIFY(backend.Results.first().ChangedArchives.contains(archive));
        QTRY_COMPARE(peer->rowCount(), 2);
        QStringList names;
        for (int row = 0; row < peer->rowCount(); ++row)
            names.append(peer->model()->index(row, 0).data().toString().normalized(
                QString::NormalizationForm_C));
        QVERIFY(names.contains("new member 空.txt"));
        QVERIFY(names.contains("inside"));
        QCOMPARE(peerAddress->text(), peerPath);
        // The independent operation owns its foreground window. Returning to
        // the manager must restore its selected panel after the peer reload.
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(selected->hasFocus());
        QCOMPARE(QDir::cleanPath(window.findChild<QWidget *>("filePanel0")
                                     ->findChild<QLineEdit *>("archivePath")
                                     ->text()),
                 QDir::cleanPath(input.path()));
    }
    void packedInputArchiveRemainsOpenInPeer() {
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString archive = SourceExtractFixture::makeArchive(input.path(), "old member");
        QVERIFY(!archive.isEmpty());
        QVERIFY(QDir(input.filePath("inside")).removeRecursively());
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        CreationBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Target = output.filePath("packed.zip");
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, archive);
        QTRY_COMPARE(peer->rowCount(), 1);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *selected = openPanel(window, 0, input.path());
        QTRY_COMPARE(selected->rowCount(), 1);
        selected->sourcePanel->Set_AutoRefresh_Mode(false);
        selected->selectRow(0);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        selected->setFocus();
        QTRY_VERIFY(selected->hasFocus());
        action(window, "toolbarAddAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        QVERIFY(QFileInfo::exists(archive));
        QVERIFY(QFileInfo::exists(backend.Target));
        auto *peerAddress =
            window.findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
        QTRY_COMPARE(QDir::cleanPath(peerAddress->text()), QDir::cleanPath(archive));
        QTRY_COMPARE(peer->rowCount(), 1);
    }
    void filesystemShellCompressionUsesChild() {
        QFETCH(QString, verb);
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString source = input.filePath("selected 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(source, "context selected bytes"));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        CContextMenuInfo context;
        context.Load(settings);
        context.Flags = NContextMenuFlags::kCompress | NContextMenuFlags::kCompressTo7z |
                        NContextMenuFlags::kCompressToZip;
        context.Flags_Def = true;
        context.Cascaded.SetTrueTrue();
        context.Save(settings);
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        CreationBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Target = output.filePath("chosen.zip");
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *selected = openPanel(window, 0, input.path());
        QTRY_COMPARE(selected->rowCount(), 1);
        selected->sourcePanel->Set_AutoRefresh_Mode(false);
        selected->selectRow(0);
        selected->setFocus();
        auto *menu = window.findChild<QMenu *>("fileMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        auto *command = FindShellVerb(*menu, "SevenZip" + verb);
        QVERIFY(command && command->isEnabled());
        command->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        QCOMPARE(backend.Requests.size(), 1);
        const bool dialog = verb == "Compress";
        const QString target = dialog ? backend.Target : backend.Requests.first().ArchivePath;
        QVERIFY(QFileInfo::exists(target));
        if (dialog) {
            QVERIFY(QFileInfo::exists(source));
            QTRY_COMPARE(selected->rowCount(), 1);
        } else {
            QCOMPARE(SourceExtractFixture::readFile(source), QByteArray("context selected bytes"));
            QTRY_COMPARE(selected->rowCount(), 2);
        }
    }
};
QTEST_MAIN(ToolbarCreationWorkflowTests)
#include "toolbar_creation_workflow_tests.moc"
