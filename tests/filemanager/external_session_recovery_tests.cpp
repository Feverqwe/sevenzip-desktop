#include "dialogs/password/PasswordDialog.h"
#include "filemanager/external/qtexternalfilerecord.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempidentity.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include <QCheckBox>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTreeWidget>

using namespace ExternalEditorFixture;

namespace {
const QString member = QStringLiteral("изменить 空.txt");
const QString password = QStringLiteral("recovery-fixture-secret");
QDialog *details() {
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "externalFileDetails" && widget->isVisible())
            return qobject_cast<QDialog *>(widget);
    return nullptr;
}
std::optional<QtOwnedTempEntry> ownerOf(const QString &copy) {
    const QString identity = QtOwnedTempIdentity(QFileInfo(copy).absolutePath());
    for (const auto &entry : QtOwnedTempRegistry().Entries())
        if (entry.identity == identity)
            return entry;
    return {};
}
} // namespace

// Separate processes ensure that no panel, backend, password or live session
// from the original opening can participate in recovery.
class RecoveryProbe final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString directory_, choice_, secret_;
    WidgetDialogFixture dialogs_;

  public:
    RecoveryProbe(QString directory, QString choice, bool encrypted)
        : directory_(std::move(directory)), choice_(std::move(choice)),
          secret_(encrypted ? password : QString()) {}

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
    }
    void resume() {
        const QString archive = QDir(directory_).filePath("archive.7z");
        const QByteArray originalArchive = read(archive);
        const QString copy = QString::fromUtf8(read(QDir(directory_).filePath("copy-path")));
        QtOwnedTempRegistry registry;
        const auto restored = ownerOf(copy);
        QVERIFY(restored);
        const auto owner = *restored;
        QVERIFY(!owner.active && owner.externallyProtected);
        auto backend = std::make_unique<ArchiveBackend>();
        int launches = 0;
        auto window = std::make_unique<MainWindow>(backend.get(), [&](const QUrl &) {
            ++launches;
            return true;
        });
        window->show();
        bool browserSeen = false, handedOff = false, asked = false;
        int passwords = 0;
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (!dialog)
                return;
            if (dialog->objectName() == "tempBrowseDialog") {
                browserSeen = true;
                auto *list = dialog->findChild<QTreeWidget *>("tempList");
                auto *button = dialog->findChild<QPushButton *>("tempSessionDetails");
                if (!list || !button)
                    return;
                const QString name = QFileInfo(owner.path).fileName();
                for (int row = 0; row < list->topLevelItemCount(); ++row) {
                    auto *item = list->topLevelItem(row);
                    if (item->text(0) != name)
                        continue;
                    list->setCurrentItem(item);
                    item->setSelected(true);
                    if (button->isEnabled()) {
                        handedOff = true;
                        button->click();
                    }
                    return;
                }
            } else if (dialog->objectName() == "externalUpdatePrompt") {
                asked = true;
                if (choice_ == "later")
                    QTest::keyClick(dialog, Qt::Key_Escape);
                else
                    dialog->findChild<QPushButton *>("externalUpdateCommit")->click();
            } else if (auto *question = dynamic_cast<CPasswordDialog *>(dialog)) {
                ++passwords;
                question->findChild<QLineEdit *>("passwordEdit")->setText(secret_);
                question->accept();
            }
        });
        responder.start(1);
        // Never hang the process if the actual front door stops finding the
        // persisted owner. The parent also imposes a process deadline.
        QTimer::singleShot(10000, this, [&] {
            if (auto *dialog = dialogs_.activeDialog())
                dialog->reject();
        });
        auto *temporary = action(*window, "tempDirAction");
        QVERIFY(temporary && temporary->isEnabled());
        temporary->trigger();
        QVERIFY(browserSeen && handedOff);
        QTRY_VERIFY(details());
        QCOMPARE(launches, 0);
        QCOMPARE(read(archive), originalArchive);
        auto *dialog = details();
        auto *check = dialog->findChild<QPushButton *>("externalFileCheck");
        auto *confirmed = dialog->findChild<QCheckBox *>("externalFileConfirmedClosed");
        QVERIFY(check && confirmed && !check->isEnabled());
        if (choice_ == "published") {
            QVERIFY(!confirmed->isVisible());
            QCOMPARE(contents(archive, member, secret_), modified);
            QCOMPARE(passwords, 0);
            dialog->close();
            return;
        }
        QVERIFY(window->close());
        window.reset();
        backend.reset();
        confirmed->setChecked(true);
        QVERIFY(check->isEnabled());
        check->click();
        QVERIFY(asked);
        if (choice_ == "update") {
            QCOMPARE(contents(archive, member, secret_), modified);
            if (!secret_.isEmpty())
                QVERIFY(passwords > 0);
        } else
            QCOMPARE(read(archive), originalArchive);
        QCOMPARE(read(copy), modified);
        QVERIFY(!registry.ClaimForDeletion(owner));
        dialog->close();
    }
};

class ExternalSessionRecoveryTests final : public QObject {
    Q_OBJECT
  private slots:
    void distinctUnicodeCopiesOpenTheirOwnDetails_data() {
        QTest::addColumn<bool>("genericFirst");
        QTest::newRow("live-distinct-files") << false;
        QTest::newRow("directory-prefers-durable-target") << true;
    }
    void distinctUnicodeCopiesOpenTheirOwnDetails() {
        QFETCH(bool, genericFirst);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto previous = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", fixture.filePath("registry").toUtf8());
        const auto restore =
            qScopeGuard([&] { qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", previous); });
        const QString directory = fixture.filePath("copies");
        QVERIFY(QDir().mkpath(directory));
        const QString composed = QString::fromUtf8("é.txt");
        const QString decomposed = composed.normalized(QString::NormalizationForm_D);
        const QString first = QDir(directory).filePath(composed);
        const QString second = QDir(directory).filePath(decomposed);
        QVERIFY(write(first, "first"));
        if (QFileInfo::exists(second))
            QSKIP("Filesystem aliases canonically equivalent filenames");
        QVERIFY(write(second, "second"));
        QVERIFY(QtOwnedTempIdentity(first) != QtOwnedTempIdentity(second));
        NWindows::NFile::NFind::CFileInfo info;
        QVERIFY(info.Find(us2fs(first.toStdWString().c_str())));
        QtOwnedTempRegistry registry;
        auto owner = registry.RegisterCreated(directory);
        QVERIFY(owner && owner->ProtectExternal());
        const QtExternalFileRecord saved{
            composed, "first entry", "first source", {}, {info.Size, info.MTime}, {}, false};
        QVERIFY(owner->SetContext(saved.Save()));
        owner.reset();
        const auto entry = registry.Entries().first();
        ArchiveBackend backend;
        QVERIFY(
            QtExternalFileTask::Details(entry, genericFirst ? second : first, backend, {}, true));
        QTRY_VERIFY(details());
        QPointer<QDialog> firstDetails = details();
        firstDetails->close();
        QTRY_VERIFY(!firstDetails);
        QVERIFY(QtExternalFileTask::Details(entry, genericFirst ? directory : second, backend, {},
                                            true));
        QTRY_VERIFY(details());
        QString visibleText;
        for (auto *label : details()->findChildren<QLabel *>())
            visibleText += label->text() + '\n';
        QVERIFY(visibleText.contains(genericFirst ? first : second));
        QCOMPARE(visibleText.contains("first source"), genericFirst);
        details()->close();
    }
    void restart_data() {
        QTest::addColumn<bool>("encrypted");
        QTest::addColumn<QString>("choice");
        QTest::newRow("plain-update") << false << QString("update");
        QTest::newRow("encrypted-fresh-password") << true << QString("update");
        QTest::newRow("changed-original-refused") << false << QString("changed");
        QTest::newRow("later-keeps-copy") << false << QString("later");
    }
    void restart() {
        QFETCH(bool, encrypted);
        QFETCH(QString, choice);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("archive.7z");
        QVERIFY(write(fixture.filePath(member), "original"));
        QVERIFY(
            createArchive(fixture.path(), archive, member, "7z", encrypted ? password : QString()));
        const QString registryPath = fixture.filePath("owners");
        auto run = [&](const QString &mode) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("SEVENZIP_OWNED_TEMP_REGISTRY", registryPath);
            environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath(mode + ".ini"));
            process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationFilePath(),
                          {"--recovery-probe", mode, fixture.path(), choice,
                           encrypted ? "encrypted" : "plain"});
            if (!process.waitForFinished(25000)) {
                process.kill();
                process.waitForFinished();
                qWarning() << "Recovery probe timed out" << mode;
                return false;
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                qWarning().noquote()
                    << process.readAllStandardOutput() << process.readAllStandardError();
                return false;
            }
            return true;
        };
        const auto cleanup = qScopeGuard([&] {
            // Both child processes have terminated; remove only their owned
            // fixture copy while its actual persisted identity is pinned.
            QtOwnedTempRegistry registry(registryPath);
            for (const auto &entry : registry.Entries())
                if (auto pin = registry.PinForRead(entry))
                    QDir(entry.path).removeRecursively();
        });
        QVERIFY(run("seed"));
        QtOwnedTempRegistry registry(registryPath);
        for (const auto &entry : registry.Entries()) {
            const QByteArray saved = QJsonDocument(entry.context).toJson();
            QVERIFY(!saved.contains(password.toUtf8()));
            QVERIFY(!saved.contains(password.toUtf8().toBase64()));
        }
        if (choice == "changed") {
            QVERIFY(QFile::remove(archive));
            QVERIFY(write(fixture.filePath("other.txt"), "new archive generation"));
            QVERIFY(createArchive(fixture.path(), archive, "other.txt", "7z"));
        }
        QVERIFY(run("resume"));
        if (choice == "update")
            QVERIFY(run("review"));
    }
};

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (argc == 6 && QByteArray(argv[1]) == "--recovery-probe") {
        RecoveryProbe probe(QString::fromLocal8Bit(argv[3]),
                            QByteArray(argv[2]) == "review" ? QString("published")
                                                            : QString::fromLocal8Bit(argv[4]),
                            QByteArray(argv[5]) == "encrypted");
        char resume[] = "resume";
        char *arguments[] = {argv[0], QByteArray(argv[2]) == "review" ? resume : argv[2]};
        return QTest::qExec(&probe, 2, arguments);
    }
    ExternalSessionRecoveryTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_session_recovery_tests.moc"
