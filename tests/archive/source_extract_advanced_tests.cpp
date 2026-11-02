#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivebatchextract.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/extract/ExtractDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "formats/cpio/support/dot_path_cpio_fixture.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/native_link_fixture.h"
#include "platform/support/native_security_fixture.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

namespace {
using namespace SourceExtractFixture;
bool runEngine(const QString &directory, const QStringList &arguments) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QStandardPaths::findExecutable("7z"), arguments);
    return process.waitForFinished(10000) && process.exitCode() == 0;
}
// A failed native worker displays its source final message. Dismiss only those
// expected terminal dialogs so the test can observe the returned result.
class CErrorDismissal {
    QTimer _timer;

  public:
    CErrorDismissal() {
        QObject::connect(&_timer, &QTimer::timeout, [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    box->accept();
                if (widget->objectName() == "progressDialog" && widget->isVisible()) {
                    auto *close = widget->findChild<QPushButton *>("IDCANCEL");
                    if (close && close->property("languageID").toUInt() == IDS_CLOSE)
                        close->click();
                }
            }
        });
        _timer.start(1);
    }
};
} // namespace

class SourceExtractAdvancedTests final : public QObject {
    Q_OBJECT
    QTemporaryDir _settings;
    QByteArray _oldSettings;
  private slots:
    void initTestCase() {
        QVERIFY(_settings.isValid());
        _oldSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", _settings.filePath("options.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (_oldSettings.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", _oldSettings);
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void nativeSecurityOptionRestoresArchivedDacl() {
#if defined(Q_OS_WIN) && defined(SEVENZIP_CODEC_MODULE)
        QTemporaryDir input, output;
        const QString name = QStringLiteral("security файл with spaces.bin");
        const QString source = input.filePath(name);
        QVERIFY(writeFile(source, "security payload"));
        QVERIFY(SetFixturePublicDacl(source));
        const QString ace = QStringLiteral("(A;;FA;;;WD)");
        QVERIFY(FixtureDacl(source).contains(ace));
        const QString archive = input.filePath("security.wim");
        QVERIFY(runEngine(input.path(), {"a", "-twim", "-sni", "--", archive, name}));
        const QByteArray original = readFile(archive);
        ArchiveBackend backend;
        const QString module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog probe(module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(module, probe.version()), S_OK);
        for (bool restore : {false, true}) {
            ExtractionOptions options;
            options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
            options.NtSecurity.Def = true;
            options.NtSecurity.Val = restore;
            const QString destination = output.filePath(restore ? "restore" : "default");
            CErrorDismissal dismissal;
            const auto result =
                RunQtArchiveBatchExtract(backend, {archive}, destination, {}, options, nullptr);
            QCOMPARE(result.Result, S_OK);
            QVERIFY(result.CallbackOK);
            const QString extracted = QDir(destination).filePath(name);
            QCOMPARE(readFile(extracted), QByteArray("security payload"));
            const QString dacl = FixtureDacl(extracted);
            QVERIFY(!dacl.isEmpty());
            QCOMPARE(dacl.contains(ace), restore);
        }
        QCOMPARE(readFile(archive), original);
        QVERIFY(FixtureDacl(source).contains(ace));
#else
        QSKIP("Native NTFS security descriptors require Windows.");
#endif
    }
    void wholeNativeArchivePreservesAlternateStreams() {
#if defined(Q_OS_WIN) && defined(SEVENZIP_CODEC_MODULE)
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString name = QStringLiteral("файл δ with space.bin");
        const QString stream = QStringLiteral("поток δ");
        QVERIFY(writeFile(input.filePath(name), "main bytes"));
        QVERIFY(writeFile(input.filePath(name + ':' + stream), "stream bytes"));
        const QString archive = input.filePath("native streams.wim");
        QVERIFY(runEngine(input.path(), {"a", "-twim", "-sns", "--", archive, name}));
        const QByteArray original = readFile(archive);
        ArchiveBackend backend;
        const QString module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog probe(module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(module, probe.version()), S_OK);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        CErrorDismissal dismissal;
        const auto result =
            RunQtArchiveBatchExtract(backend, {archive}, output.path(), {}, options, nullptr);
        QCOMPARE(result.Result, S_OK);
        QVERIFY(result.CallbackOK);
        QCOMPARE(readFile(output.filePath(name)), QByteArray("main bytes"));
        QCOMPARE(readFile(output.filePath(name + ':' + stream)), QByteArray("stream bytes"));
        QCOMPARE(readFile(archive), original);
#else
        QSKIP("Native filesystem alternate streams require Windows.");
#endif
    }
    void sourceBoolPrecedenceAndCancel() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        NExtract::CInfo saved;
        saved.Load(settings);
        saved.ElimDup.SetTrueTrue();
        saved.NtSecurity.SetTrueTrue();
        saved.Save(settings);
        CExtractDialog dialog(_settings.path(), {});
        CBoolPair forced;
        forced.Def = true;
        forced.Val = false;
        dialog.SetMetadataOptions(forced, forced);
        QVERIFY(!dialog.findChild<QCheckBox *>("extractElimDup")->isChecked());
        QVERIFY(!dialog.findChild<QCheckBox *>("extractNtSecurity")->isChecked());
        dialog.reject();
        saved.Load(settings);
        QVERIFY(saved.ElimDup.Val);
        QVERIFY(saved.NtSecurity.Val);
        CExtractDialog changed(_settings.path(), {});
        changed.findChild<QCheckBox *>("extractElimDup")->setChecked(false);
        changed.findChild<QCheckBox *>("extractNtSecurity")->setChecked(false);
        changed.findChild<QComboBox *>("extractPathMode")->setCurrentIndex(2);
        changed.accept();
        saved.Load(settings);
        QVERIFY(saved.ElimDup.Def && !saved.ElimDup.Val);
        QVERIFY(saved.NtSecurity.Def && !saved.NtSecurity.Val);
        QCOMPARE(saved.PathMode, NExtract::NPathMode::kAbsPaths);
        QCOMPARE(changed.options().pathMode, ExtractionOptions::PathMode::AbsolutePaths);
    }
    void geometryAndLanguage_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(700, 390), QSize(600, 340)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }
    void geometryAndLanguage() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        SaveRegLang(UString(language.toStdWString().c_str()),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtNotifyLanguageChanged();
        CExtractDialog dialog(_settings.path(), {});
        auto *advanced = dialog.findChild<QToolButton *>("extractAdvancedToggle");
        auto *passwordEnabled = dialog.findChild<QCheckBox *>("extractPasswordEnabled");
        QVERIFY(advanced && passwordEnabled);
        advanced->click();
        passwordEnabled->click();
        dialog.resize(size);
        dialog.show();
        QApplication::processEvents();
        for (const char *name : {"extractElimDup", "extractNtSecurity", "extractPathMode",
                                 "extractOverwriteMode", "extractPassword"}) {
            auto *control = dialog.findChild<QWidget *>(name);
            QVERIFY(control);
#ifndef Q_OS_WIN
            if (control->objectName() == "extractNtSecurity") {
                QVERIFY(!control->isVisible());
                continue;
            }
#endif
            QVERIFY2(UiInspection::fullyVisible(*control, dialog), name);
            if (auto *button = qobject_cast<QCheckBox *>(control))
                QVERIFY2(button->width() >= button->sizeHint().width(), name);
        }
        auto *elim = dialog.findChild<QWidget *>("extractElimDup");
        auto *mode = dialog.findChild<QWidget *>("extractPathMode");
        auto *overwrite = dialog.findChild<QWidget *>("extractOverwriteMode");
        QVERIFY(UiInspection::bounds(*mode, dialog).bottom() <
                UiInspection::bounds(*elim, dialog).top());
        QVERIFY(UiInspection::bounds(*overwrite, dialog).bottom() <
                UiInspection::bounds(*mode, dialog).top());
#ifndef Q_OS_WIN
        QVERIFY(!dialog.findChild<QCheckBox *>("extractNtSecurity")->isEnabled());
#endif
        QVERIFY(UiInspection::writeReport(
            dialog, QString("extract-advanced-%1-%2").arg(language).arg(size.width())));
        dialog.reject();
        SaveRegLang(UString(L"-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtNotifyLanguageChanged();
    }
    void rootEliminationAndMetadata_data() {
        QTest::addColumn<bool>("native");
        QTest::newRow("native-discovery") << false;
#ifdef SEVENZIP_CODEC_MODULE
        QTest::newRow("module-catalog") << true;
#endif
    }
    void dotPathsInCpio() {
        QTemporaryDir input, output;
        const QString archive = input.filePath("Payload.cpio");
        QVERIFY(writeFile(archive, DotPathCpioFixture()));
        ArchiveBackend backend;
        CErrorDismissal dismissal;
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        const auto result =
            RunQtArchiveBatchExtract(backend, {archive}, output.path(), {}, options, nullptr);
        QCOMPARE(result.Result, S_OK);
        QVERIFY(result.CallbackOK);
        QCOMPARE(readFile(output.filePath(QStringLiteral("ProxyBridge.app/имя 空.txt"))),
                 QByteArray("payload bytes"));
    }
    void rootEliminationAndMetadata() {
        QFETCH(bool, native);
        QTemporaryDir input, output;
        const auto archive = makeArchive(input.path(), "exact payload");
        QVERIFY(!archive.isEmpty());
        const auto original = readFile(archive);
        ArchiveBackend backend;
        if (native) {
#ifdef SEVENZIP_CODEC_MODULE
            const QString module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
            ArchiveCodecCatalog probe(module, 0);
            QCOMPARE(probe.result(), S_OK);
            QCOMPARE(backend.loadCodecCatalog(module, probe.version()), S_OK);
#endif
        }
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        for (bool eliminate : {false, true}) {
            options.ElimDup.Val = eliminate;
            const QString destination = output.filePath(eliminate ? "on/inside" : "off/inside");
            const auto result =
                RunQtArchiveBatchExtract(backend, {archive}, destination, {}, options, nullptr);
            QCOMPARE(result.Result, S_OK);
            QVERIFY(result.CallbackOK);
            const QString leaf = eliminate ? kName.section('/', -1) : kName;
            const QString extracted = QDir(destination).filePath(leaf);
            QCOMPARE(readFile(extracted), QByteArray("exact payload"));
            QCOMPARE(QFileInfo(extracted).lastModified().toSecsSinceEpoch(),
                     QFileInfo(input.filePath(kName)).lastModified().toSecsSinceEpoch());
            QVERIFY(!backend.busy());
        }
        QCOMPARE(readFile(archive), original);
    }
    void eliminationCancelKeepsCompletedFilesAndExistingTarget_data() {
        rootEliminationAndMetadata_data();
    }
    void eliminationCancelKeepsCompletedFilesAndExistingTarget() {
        QFETCH(bool, native);
        QTemporaryDir input, output;
        QVERIFY(writeFile(input.filePath("inside/a 空.bin"), "first bytes"));
        QVERIFY(writeFile(input.filePath("inside/z last.bin"), "second bytes"));
        const QString archive = input.filePath("cancel.zip");
        QVERIFY(runEngine(input.path(), {"a", "-tzip", archive, "inside"}));
        const QByteArray original = readFile(archive);
        const QString destination = output.filePath("inside");
        const QString existing = QDir(destination).filePath("z last.bin");
        QVERIFY(writeFile(existing, "preserved"));
        ArchiveBackend backend;
        if (native) {
#ifdef SEVENZIP_CODEC_MODULE
            const QString module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
            ArchiveCodecCatalog probe(module, 0);
            QCOMPARE(probe.result(), S_OK);
            QCOMPARE(backend.loadCodecCatalog(module, probe.version()), S_OK);
#endif
        }
        bool answered = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &backend, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                    dialog && dialog->isVisible()) {
                    answered = true;
                    dialog->findChild<QPushButton *>("overwriteCancel")->click();
                }
        });
        answer.start(1);
        ExtractionOptions options;
        options.ElimDup.SetTrueTrue();
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        const auto cancelled =
            RunQtArchiveBatchExtract(backend, {archive}, destination, {}, options, nullptr);
        answer.stop();
        QCOMPARE(cancelled.Result, E_ABORT);
        QVERIFY(answered);
        QCOMPARE(readFile(QDir(destination).filePath("a 空.bin")), QByteArray("first bytes"));
        QCOMPARE(readFile(existing), QByteArray("preserved"));
        QCOMPARE(readFile(archive), original);
        QVERIFY(!backend.busy());
    }
    void absolutePathsAreBoundedBeforeWriting() {
        QTemporaryDir fixture;
        const QString destination = fixture.filePath("output");
        const QString source = QDir(destination).filePath(kName);
        QVERIFY(writeFile(source, "absolute bytes"));
        const QString archive = fixture.filePath("absolute.zip");
        QVERIFY(runEngine(fixture.path(), {"a", "-tzip", "-spf", archive, source}));
        QVERIFY(QFile::remove(source));
        ArchiveBackend backend;
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::AbsolutePaths;
        options.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        const auto accepted =
            RunQtArchiveBatchExtract(backend, {archive}, destination, {}, options, nullptr);
        QCOMPARE(accepted.Result, S_OK);
        QVERIFY(accepted.CallbackOK);
        QCOMPARE(readFile(source), QByteArray("absolute bytes"));
        QVERIFY(QFile::remove(source));
        CErrorDismissal dismissal;
        const auto denied = RunQtArchiveBatchExtract(backend, {archive}, fixture.filePath("other"),
                                                     {}, options, nullptr);
        QVERIFY(denied.Result != S_OK || !denied.CallbackOK);
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(!backend.busy());
    }
#ifndef Q_OS_WIN
    void standaloneStreamChecksItsActualOutput_data() {
        QTest::addColumn<bool>("occupied");
        QTest::addColumn<bool>("asynchronous");
        QTest::newRow("one-character-owner") << false << false;
        QTest::newRow("existing-output-link") << true << false;
        QTest::newRow("async-one-character-owner") << false << true;
        QTest::newRow("async-existing-output-link") << true << true;
    }
    void standaloneStreamChecksItsActualOutput() {
        QFETCH(bool, occupied);
        QFETCH(bool, asynchronous);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        NativeArchiveFixture::Item host;
        host.Path = "h";
        host.Bytes = "host bytes";
        auto stream = host;
        stream.Path += ":stream δ";
        stream.Bytes = "stream bytes";
        stream.AltStream = true;
        const QString archive = fixture.filePath("streams.wim");
        QVERIFY(NativeArchiveFixture::Write(archive, {host, stream}, "wim"));
        const QByteArray original = readFile(archive);
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(output));
        const QString streamOutput = QDir(output).filePath(stream.Path);
        const QString victim = fixture.filePath("outside.bin");
        QVERIFY(writeFile(victim, "preserved"));
        if (occupied)
            QVERIFY(CreateNativeSymbolicLink(streamOutput, victim));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        CErrorDismissal dismissal;
        bool success;
        if (asynchronous) {
            QSignalSpy finished(&backend, &ArchiveBackend::finished);
            backend.list(archive);
            QTRY_COMPARE(finished.size(), 1);
            QVERIFY(finished.takeFirst().at(0).toBool());
            backend.extract(archive, output, {}, {}, options);
            QTRY_COMPARE(finished.size(), 1);
            success = finished.takeFirst().at(0).toBool();
        } else {
            const auto result =
                RunQtArchiveBatchExtract(backend, {archive}, output, {}, options, nullptr);
            success = result.Result == S_OK && result.CallbackOK;
        }
        if (occupied) {
            QVERIFY(!success);
            QVERIFY(QFileInfo(streamOutput).isSymLink());
        } else {
            QVERIFY(success);
            QCOMPARE(readFile(QDir(output).filePath(host.Path)), host.Bytes);
            QCOMPARE(readFile(streamOutput), stream.Bytes);
        }
        QCOMPARE(readFile(victim), QByteArray("preserved"));
        QCOMPARE(readFile(archive), original);
        QString error;
        QVERIFY(!ArchiveBackend::validateExtractionStreamOutput(
            output, fixture.filePath("outside:stream"), &error));
        QVERIFY(!backend.busy());
    }
#endif
    void correctedPathRejectsExistingLink() {
        QTemporaryDir input, output, outside;
        const auto archive = makeArchive(input.path(), "incoming bytes");
        QVERIFY(!archive.isEmpty());
        const QString destination = output.filePath("inside");
        const QString leaf = kName.section('/', -1);
        const QString victim = outside.filePath("preserved.bin");
        QVERIFY(writeFile(victim, "preserved"));
        QVERIFY(QDir().mkpath(destination));
        const QString link = QDir(destination).filePath(leaf);
        QVERIFY(CreateNativeSymbolicLink(link, victim));
        QVERIFY(QFileInfo(link).isSymLink());
        ArchiveBackend backend;
        ExtractionOptions options;
        options.ElimDup.SetTrueTrue();
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        CErrorDismissal dismissal;
        const auto rejected =
            RunQtArchiveBatchExtract(backend, {archive}, destination, {}, options, nullptr);
        QVERIFY(rejected.Result != S_OK || !rejected.CallbackOK);
        QCOMPARE(readFile(victim), QByteArray("preserved"));
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(!backend.busy());
    }
};
QTEST_MAIN(SourceExtractAdvancedTests)
#include "source_extract_advanced_tests.moc"
