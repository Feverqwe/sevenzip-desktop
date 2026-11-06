#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include <QComboBox>
#include <QPointer>
#include <QScopeGuard>

using namespace ExternalEditorFixture;

class ExternalCopyLifetimeTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void editorExitWhileExportChoiceIsOpen_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("unchanged-copy-remains-readable") << 0;
        QTest::newRow("missing-copy-does-not-destroy-stack-dialog") << 1;
        QTest::newRow("replacement-directory-is-not-exported") << 2;
    }
    void editorExitWhileExportChoiceIsOpen() {
        QFETCH(int, change);
        QTemporaryDir fixture;
        const auto archive = fixture.filePath("source.zip");
        const auto control = fixture.filePath("editor");
        QVERIFY(write(fixture.filePath("file.txt"), "original"));
        QVERIFY(createArchive(fixture.path(), archive, "file.txt"));
        const auto original = read(archive);
        configure(control);
        const auto cleanup = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const auto copy = QString::fromUtf8(read(control + ".ready"));
        const auto directory = QFileInfo(copy).absolutePath();
        const auto moved = fixture.filePath("moved-owned-copy");
        const auto exported = fixture.filePath("exported.txt");
        waitForKnownEditor();
        QtExternalFileTask::Active().front()->Show();
        QPointer<QDialog> details;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "externalFileDetails" && widget->isVisible())
                details = qobject_cast<QDialog *>(widget);
        QVERIFY(details);
        bool survived = false;
        QTimer completed;
        dialogs_.respond(window, [&](QDialog &choice) {
            QVERIFY(choice.objectName() == "copyDialog");
            if (change == 1)
                QVERIFY(QFile::remove(copy));
            if (change == 2) {
                QVERIFY(QDir().rename(directory, moved));
                QVERIFY(QDir().mkpath(directory));
                QVERIFY(write(copy, "foreign replacement"));
                choice.findChild<QComboBox *>("copyDestination")->setEditText(exported);
                survived = details && choice.isVisible();
                choice.accept();
                return;
            }
            QVERIFY(write(control + ".exit", "exit"));
            connect(&completed, &QTimer::timeout, &choice,
                    [&, choice = QPointer<QDialog>(&choice)] {
                        if (!QtExternalFileTask::Active().isEmpty() ||
                            !QFileInfo::exists(control + ".finished"))
                            return;
                        completed.stop();
                        QTimer::singleShot(20, qApp, [&, choice] {
                            survived = choice && details && choice->isVisible() &&
                                       QFileInfo::exists(copy) == (change != 1);
                            if (choice) {
                                choice->reject();
                            }
                        });
                    });
            completed.start(5);
        });
        details->findChild<QPushButton *>("externalFileSaveCopy")->click();
        QVERIFY(survived);
        QCOMPARE(read(archive), original);
        if (change == 0)
            QCOMPARE(read(copy), QByteArray("original"));
        if (change == 2) {
            QVERIFY(!QFileInfo::exists(exported));
            QCOMPARE(read(copy), QByteArray("foreign replacement"));
            QCOMPARE(read(QDir(moved).filePath(QFileInfo(copy).fileName())),
                     QByteArray("original"));
        }
        if (details)
            details->close();
        QVERIFY(window.close());
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo::exists(control + ".finished"));
        if (change == 2)
            QVERIFY(QDir(directory).removeRecursively());
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            if (entry.path == QFileInfo(copy).absolutePath()) {
                auto pin = QtOwnedTempRegistry().PinForRead(entry);
                QVERIFY(pin);
                QVERIFY(QDir(entry.path).removeRecursively());
            }
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    ExternalCopyLifetimeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_copy_lifetime_tests.moc"
