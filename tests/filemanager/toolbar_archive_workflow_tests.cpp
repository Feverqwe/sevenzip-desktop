#include "Common/MyTypes.h"
#include "filemanager/views/filetable.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/extract/alias.h"
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/support/window_state_support.h"
#include "gui/resource2.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_temp_cleanup.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include "support/progress_dialog.h"
#include "support/scoped_environment.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTreeWidget>

namespace {
using namespace SourceExtractFixture;
QString FixtureTemplate() {
    return QDir(QFileInfo(QDir::tempPath()).canonicalFilePath()).filePath("toolbar-XXXXXX");
}
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
class GuiTestBackend final : public ArchiveBackend {
  public:
    QList<QtOperationResult> Results;
    QList<QStringList> Selections;
    bool CrashNext = false;
    bool Killed = false;
    bool CancelExtract = false;
    QString ExtractDestination;
    QString ExtractDialogReport;
    QString ExtractDialogTag;
    QString OutputBarrier;
    QList<QtOperationJob> ExtractJobs;
    QtOperationLauncher *startGuiExtract(const QStringList &archives, const QString &destination,
                                         bool showDialog, bool elimDup, quint32 writeZone,
                                         QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = archives;
        job.Destination = destination;
        job.ShowDialog = showDialog;
        job.ElimDup = elimDup;
        job.WriteZone = writeZone;
        job.YesToAll = true;
        ScopedTestEnvironment decision("SEVENZIP_OPERATION_EXTRACT_DECISION",
                                       CancelExtract ? "cancel" : "accept");
        ScopedTestEnvironment output("SEVENZIP_OPERATION_DESTINATION", ExtractDestination);
        ScopedTestEnvironment report("SEVENZIP_OPERATION_DIALOG_REPORT", ExtractDialogReport);
        ScopedTestEnvironment tag("SEVENZIP_OPERATION_DIALOG_TAG", ExtractDialogTag);
        ScopedTestEnvironment barrier("SEVENZIP_OPERATION_OUTPUT_BARRIER", OutputBarrier);
        auto *operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        if (operation) {
            ExtractJobs.append(job);
            connect(operation, &QtOperationLauncher::finished, this,
                    [this](const QtOperationResult &result) { Results.append(result); });
        }
        return operation;
    }
    QtOperationLauncher *startGuiTest(const QStringList &archives, QString &error,
                                      bool hashMode = false) override {
        QtOperationJob job;
        job.Paths = archives;
        job.HashMode = hashMode;
        job.YesToAll = !CrashNext;
        auto *operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_NATIVE_GUI), job, error);
        if (operation) {
            if (CrashNext) {
                CrashNext = false;
                connect(operation, &QtOperationLauncher::accepted, operation,
                        [this, operation] { Killed = KillOperationProbe(operation->processId()); });
            }
            Selections.append(archives);
            connect(operation, &QtOperationLauncher::finished, this,
                    [this](const QtOperationResult &result) { Results.append(result); });
        }
        return operation;
    }
};
bool makeArchive(const QString &directory, const QString &path, const QString &type,
                 const QString &name) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QStandardPaths::findExecutable("7z"), {"a", "-t" + type, "--", path, name});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}
void dismissResults(WidgetDialogFixture &dialogs, QStringList &errors, QStringList &successes) {
    if (auto *widget = dialogs.activeDialog()) {
        if (auto *result = TestProgressDialog::ResultWindow(widget)) {
            const QString report = TestProgressDialog::ReportText(*result);
            if (report.contains(ToQString(LangString(IDS_MESSAGE_NO_ERRORS))))
                successes << report;
            else
                errors << report;
            TestProgressDialog::CloseResult(*result);
        } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
            // Source ProgressDialog2 success MB_OK carries no icon flags.
            if (box->objectName() == "progressFinalMessage" && box->icon() == QMessageBox::NoIcon)
                successes << box->text();
            else
                errors << box->text();
            box->accept();
        } else if (auto *results = qobject_cast<CListViewDialog *>(widget)) {
            errors << results->windowTitle();
            results->accept();
        } else if (widget->objectName() == "progressDialog") {
            auto *close = widget->findChild<QPushButton *>("IDCANCEL");
            if (close && close->property("languageID").toUInt() == IDS_CLOSE) {
                if (auto *messages = widget->findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES"))
                    for (int row = 0; row < messages->topLevelItemCount(); ++row)
                        errors << messages->topLevelItem(row)->text(1);
                close->click();
            }
        }
    }
}
} // namespace

class ToolbarArchiveWorkflowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QString module_;
    quint32 version_ = 0;
    static bool showTwoPanels(MainWindow &window) {
        if (!QTest::qWaitFor([&] { return action(window, "refreshAction")->isEnabled(); }))
            return false;
        auto *twoPanels = action(window, "twoPanelsAction");
        if (!twoPanels->isChecked())
            twoPanels->trigger();
        return QTest::qWaitFor(
            [&] { return twoPanels->isChecked() && window.findChild<QWidget *>("filePanel1"); });
    }
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static void inspectDialog(QDialog &dialog, const QString &tag) {
        for (const QSize size : {QSize(700, 390), QSize(600, 340)}) {
            dialog.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::writeReport(dialog, tag + '-' + QString::number(size.width())));
        }
    }

  private slots:
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void initTestCase() {
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module_, 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void repeatedArchiveTesting_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("opened");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("7z")})
            for (bool opened : {false, true})
                QTest::newRow(qPrintable(format + (opened ? "-opened" : "-selected")))
                    << format << opened;
    }
    void repeatedArchiveTesting() {
        QFETCH(QString, format);
        QFETCH(bool, opened);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("данные с пробелом.txt");
        const QByteArray payload = QByteArray("archive test payload\n").repeated(10000);
        QVERIFY(writeFile(fixture.filePath(name), payload));
        const QString archive = fixture.filePath("archives/test." + format);
        QVERIFY(QDir().mkpath(QFileInfo(archive).absolutePath()));
        QVERIFY(makeArchive(fixture.path(), archive, format, name));
        const auto before = readFile(archive);
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *source = openPanel(window, 0, opened ? archive : QFileInfo(archive).absolutePath());
        QTRY_COMPARE(source->rowCount(), 1);
        source->selectRow(0);
        QStringList errors, successes;
        QTimer driver;
        dialogs_.connectResponder(driver, &window,
                                  [&] { dismissResults(dialogs_, errors, successes); });
        driver.start(1);
        if (!opened) {
            source->sourcePanel->Set_AutoRefresh_Mode(false);
            source->setFocus();
            const QString parked = fixture.filePath("parked." + format);
            QVERIFY(QFile::rename(archive, parked));
            PanelKeys::command(source, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
            QVERIFY(backend.Selections.isEmpty());
            QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(archive).fileName()));
            QTRY_COMPARE(window.focusWidget(), source);
            QVERIFY(source->selectionModel()->isRowSelected(0, {}));
            QVERIFY(QFile::rename(parked, archive));
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            const bool crash = !opened && attempt == 0;
            backend.CrashNext = crash;
            errors.clear();
            successes.clear();
            QVERIFY(action(window, "testArchiveAction")->isEnabled());
            source->setFocus();
            PanelKeys::command(source, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
            if (opened)
                QCOMPARE(successes.size(), 1);
            else {
                QVERIFY(window.statusBar()->currentMessage().isEmpty());
                QVERIFY(action(window, "refreshAction")->isEnabled());
                QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), attempt + 1, 30000);
                QCOMPARE(backend.Results.last().Status, crash
                                                            ? QtOperationResult::Outcome::Crashed
                                                            : QtOperationResult::Outcome::Success);
                if (crash) {
                    QVERIFY(backend.Killed);
                    QTRY_VERIFY(!errors.isEmpty());
                } else
                    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
                QCOMPARE(backend.Selections.last().size(), 1);
                QCOMPARE(QFileInfo(backend.Selections.last().first()).canonicalFilePath(),
                         QFileInfo(archive).canonicalFilePath());
            }
            QCOMPARE(readFile(archive), before);
        }
        if (opened)
            QVERIFY(backend.Selections.isEmpty()); // Live folder testing stays in CopyTo.
    }
    void filesystemToolbarFailureRestoresSource_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void filesystemToolbarFailureRestoresSource() {
        QFETCH(unsigned, pane);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        const QString archive = QDir(input).filePath(QStringLiteral("сломанный 空.zip"));
        const QByteArray before("invalid private ZIP bytes");
        QVERIFY(writeFile(archive, before));
        const QString output = fixture.filePath("output");
        const QString sentinel = QDir(output).filePath("keep.txt");
        QVERIFY(writeFile(sentinel, "keep"));
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(showTwoPanels(window));
        auto *source = openPanel(window, pane, input);
        QTRY_COMPARE(source->rowCount(), 1);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        source->selectRow(0);
        source->setCurrentCell(0, 0);
        source->setFocus();
        CMyComPtr<IFolderFolder> provider = source->sourcePanel->GetFolder();
        QStringList errors, successes;
        backend.ExtractDestination = output;
        QTimer driver;
        dialogs_.connectResponder(driver, &window,
                                  [&] { dismissResults(dialogs_, errors, successes); });
        driver.start(1);
        for (const char *command : {"extractArchiveAction", "testArchiveAction"}) {
            errors.clear();
            successes.clear();
            QVERIFY(action(window, command)->isEnabled());
            action(window, command)->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(),
                                      qstrcmp(command, "testArchiveAction") == 0 ? 2 : 1, 30000);
            QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Failure);
            QVERIFY(successes.isEmpty());
            QCOMPARE(source->sourcePanel->GetFolder(), provider.Interface());
            QCOMPARE(source->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(readFile(archive), before);
            QCOMPARE(readFile(sentinel), QByteArray("keep"));
            QCOMPARE(QDir(output).entryList(QDir::Files), QStringList{"keep.txt"});
            QVERIFY(!backend.busy());
            QVERIFY(source->sourcePanel->ShouldProcessNotifications());
            QVERIFY(action(window, "refreshAction")->isEnabled());
            QTRY_VERIFY(source->hasFocus());
        }
    }
    void mixedArchiveTestReportsFailureAndRetainsSelection() {
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("archives");
        QVERIFY(QDir().mkpath(input));
        const QString damaged = QDir(input).filePath(QStringLiteral("01 повреждённый.zip"));
        const QString healthy = QDir(input).filePath(QStringLiteral("02 исправный.zip"));
        const QByteArray invalid("invalid ZIP bytes");
        QVERIFY(writeFile(damaged, invalid));
        QVERIFY(writeFile(fixture.filePath("entry.txt"), "payload"));
        QVERIFY(makeArchive(fixture.path(), healthy, "zip", "entry.txt"));
        const auto before = readFile(healthy);
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *source = openPanel(window, 0, input);
        QTRY_COMPARE(source->rowCount(), 2);
        source->sourcePanel->Set_AutoRefresh_Mode(false);
        source->selectAll();
        source->setFocus();
        QStringList errors, successes;
        QTimer driver;
        dialogs_.connectResponder(driver, &window,
                                  [&] { dismissResults(dialogs_, errors, successes); });
        driver.start(1);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        const QString parkedDamaged = fixture.filePath("parked damaged.zip");
        const QString parkedHealthy = fixture.filePath("parked healthy.zip");
        QVERIFY(QFile::rename(damaged, parkedDamaged));
        QVERIFY(QFile::rename(healthy, parkedHealthy));
        action(window, "testArchiveAction")->trigger();
        QVERIFY(backend.Selections.isEmpty());
        QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(damaged).fileName()) ||
                window.statusBar()->currentMessage().contains(QFileInfo(healthy).fileName()));
        QCOMPARE(source->selectionModel()->selectedRows().size(), 2);
        QTRY_VERIFY(source->hasFocus());
        QVERIFY(QFile::rename(parkedDamaged, damaged));
        QVERIFY(QFile::rename(parkedHealthy, healthy));
        action(window, "testArchiveAction")->trigger();
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Failure);
        QCOMPARE(backend.Selections.size(), 1);
        QStringList tested;
        for (const auto &path : backend.Selections.first())
            tested.append(QFileInfo(path).canonicalFilePath());
        tested.sort();
        QCOMPARE(tested, QStringList({QFileInfo(damaged).canonicalFilePath(),
                                      QFileInfo(healthy).canonicalFilePath()}));
        QCOMPARE(source->selectionModel()->selectedRows().size(), 2);
        QTRY_VERIFY(source->hasFocus());
        const auto healthyRows =
            source->model()->match(source->model()->index(0, 0), QtPanelList::NameRole,
                                   QFileInfo(healthy).fileName(), 1, Qt::MatchExactly);
        QCOMPARE(healthyRows.size(), 1);
        QTest::mouseClick(source->viewport(), Qt::LeftButton, Qt::NoModifier,
                          source->visualRect(healthyRows.front()).center());
        QCOMPARE(source->selectionModel()->selectedRows(), healthyRows);
        PanelKeys::command(window.focusWidget(), Qt::Key_T,
                           Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 2, 30000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        QCOMPARE(backend.Selections.size(), 2);
        QCOMPARE(backend.Selections.last().size(), 1);
        QCOMPARE(QFileInfo(backend.Selections.last().front()).canonicalFilePath(),
                 QFileInfo(healthy).canonicalFilePath());
        QCOMPARE(readFile(damaged), invalid);
        QCOMPARE(readFile(healthy), before);
    }

    void archiveToolbarUsesSourceCopyTo_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void archiveToolbarUsesSourceCopyTo() {
        QFETCH(unsigned, pane);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("данные δ.txt");
        const QByteArray payload("source toolbar bytes");
        const QString other = QStringLiteral("second entry.txt");
        const QByteArray otherPayload("second entry bytes");
        QVERIFY(writeFile(fixture.filePath(name), payload));
        QVERIFY(writeFile(fixture.filePath(other), otherPayload));
        const QString archive = fixture.filePath("source.zip");
        QVERIFY(makeArchive(fixture.path(), archive, "zip", name));
        QVERIFY(makeArchive(fixture.path(), archive, "zip", other));
        const QByteArray before = readFile(archive);
        const QString output = fixture.filePath("peer output");
        QVERIFY(writeFile(QDir(output).filePath("peer.txt"), "peer bytes"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(showTwoPanels(window));
        auto *peer = openPanel(window, 1 - pane, output);
        QTRY_COMPARE(peer->rowCount(), 1);
        peer->selectRow(0);
        auto *source = openPanel(window, pane, archive);
        QTRY_COMPARE(source->rowCount(), 2);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        source->selectAll();
        source->setFocus();
        CMyComPtr<IFolderFolder> provider = source->sourcePanel->GetFolder();
        QString modalName;
        QString destination;
        QStringList errors;
        QStringList successes;
        bool retry = false;
        QTimer driver;
        dialogs_.connectResponder(driver, &window, [&] {
            if (auto *copy = dynamic_cast<CopyDialog *>(dialogs_.activeDialog())) {
                modalName = copy->objectName();
                destination = copy->destination();
                if (retry)
                    copy->accept();
                else {
                    inspectDialog(*copy, QString("toolbar-archive-copy-%1").arg(pane));
                    QTest::keyClick(copy, Qt::Key_Escape);
                }
            } else
                dismissResults(dialogs_, errors, successes);
        });
        driver.start(1);
        action(window, "extractArchiveAction")->trigger();
        driver.stop();
        QCOMPARE(modalName, QString("copyDialog"));
        QCOMPARE(QDir::cleanPath(destination), QDir::cleanPath(output));
        QCOMPARE(source->sourcePanel->GetFolder(), provider.Interface());
        QCOMPARE(readFile(archive), before);
        QCOMPARE(readFile(QDir(output).filePath("peer.txt")), QByteArray("peer bytes"));
        QVERIFY(!QFileInfo::exists(QDir(output).filePath(name)));
        QVERIFY(!QFileInfo::exists(QDir(output).filePath(other)));
        QCOMPARE(peer->selectionModel()->selectedRows().size(), 1);
        QVERIFY(source->sourcePanel->ShouldProcessNotifications());
        QCOMPARE(source->selectionModel()->selectedRows().size(), 2);
        QTRY_COMPARE(window.focusWidget(), source);
        retry = true;
        modalName.clear();
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        constexpr int added = 257;
        for (int i = 0; i < added; ++i)
            QVERIFY(writeFile(QDir(output).filePath(QString("new-%1.txt").arg(i)), "peer update"));
        QVERIFY(WindowActivation::waitForActive(window));
        QCOMPARE(window.focusWidget(), source);
        QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
        QTRY_VERIFY(peer->hasFocus());
        QTest::keySequence(peer, action(window, "refreshAction")->shortcut());
        QVERIFY(!action(window, "refreshAction")->isEnabled());
        QTest::keyClick(peer, Qt::Key_Tab);
        QVERIFY(source->hasFocus());
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        driver.start(1);
        PanelKeys::command(window.focusWidget(), Qt::Key_E,
                           Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(modalName, QString("copyDialog"));
        QTRY_COMPARE(peer->rowCount(), added + 3);
        driver.stop();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(readFile(QDir(output).filePath(name)), payload);
        QCOMPARE(readFile(QDir(output).filePath(other)), otherPayload);
        QCOMPARE(readFile(archive), before);
    }
    void filesystemShellCommandsUseIndependentOperations_data() {
        QTest::addColumn<QString>("verb");
        for (const QString &verb :
             {QString("Test"), QString("Extract"), QString("ExtractHere"), QString("ExtractTo")})
            QTest::newRow(qPrintable(verb)) << verb;
    }
    void filesystemShellCommandsUseIndependentOperations() {
        QFETCH(QString, verb);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const auto input = fixture.filePath("archives");
        QVERIFY(QDir().mkpath(input));
        const auto archive = makeArchive(input, "context command bytes");
        QVERIFY(!archive.isEmpty());
        QVERIFY(QFile::remove(QDir(input).filePath(kName)));
        const QString extractedFolder = QFileInfo(kName).path();
        QVERIFY(QDir(input).rmdir(extractedFolder));
        const auto before = readFile(archive);
        CContextMenuInfo context;
        context.Load(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        context.Flags = NContextMenuFlags::kTest | NContextMenuFlags::kExtract |
                        NContextMenuFlags::kExtractHere | NContextMenuFlags::kExtractTo;
        context.Flags_Def = true;
        context.Cascaded.SetTrueTrue();
        context.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        backend.ExtractDestination = fixture.filePath("edited output");
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *source = openPanel(window, 0, input);
        QTRY_COMPARE(source->rowCount(), 1);
        int selected = -1;
        for (int row = 0; row < source->rowCount(); ++row)
            if (source->model()->index(row, 0).data().toString() == "input.zip")
                selected = row;
        QVERIFY(selected >= 0);
        source->selectRow(selected);
        source->setFocus();
        auto *menu = window.findChild<QMenu *>("fileMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        auto *command = FindShellVerb(*menu, "SevenZip" + verb);
        QVERIFY(command && command->isEnabled());
        QStringList errors, successes;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *dialog = dynamic_cast<ExtractDialog *>(dialogs_.activeDialog())) {
                errors << "Unexpected extraction dialog in the manager process";
                dialog->reject();
            }
            dismissResults(dialogs_, errors, successes);
        });
        responder.start(5);
        command->trigger();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 20000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        if (verb == "Test") {
            QCOMPARE(backend.Selections.size(), 1);
            QCOMPARE(QFileInfo(backend.Selections.first().first()).canonicalFilePath(),
                     QFileInfo(archive).canonicalFilePath());
        } else {
            QCOMPARE(backend.ExtractJobs.size(), 1);
            QCOMPARE(backend.ExtractJobs.first().ShowDialog, verb == "Extract");
            const auto output = verb == "Extract"       ? backend.ExtractDestination
                                : verb == "ExtractHere" ? input
                                                        : QDir(input).filePath("input");
            QCOMPARE(readFile(QDir(output).filePath(kName)), QByteArray("context command bytes"));
            if (verb == "ExtractHere") {
                QTRY_VERIFY(!source->model()
                                 ->match(source->model()->index(0, 0), Qt::DisplayRole,
                                         extractedFolder, 1, Qt::MatchExactly)
                                 .isEmpty());
            }
        }
        QCOMPARE(readFile(archive), before);
        QVERIFY(action(window, "refreshAction")->isEnabled());
    }
    void extractionRefreshUsesCurrentLocation_data() {
        QTest::addColumn<bool>("navigate");
        QTest::newRow("navigation while child runs") << true;
        QTest::newRow("defer behind another source modal") << false;
    }
    void extractionRefreshUsesCurrentLocation() {
        QFETCH(bool, navigate);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const auto input = fixture.filePath("input");
        QVERIFY(QDir().mkpath(input));
        const auto archive = makeArchive(input, "refresh output");
        QVERIFY(!archive.isEmpty());
        const auto output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(output));
        const auto unrelated = fixture.filePath("unrelated");
        QVERIFY(writeFile(QDir(unrelated).filePath("selected.txt"), "retained selection"));
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        backend.ExtractDestination = output;
        backend.OutputBarrier = fixture.filePath("output gate");
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(showTwoPanels(window));
        auto *source = openPanel(window, 0, input);
        QTRY_COMPARE(source->rowCount(), 2); // The fixture input and its archive.
        auto *peer = openPanel(window, 1, output);
        QTRY_COMPARE(peer->rowCount(), 0);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        source->setFocus();
        int selected = -1;
        for (int row = 0; row < source->rowCount(); ++row)
            if (source->model()->index(row, 0).data().toString() == "input.zip")
                selected = row;
        QVERIFY(selected >= 0);
        source->selectRow(selected);
        action(window, "extractArchiveAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(backend.OutputBarrier + ".ready"), 15000);
        if (navigate) {
            peer = openPanel(window, 1, unrelated);
            QTRY_COMPARE(peer->rowCount(), 1);
            peer->selectRow(0);
            peer->setFocus();
            CMyComPtr<IFolderFolder> provider = peer->sourcePanel->GetFolder();
            QVERIFY(writeFile(backend.OutputBarrier, "continue"));
            QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 15000);
            QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
            QCOMPARE(peer->sourcePanel->GetFolder(), provider.Interface());
            QCOMPARE(peer->rowCount(), 1);
            QCOMPARE(peer->model()->index(0, 0).data().toString(), QString("selected.txt"));
            QCOMPARE(peer->selectionModel()->selectedRows().size(), 1);
            QTRY_VERIFY(peer->hasFocus());
        } else {
            bool released = false, deferred = false;
            QTimer driver;
            dialogs_.connectResponder(driver, &window, [&] {
                auto *dialog = dialogs_.activeDialog();
                if (!dialog || dialog->objectName() != "copyDialog")
                    return;
                if (!released) {
                    released = writeFile(backend.OutputBarrier, "continue");
                } else if (backend.Results.size() == 1) {
                    deferred = peer->rowCount() == 0;
                    dialog->reject();
                }
            });
            driver.start(5);
            QTimer::singleShot(25000, &window, [&window] {
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                    if (dialog->objectName() == "copyDialog")
                        dialog->reject();
            });
            QVERIFY(action(window, "copyAction")->isEnabled());
            action(window, "copyAction")->trigger();
            driver.stop();
            QVERIFY(released);
            QVERIFY(deferred);
            QCOMPARE(backend.Results.size(), 1);
            QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
            QTRY_COMPARE(peer->rowCount(), 1);
        }
        QCOMPARE(readFile(QDir(output).filePath(kName)), QByteArray("refresh output"));
    }
    void filesystemExtractRequiresOneArchive_data() {
        QTest::addColumn<bool>("multiple");
        QTest::addColumn<unsigned>("pane");
        for (bool multiple : {false, true})
            for (unsigned pane : {0u, 1u})
                QTest::newRow(qPrintable(QString("%1-%2").arg(multiple).arg(pane)))
                    << multiple << pane;
    }
    void filesystemExtractRequiresOneArchive() {
        QFETCH(bool, multiple);
        QFETCH(unsigned, pane);
        QTemporaryDir fixture(FixtureTemplate());
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("archives");
        QVERIFY(QDir().mkpath(input));
        const QString name = QStringLiteral("данные 空.txt");
        const QByteArray payload("batch toolbar bytes");
        QVERIFY(writeFile(fixture.filePath(name), payload));
        const QString first = QDir(input).filePath(QStringLiteral("первый δ.zip"));
        const QString second = QDir(input).filePath("second.7z");
        QVERIFY(makeArchive(fixture.path(), first, "zip", name));
        QVERIFY(makeArchive(fixture.path(), second, "7z", name));
        const QByteArray firstBefore = readFile(first), secondBefore = readFile(second);
        const QString output = fixture.filePath("output δ");
        QVERIFY(QDir().mkpath(output));
        GuiTestBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(showTwoPanels(window));
        auto *peer = openPanel(window, 1 - pane, output);
        QTRY_COMPARE(peer->rowCount(), 0);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *source = openPanel(window, pane, input);
        QTRY_COMPARE(source->rowCount(), 2);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        source->sourcePanel->Set_AutoRefresh_Mode(false);
        int firstRow = -1;
        for (int i = 0; i < source->rowCount(); ++i)
            if (source->model()->index(i, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == QFileInfo(first).fileName())
                firstRow = i;
        QVERIFY(firstRow >= 0);
        source->selectRow(firstRow);
        source->setCurrentCell(firstRow, 0);
        source->setFocus();
        if (multiple)
            source->sourcePanel->SelectAll(true);
        QCOMPARE(action(window, "extractArchiveAction")->isEnabled(), !multiple);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        CMyComPtr<IFolderFolder> provider = source->sourcePanel->GetFolder();
        QStringList errors;
        QStringList successes;
        QTimer driver;
        dialogs_.connectResponder(driver, &window,
                                  [&] { dismissResults(dialogs_, errors, successes); });
        driver.start(1);
        if (multiple) {
            PanelKeys::command(source, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
            action(window, "extractArchiveAction")->trigger();
            QVERIFY(backend.ExtractJobs.isEmpty());
            QVERIFY(QDir(output).isEmpty());
            PanelKeys::command(source, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
            QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
            QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
            QCOMPARE(backend.Selections.last().size(), 2);
            QCOMPARE(readFile(first), firstBefore);
            QCOMPARE(readFile(second), secondBefore);
            source->selectRow(firstRow);
            QTRY_VERIFY(action(window, "extractArchiveAction")->isEnabled());
            backend.Results.clear();
            backend.Selections.clear();
        } else {
            const QString parked = fixture.filePath("parked.zip");
            QVERIFY(QFile::rename(first, parked));
            PanelKeys::command(source, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
            QVERIFY(backend.ExtractJobs.isEmpty());
            QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(first).fileName()));
            QTRY_COMPARE(window.focusWidget(), source);
            QVERIFY(source->selectionModel()->isRowSelected(firstRow, {}));
            QVERIFY(QDir().mkdir(first));
            PanelKeys::command(source, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
            QVERIFY(backend.ExtractJobs.isEmpty());
            QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(first).fileName()));
            QVERIFY(QDir().rmdir(first));
            QVERIFY(QFile::rename(parked, first));
        }
        backend.ExtractDestination = output;
        backend.ExtractDialogReport = fixture.filePath("extract-dialog.json");
        backend.ExtractDialogTag = QString("toolbar-batch-%1-%2").arg(multiple).arg(pane);
        backend.CancelExtract = true;
        source->setFocus();
        PanelKeys::command(source, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
        if (!multiple)
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Cancelled);
        QVERIFY(QDir(output).isEmpty());
        backend.CancelExtract = false;
        QTRY_COMPARE(window.focusWidget(), source);
        PanelKeys::command(window.focusWidget(), Qt::Key_E,
                           Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 2, 30000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        const auto dialogReport =
            QJsonDocument::fromJson(readFile(backend.ExtractDialogReport)).object();
        QCOMPARE(dialogReport.value("title").toString().normalized(QString::NormalizationForm_C),
                 (QtLangText(IDD_EXTRACT, "Extract") + " : " + QDir::toNativeSeparators(first))
                     .normalized(QString::NormalizationForm_C));
        QTRY_COMPARE(peer->rowCount(), 1);
        QCOMPARE(backend.ExtractJobs.last().Paths.size(), 1);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(readFile(QDir(output).filePath(name)), payload);
        successes.clear();
        source->setFocus();
        PanelKeys::command(source, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 3, 30000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        QCOMPARE(backend.Selections.last().size(), 1);
        driver.stop();
        QCOMPARE(source->sourcePanel->GetFolder(), provider.Interface());
        QCOMPARE(readFile(first), firstBefore);
        QCOMPARE(readFile(second), secondBefore);
        QCOMPARE(readFile(fixture.filePath(name)), payload);
        QVERIFY(source->sourcePanel->ShouldProcessNotifications());
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_VERIFY(source->hasFocus());
    }
};
QTEST_MAIN(ToolbarArchiveWorkflowTests)
#include "toolbar_archive_workflow_tests.moc"
