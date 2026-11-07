#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempidentity.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>

using namespace ExternalEditorFixture;

namespace {
const QString member = QStringLiteral("изменить 空.txt");
const QString password = QStringLiteral("recovery-fixture-secret");
std::optional<QtOwnedTempEntry> ownerOf(const QString &copy) {
    const QString identity = QtOwnedTempIdentity(QFileInfo(copy).absolutePath());
    for (const auto &entry : QtOwnedTempRegistry().Entries())
        if (entry.identity == identity)
            return entry;
    return {};
}
} // namespace

// Separate processes prove that retained copies remain readable without
// restoring an archive-update route after their original tab has closed.
class RecoveryProbe final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString directory_, secret_;
    WidgetDialogFixture dialogs_;

  public:
    RecoveryProbe(QString directory, bool encrypted)
        : directory_(std::move(directory)), secret_(encrypted ? password : QString()) {}

  private slots:
    void seed() {
        const QString archive = QDir(directory_).filePath("archive.7z");
        auto backend = std::make_unique<ArchiveBackend>();
        QString copy;
        MainWindow window(backend.get(), [&](const QUrl &url) {
            copy = url.toLocalFile();
            return true;
        });
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(dialogs_.activeDialog())) {
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(secret_);
                dialog->accept();
            }
        });
        responder.start(1);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        table(window)->setCurrentCell(0, 0);
        auto *open = action(window, "openOutsideAction");
        QVERIFY(open && open->isEnabled());
        open->trigger();
        QTRY_VERIFY(!copy.isEmpty());
        QTRY_VERIFY(QtExternalFileTask::Active().isEmpty());
        QtOwnedTempRegistry registry;
        QTRY_VERIFY(ownerOf(copy).has_value() && !ownerOf(copy)->active);
        const auto owner = *ownerOf(copy);
        QVERIFY(owner.externallyProtected);
        QVERIFY(!owner.context.isEmpty());
        QCOMPARE(read(copy), QByteArray("original"));
        QVERIFY(write(copy, modified));
        QVERIFY(write(QDir(directory_).filePath("copy-path"), copy.toUtf8()));
        QVERIFY(window.close());
        QVERIFY(!registry.ClaimForDeletion(owner));
    }
    void inspect() {
        const QString archive = QDir(directory_).filePath("archive.7z");
        const QByteArray originalArchive = read(archive);
        const QString copy = QString::fromUtf8(read(QDir(directory_).filePath("copy-path")));
        QtOwnedTempRegistry registry;
        const auto owner = ownerOf(copy);
        QVERIFY(owner && !owner->active && owner->externallyProtected);

        registry.CleanUnusedSystemTemporaryObjects();

        QVERIFY(!registry.ClaimForDeletion(*owner));
        QVERIFY(QtExternalFileTask::Active().isEmpty());
        QCOMPARE(read(archive), originalArchive);
        QCOMPARE(read(copy), modified);
    }
};

class ExternalSessionRecoveryTests final : public QObject {
    Q_OBJECT
  private slots:
    void restart_data() {
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("plain-retained-copy") << false;
        QTest::newRow("encrypted-retained-copy") << true;
    }
    void restart() {
        QFETCH(bool, encrypted);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("archive.7z");
        QVERIFY(write(fixture.filePath(member), "original"));
        QVERIFY(
            createArchive(fixture.path(), archive, member, "7z", encrypted ? password : QString()));
        const QString registryPath = fixture.filePath("owners");
        const auto cleanup = qScopeGuard([&] {
            QtOwnedTempRegistry registry(registryPath);
            for (const auto &entry : registry.Entries())
                if (auto pin = registry.PinForRead(entry))
                    QDir(entry.path).removeRecursively();
        });
        for (const QString &mode : {QString("seed"), QString("inspect")}) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("SEVENZIP_OWNED_TEMP_REGISTRY", registryPath);
            environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath(mode + ".ini"));
            process.setProcessEnvironment(environment);
            process.start(
                QCoreApplication::applicationFilePath(),
                {"--retained-copy-probe", mode, fixture.path(), encrypted ? "encrypted" : "plain"});
            if (!process.waitForFinished(25000)) {
                process.kill();
                process.waitForFinished();
                QFAIL(qPrintable("Retained-copy probe timed out: " + mode));
            }
            QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                     qPrintable(QString::fromUtf8(process.readAllStandardOutput() +
                                                  process.readAllStandardError())));
            QtOwnedTempRegistry registry(registryPath);
            for (const auto &entry : registry.Entries()) {
                const QByteArray saved = QJsonDocument(entry.context).toJson();
                QVERIFY(!saved.contains(password.toUtf8()));
                QVERIFY(!saved.contains(password.toUtf8().toBase64()));
            }
        }
    }
};

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (argc == 5 && QByteArray(argv[1]) == "--retained-copy-probe") {
        RecoveryProbe probe(QString::fromLocal8Bit(argv[3]), QByteArray(argv[4]) == "encrypted");
        char *arguments[] = {argv[0], argv[2]};
        return QTest::qExec(&probe, 2, arguments);
    }
    ExternalSessionRecoveryTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_session_recovery_tests.moc"
