#include "Common/MyTypes.h"
#include "filemanager/views/filetable.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

namespace {
QAction *FindShellVerb(QMenu &menu, const QString &verb) {
    for (auto *action : menu.actions()) {
        if (action->property("shellVerb").toString() == verb)
            return action;
        if (action->menu())
            if (auto *found = FindShellVerb(*action->menu(), verb))
                return found;
    }
    return nullptr;
}
class HashBackend final : public ArchiveBackend {
  public:
    QList<QtOperationJob> Requests;
    QList<QtOperationResult> Results;
    QString Report, Barrier, ReportTag;
    bool CrashNext = false;
    unsigned Accepted = 0;
    QtOperationLauncher *startGuiHash(const QStringList &paths, const QString &method,
                                      const QString &prefix, bool enter, QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Hash;
        job.Paths = paths;
        job.HashMethod = method;
        job.HashBasePrefix = prefix;
        job.HashEnterToDirs = enter;
        return Launch(job, error);
    }
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
        return Launch(job, error);
    }
    QtOperationLauncher *startGuiTest(const QStringList &paths, QString &error,
                                      bool hashMode = false) override {
        QtOperationJob job;
        job.Paths = paths;
        job.HashMode = hashMode;
        job.YesToAll = true;
        return Launch(job, error);
    }

  private:
    QtOperationLauncher *Launch(const QtOperationJob &job, QString &error) {
        Requests.append(job);
        ScopedTestEnvironment report("SEVENZIP_OPERATION_HASH_REPORT", Report);
        ScopedTestEnvironment reportTag("SEVENZIP_OPERATION_HASH_UI_TAG", ReportTag);
        ScopedTestEnvironment barrier("SEVENZIP_OPERATION_BARRIER", Barrier);
        auto *operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        if (operation) {
            const bool crash = std::exchange(CrashNext, false);
            connect(operation, &QtOperationLauncher::accepted, this, [this, operation, crash] {
                ++Accepted;
                if (crash)
                    KillOperationProbe(operation->processId());
            });
            connect(operation, &QtOperationLauncher::finished, this,
                    [this](const QtOperationResult &result) { Results.append(result); });
        }
        return operation;
    }
};
} // namespace

class ManagerHashWorkflowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static bool containsDigest(const QString &report, const QByteArray &payload) {
        return SourceExtractFixture::readFile(report).contains(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
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
    void selectedPanelHashesWithoutBlockingManager_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void selectedPanelHashesWithoutBlockingManager() {
        QFETCH(unsigned, pane);
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString source = input.filePath("selected [literal] 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(source, "selected manager bytes"));
        HashBackend backend;
        backend.Report = output.filePath("hashes.json");
        backend.Barrier = output.filePath("continue");
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *selected = openPanel(window, pane, input.path());
        QTRY_COMPARE(selected->rowCount(), 1);
        selected->selectRow(0);
        selected->setFocus();
        QTRY_VERIFY(selected->hasFocus());
        auto *hash = action(window, "hashSHA256Action");
        QTRY_VERIFY(hash->isEnabled());
        hash->trigger();
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(selected->isEnabled());
        QTRY_COMPARE(backend.Accepted, 1u);
        QVERIFY(backend.Results.isEmpty());
        // The accepted job owns its selection even if the manager navigates.
        auto *table = openPanel(window, pane, output.path());
        QTRY_COMPARE(table->rowCount(), 0);
        QVERIFY(SourceExtractFixture::writeFile(backend.Barrier, "continue"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 20000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        QVERIFY(containsDigest(backend.Report, "selected manager bytes"));
        QCOMPARE(SourceExtractFixture::readFile(source), QByteArray("selected manager bytes"));
        QCOMPARE(QDir::cleanPath(window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                                     ->findChild<QLineEdit *>("archivePath")
                                     ->text()),
                 QDir::cleanPath(output.path()));
    }
    void managerCloseLeavesAcceptedHashRunning() {
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        QVERIFY(SourceExtractFixture::writeFile(input.filePath("selected.txt"), "after close"));
        HashBackend backend;
        backend.Report = output.filePath("hashes.json");
        backend.Barrier = output.filePath("continue");
        MainWindow window(&backend);
        window.show();
        auto *table = openPanel(window, 0, input.path());
        QTRY_COMPARE(table->rowCount(), 1);
        table->selectRow(0);
        table->setFocus();
        auto *hash = action(window, "hashSHA256Action");
        QTRY_VERIFY(hash->isEnabled());
        hash->trigger();
        QTRY_COMPARE(backend.Accepted, 1u);
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
        QVERIFY(backend.Results.isEmpty());
        QVERIFY(SourceExtractFixture::writeFile(backend.Barrier, "continue"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 20000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        QVERIFY(containsDigest(backend.Report, "after close"));
    }
    void shellChecksumOffersIndividualAlgorithms() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Native codec module unavailable");
#else
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString source = input.filePath("selected 空.txt");
        const QByteArray payload = "shell checksum bytes";
        QVERIFY(SourceExtractFixture::writeFile(source, payload));
        CContextMenuInfo context;
        context.Load(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        context.Flags = NContextMenuFlags::kCRC;
        context.Flags_Def = true;
        context.Cascaded.SetTrueTrue();
        context.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        HashBackend backend;
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        backend.Report = output.filePath("hashes.json");
        MainWindow window(&backend);
        window.show();
        auto *table = openPanel(window, 0, input.path());
        QTRY_COMPARE(table->rowCount(), 1);
        table->selectRow(0);
        table->setFocus();
        auto *menu = window.findChild<QMenu *>("fileMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        QVERIFY(!FindShellVerb(*menu, "SevenZip.Checksum.Calc.*"));
        auto *calculate = FindShellVerb(*menu, "SevenZip.Checksum.Calc.SHA256");
        QVERIFY(calculate && calculate->isEnabled());
        calculate->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 20000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        QVERIFY(containsDigest(backend.Report, payload));
        QCOMPARE(SourceExtractFixture::readFile(source), payload);
#endif
    }
    void crashedHashDoesNotDisableNextCommand() {
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        QVERIFY(SourceExtractFixture::writeFile(input.filePath("selected.txt"), "retry bytes"));
        HashBackend backend;
        backend.Report = output.filePath("hashes.json");
        backend.CrashNext = true;
        backend.Barrier = output.filePath("crash gate");
        MainWindow window(&backend);
        window.show();
        auto *table = openPanel(window, 0, input.path());
        QTRY_COMPARE(table->rowCount(), 1);
        table->selectRow(0);
        table->setFocus();
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->accept();
        });
        dismiss.start(1);
        auto *hash = action(window, "hashSHA256Action");
        QTRY_VERIFY(hash->isEnabled());
        hash->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 15000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Crashed);
        QTRY_VERIFY(hash->isEnabled() && table->isEnabled());
        backend.Barrier.clear();
        hash->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 2, 20000);
        QCOMPARE(backend.Results.last().Status, QtOperationResult::Outcome::Success);
        QVERIFY(containsDigest(backend.Report, "retry bytes"));
    }
#ifdef Q_OS_WIN
    void alternateStreamHashKeepsSourcePrefixAndResults_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void alternateStreamHashKeepsSourcePrefixAndResults() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString name = QStringLiteral("файл 空 with space.bin");
        const QString streamName = QStringLiteral("поток 空 with space");
        const QString base = input.filePath(name), stream = base + ':' + streamName;
        QVERIFY(SourceExtractFixture::writeFile(base, "base"));
        QVERIFY(SourceExtractFixture::writeFile(stream, "123456789"));
        HashBackend backend;
        backend.Report = output.filePath("hashes.json");
        backend.ReportTag = "namespace-stream-hash-" + language;
        MainWindow window(&backend);
        window.show();
        auto *table = openPanel(window, 0, input.path());
        QTRY_COMPARE(table->rowCount(), 1);
        window.openPath(base + ':');
        QTRY_COMPARE(table->rowCount(), 1);
        QTRY_COMPARE(table->model()->index(0, 0).data().toString(), streamName);
        table->setCurrentCell(0, 0);
        table->selectRow(0);
        auto *hash = action(window, "hashSHA256Action");
        QVERIFY(hash->isEnabled());
        hash->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 20000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        QVERIFY(containsDigest(backend.Report, "123456789"));
        QVERIFY(QJsonDocument::fromJson(SourceExtractFixture::readFile(backend.Report))
                    .object()
                    .value("geometry")
                    .toBool());
        QCOMPARE(SourceExtractFixture::readFile(base), QByteArray("base"));
        QCOMPARE(SourceExtractFixture::readFile(stream), QByteArray("123456789"));
    }
#endif
};
QTEST_MAIN(ManagerHashWorkflowTests)
#include "manager_hash_workflow_tests.moc"
