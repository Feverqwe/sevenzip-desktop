#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/operation_kill.h"
#include "support/scoped_environment.h"
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>

class DraftQuitTests final : public QObject {
    Q_OBJECT
  private slots:
    void quitReturnsToUnstartedForm_data() {
        QTest::addColumn<QString>("locale");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void quitReturnsToUnstartedForm() {
        QFETCH(QString, locale);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(locale), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "untouched bytes");
        QVERIFY(!archive.isEmpty());
        const auto original = SourceExtractFixture::readFile(archive);
        const auto report = fixture.filePath("draft.json");
        const auto release = fixture.filePath("release");
        ScopedTestEnvironment show("SEVENZIP_OPERATION_WINDOW_REPORT", report);
        ScopedTestEnvironment close("SEVENZIP_OPERATION_RELEASE_WINDOW", release);
        ScopedTestEnvironment text("SEVENZIP_OPERATION_DRAFT_TEXT", "retained draft value");
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = {archive};
        job.Destination = fixture.filePath("not applied");
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto stop = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_VERIFY_WITH_TIMEOUT(
            operation && operation->presentation().Phase == QtOperationPhase::Draft, 15000);
        const auto state = [&] {
            return QJsonDocument::fromJson(SourceExtractFixture::readFile(report)).object();
        };
        QTRY_COMPARE(state().value("draftValue").toString(), QString("retained draft value"));
        QTRY_VERIFY(state().value("minimized").toBool());
        application.Quit();
        QVERIFY(first.isVisible() && second.isVisible());
        QTRY_VERIFY(state().value("minimized").isBool() && !state().value("minimized").toBool());
        application.Quit();
        QVERIFY(first.isVisible() && second.isVisible());
        QVERIFY(operation && operation->active());
        QVERIFY(finished.isEmpty());
        QCOMPARE(state().value("draftValue").toString(), QString("retained draft value"));
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        QVERIFY(!QFileInfo::exists(job.Destination));
        QVERIFY(SourceExtractFixture::writeFile(release, "close unstarted form"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    }
};

QTEST_MAIN(DraftQuitTests)
#include "draft_quit_tests.moc"
