#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "platform/support/external_editor_fixture.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopeGuard>

using namespace ExternalEditorFixture;

class ArchiveExternalSecurityTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void protectedArchiveUsesSourcePasswordAndWriter_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("cancelPassword");
        QTest::newRow("zip-aes-update") << QString("zip") << false;
        QTest::newRow("7z-headers-update") << QString("7z") << false;
        QTest::newRow("zip-extract-password-cancel") << QString("zip") << true;
    }
    void protectedArchiveUsesSourcePasswordAndWriter() {
        QFETCH(QString, format);
        QFETCH(bool, cancelPassword);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("защищённый файл.txt");
        const QString archive = fixture.filePath("original." + format);
        const QString password = format == "zip" ? QStringLiteral("fixture password space")
                                                 : QStringLiteral("fixture 密碼 space");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name, format, password));
        const QByteArray original = read(archive);
        const QString control = fixture.filePath("control");
        const auto stop = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend, {}, nullptr, nullptr, DeviceSelectionServices::system(),
                          opener(control));
        bool extracting = false, asked = false;
        int passwordPrompts = 0;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            auto *modal = widgetDialogs_.activeDialog();
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(modal)) {
                ++passwordPrompts;
                if (cancelPassword && extracting)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    dialog->accept();
                }
            } else if (modal && modal->objectName() == "externalUpdatePrompt") {
                asked = true;
                modal->findChild<QPushButton *>("externalUpdateCommit")->click();
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close))
                    close->click();
            }
        });
        responder.start(1);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        extracting = true;
        action(window, "openOutsideAction")->trigger();
        if (cancelPassword) {
            QVERIFY(passwordPrompts > 0);
            QVERIFY(!QFileInfo(control + ".ready").exists());
            QCOMPARE(read(archive), original);
        } else {
            QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
            const QString temporary = QString::fromUtf8(read(control + ".ready"));
            QEventLoop completion;
            QTimer completed;
            connect(&completed, &QTimer::timeout, &completion, [&] {
                if (asked && !QFileInfo(temporary).exists())
                    completion.quit();
            });
            completed.start(5);
            waitForKnownEditor();
            QTimer::singleShot(5000, &completion, &QEventLoop::quit);
            QVERIFY(write(control, "change"));
            QVERIFY(write(control + ".exit", "exit"));
            // Cocoa must deliver nested modal requests from a running loop,
            // rather than QTest's manual processEvents wait.
            completion.exec();
            QVERIFY(asked);
            QVERIFY(!QFileInfo(temporary).exists());
            QVERIFY(passwordPrompts > 0);
            QCOMPARE(contents(archive, name, password), modified);
            QProcess wrong;
            wrong.start(executable(), {"t", "-pwrong-fixture-password", "--", archive});
            QVERIFY(wrong.waitForFinished(10000));
            QVERIFY(wrong.exitCode() != 0);
        }
    }
};
int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    ArchiveExternalSecurityTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "archive_external_security_tests.moc"
