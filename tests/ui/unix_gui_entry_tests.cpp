#include "archive/support/source_extract_fixture.h"
#include "support/operation_program.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

class UnixGuiEntryTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { ConfigureInstalledOperationPlatform(); }
    void sourceDispatch_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("excluded");
        QTest::addColumn<bool>("broken");
        QTest::addColumn<bool>("moduleOnly");
        QTest::addColumn<bool>("skip");
        QTest::newRow("create") << QString("a") << false << false << false << false;
        QTest::newRow("test") << QString("t") << false << false << false << false;
        QTest::newRow("extract") << QString("x") << false << false << false << false;
        QTest::newRow("skip-existing") << QString("x") << false << false << false << true;
        QTest::newRow("excluded-handler") << QString("x") << true << false << false << false;
        QTest::newRow("broken-archive") << QString("t") << false << true << false << false;
        QTest::newRow("test-without-console") << QString("t") << false << false << true << false;
        QTest::newRow("damaged-without-console") << QString("t") << false << true << true << false;
    }
    void sourceDispatch() {
        QFETCH(QString, command);
        QFETCH(bool, excluded);
        QFETCH(bool, broken);
        QFETCH(bool, moduleOnly);
        QFETCH(bool, skip);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("Unix source GUI entry payload");
        const QString original = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!original.isEmpty());
        const QString archive =
            fixture.filePath(QStringLiteral("архив 密碼 with spaces \"literal quotes\".zip"));
        QVERIFY(QFile::rename(original, archive));
        if (broken)
            QVERIFY(SourceExtractFixture::writeFile(archive, "broken ZIP"));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        const QString output = fixture.filePath(QStringLiteral("выход 空 \"quoted\""));
        const QString sentinel = QDir(output).filePath("existing.bin");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, "keep"));
        if (skip)
            QVERIFY(SourceExtractFixture::writeFile(
                QDir(output).filePath(SourceExtractFixture::kName), "existing bytes"));
        QStringList arguments{command, "-y", "-o" + output};
        if (skip)
            arguments << "-aos";
        if (excluded)
            arguments << "-stxzip";
        const QString created = fixture.filePath(QStringLiteral("новый 空 \"quoted\".zip"));
        if (command == "a")
            arguments << "-tzip" << "--" << created << SourceExtractFixture::kName;
        else
            arguments << "--" << archive;
        QProcess helper;
        if (moduleOnly) {
#ifdef SEVENZIP_CODEC_MODULE
            const QString deployment = fixture.filePath("module-only deployment");
            QVERIFY(QDir().mkpath(deployment));
            const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
            const QString copiedModule = QDir(deployment).filePath(QFileInfo(module).fileName());
            QVERIFY(QFile::copy(module, copiedModule));
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("SEVENZIP_CODEC_MODULE_PATH", copiedModule);
            helper.setProcessEnvironment(environment);
#else
            QSKIP("The native module is not configured for this build");
#endif
        }
        helper.setWorkingDirectory(fixture.path());
        helper.start(NativeOperationProgram(SEVENZIP_NATIVE_GUI), arguments);
        QVERIFY(helper.waitForStarted(5000));
        QVERIFY2(helper.waitForFinished(30000), "source GUI dispatch did not complete");
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), excluded || broken ? 2 : skip ? 1 : 0);
        const QString extracted = QDir(output).filePath(SourceExtractFixture::kName);
        if (command == "x" && !excluded)
            QCOMPARE(SourceExtractFixture::readFile(extracted),
                     skip ? QByteArray("existing bytes") : bytes);
        else
            QVERIFY(!QFileInfo::exists(extracted));
        if (command == "a") {
            QProcess verify;
            verify.start(QStandardPaths::findExecutable("7z"), {"t", "--", created});
            QVERIFY(verify.waitForFinished(10000));
            QCOMPARE(verify.exitCode(), 0);
            QProcess extract;
            extract.start(QStandardPaths::findExecutable("7z"),
                          {"x", "-y", "-o" + output, "--", created});
            QVERIFY(extract.waitForFinished(10000));
            QCOMPARE(extract.exitCode(), 0);
            QCOMPARE(SourceExtractFixture::readFile(extracted), bytes);
        }
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("keep"));
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
    }
};

QTEST_GUILESS_MAIN(UnixGuiEntryTests)
#include "unix_gui_entry_tests.moc"
