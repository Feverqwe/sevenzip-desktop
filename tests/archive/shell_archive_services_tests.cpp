#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/extract/ExtractDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "localization/qtlangcontrols.h"
#include "operations/CompressCall.h"
#include "platform/shell/qtshellarchiveservices.h"
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>

namespace {
using namespace SourceExtractFixture;
UString us(const QString &s) { return UString(s.toStdWString().c_str()); }
class CDismissal {
    QTimer _timer;
    bool _waitForHashResult;

  public:
    QString ResultText;
    QStringList Errors;
    explicit CDismissal(bool waitForHashResult = false) : _waitForHashResult(waitForHashResult) {
        QObject::connect(&_timer, &QTimer::timeout, [this] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                    if (message->icon() == QMessageBox::Critical ||
                        message->icon() == QMessageBox::Warning) {
                        Errors.append(message->text());
                        _waitForHashResult = false;
                    }
                    message->accept();
                    return;
                }
                if (auto *results = qobject_cast<QtTaskResultDialog *>(widget)) {
                    _waitForHashResult = false;
                    ResultText =
                        results->findChild<QPlainTextEdit *>("taskResultReport")->toPlainText();
                    results->accept();
                    return;
                }
                if (!_waitForHashResult && widget->objectName() == "progressDialog" &&
                    QApplication::activeModalWidget() == widget) {
                    auto *close = widget->findChild<QPushButton *>("IDCANCEL");
                    if (close && close->property("languageID").toUInt() == IDS_CLOSE)
                        close->click();
                }
            }
        });
        _timer.start(5);
    }
};
bool engine(const QString &dir, const QStringList &args) {
    QProcess process;
    process.setWorkingDirectory(dir);
    process.start(QStandardPaths::findExecutable("7z"), args);
    return process.waitForFinished(10000) && process.exitCode() == 0;
}
} // namespace

class ShellArchiveServicesTests final : public QObject {
    Q_OBJECT
    QTemporaryDir _settings;
    QByteArray _oldSettings;
    QString _module;

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official native runtime unavailable");
#else
        _module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        QVERIFY(_settings.isValid());
        _oldSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", _settings.filePath("settings.ini").toUtf8());
#endif
    }
    void cleanupTestCase() {
        if (_oldSettings.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", _oldSettings);
    }
    void nativeMultiArchiveWildcardExtractAndActualTest() {
        QTemporaryDir input, output;
        const QString name = QStringLiteral("данные 空 с пробелом.bin");
        QVERIFY(writeFile(input.filePath(name), "shell native payload"));
        const QString a = input.filePath("a.zip"), b = input.filePath("b.7z");
        QVERIFY(engine(input.path(), {"a", "-tzip", "--", a, name}));
        QVERIFY(engine(input.path(), {"a", "-t7z", "--", b, name}));
        ArchiveBackend backend;
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(_module, probe.version()), S_OK);
        HRESULT error = S_OK;
        QtShellArchiveServices service(
            backend, nullptr, {.Error = [&](HRESULT result, const UString &) { error = result; }});
        UStringVector paths;
        paths.Add(us(b));
        paths.Add(us(a));
        CDismissal dismissal;
        QCOMPARE(
            ::ExtractArchives(paths, us(output.filePath("*")), false, false, UInt32(-1), service),
            S_OK);
        QCOMPARE(error, S_OK);
        QVERIFY(dismissal.Errors.isEmpty());
        QCOMPARE(readFile(output.filePath("a/" + name)), QByteArray("shell native payload"));
        QCOMPARE(readFile(output.filePath("b/" + name)), QByteArray("shell native payload"));
        QCOMPARE(::TestArchives(paths, false, service), S_OK);
        const QByteArray zipBytes = readFile(a), sevenZipBytes = readFile(b);
        QCOMPARE(::TestArchives(paths, true, service), E_FAIL);
        QCOMPARE(readFile(a), zipBytes);
        QCOMPARE(readFile(b), sevenZipBytes);
        QVERIFY(!backend.busy());
    }
    void sourceDialogRetainsArchiveCaptionAndCancel_data() {
        QTest::addColumn<bool>("multiple");
        QTest::newRow("single") << false;
        QTest::newRow("multiple") << true;
    }
    void sourceDialogRetainsArchiveCaptionAndCancel() {
        QFETCH(bool, multiple);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("архив δ with spaces.zip"));
        QVERIFY(writeFile(archive, "not opened before Cancel"));
        ArchiveBackend backend;
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(_module, probe.version()), S_OK);
        QtShellArchiveServices service(backend, nullptr,
                                       {.Error = [](HRESULT, const UString &) {}});
        UStringVector paths;
        paths.Add(us(archive));
        if (multiple) {
            const QString second = fixture.filePath("second.7z");
            QVERIFY(writeFile(second, "second unchanged"));
            paths.Add(us(second));
        }
        QString caption;
        bool seen = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            if (auto *dialog = dynamic_cast<CExtractDialog *>(QApplication::activeModalWidget())) {
                seen = true;
                caption = dialog->windowTitle();
                dialog->reject();
            }
        });
        driver.start(1);
        QCOMPARE(::ExtractArchives(paths, us(fixture.filePath("output")), true, false, UInt32(-1),
                                   service),
                 E_ABORT);
        QVERIFY(seen);
        QCOMPARE(caption, QtLangText(IDD_EXTRACT, "Extract") +
                              (multiple ? QString() : " : " + QDir::toNativeSeparators(archive)));
        QCOMPARE(readFile(archive), QByteArray("not opened before Cancel"));
        QVERIFY(!QFileInfo::exists(fixture.filePath("output")));
        QVERIFY(!backend.busy());
    }
    void actualHashGuiUsesRetainedNativeProviders() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("checksum 空 file.bin"));
        QVERIFY(writeFile(path, "abc"));
        ArchiveBackend backend;
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(_module, probe.version()), S_OK);
        HRESULT error = S_OK;
        QtShellArchiveServices service(
            backend, nullptr, {.Error = [&](HRESULT result, const UString &) { error = result; }});
        UStringVector paths;
        paths.Add(us(path));
        // Progress exposes Close before its native callback opens the nested
        // hash report. A programmatic click must not retire that parent first.
        CDismissal dismissal(true);
        QCOMPARE(::CalcChecksum(paths, UString(L"SHA256"), service), S_OK);
        QCOMPARE(error, S_OK);
        QVERIFY(dismissal.Errors.isEmpty());
        QVERIFY2(dismissal.ResultText.contains(QStringLiteral(
                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")),
                 qPrintable(dismissal.ResultText));
        QCOMPARE(readFile(path), QByteArray("abc"));
    }
    void archiveTestRejectsChecksumManifest() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("checksum 空 file.bin");
        QVERIFY(writeFile(fixture.filePath("inner/" + name), "abc"));
        const QString sentinel = fixture.filePath("outside.bin");
        QVERIFY(writeFile(sentinel, "abc"));
        const QString manifest = fixture.filePath("inner/checksums.sha256");
        const QByteArray bytes =
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  " + name.toUtf8() +
            "\n";
        QVERIFY(writeFile(manifest, bytes));
        ArchiveBackend backend;
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(_module, probe.version()), S_OK);
        QtShellArchiveServices service(backend, nullptr,
                                       {.Error = [](HRESULT, const UString &) {}});
        UStringVector paths;
        paths.Add(us(manifest));
        CDismissal dismissal;
        QCOMPARE(::TestArchives(paths, false, service), E_FAIL);
        QCOMPARE(readFile(manifest), bytes);
        QCOMPARE(readFile(sentinel), QByteArray("abc"));
        QVERIFY(!backend.busy());
    }
    void overwriteBatchDecisionPreservesArchiveAndExistingFiles_data() {
        QTest::addColumn<QString>("decision");
        QTest::newRow("cancel-button") << QString("cancel");
        QTest::newRow("escape") << QString("escape");
        QTest::newRow("keep-both-for-all") << QString("keep");
    }
    void overwriteBatchDecisionPreservesArchiveAndExistingFiles() {
        QFETCH(QString, decision);
        const bool keep = decision == "keep";
        QTemporaryDir input, output;
        const QString name = QStringLiteral("existing 空 file.bin");
        const QString other = QStringLiteral("second conflict.bin");
        QVERIFY(writeFile(input.filePath(name), "archive bytes"));
        QVERIFY(writeFile(input.filePath(other), "second archive bytes"));
        const QString archive = input.filePath("input.zip");
        QVERIFY(engine(input.path(), {"a", "-tzip", "--", archive, name, other}));
        const QByteArray original = readFile(archive);
        QVERIFY(writeFile(output.filePath(name), "preserved bytes"));
        QVERIFY(writeFile(output.filePath(other), "second preserved bytes"));
        ArchiveBackend backend;
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(backend.loadCodecCatalog(_module, probe.version()), S_OK);
        HRESULT error = S_OK;
        QtShellArchiveServices service(
            backend, nullptr, {.Error = [&](HRESULT result, const UString &) { error = result; }});
        bool applyAvailable = false;
        int prompts = 0;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                    dialog && dialog->isVisible()) {
                    auto *apply = dialog->findChild<QCheckBox *>("overwriteApplyToRemaining");
                    ++prompts;
                    applyAvailable = apply && apply->isVisible() && apply->isEnabled();
                    if (applyAvailable)
                        apply->setChecked(true);
                    if (keep && prompts == 1)
                        dialog->findChild<QPushButton *>("overwriteRename")->click();
                    else if (decision == "escape")
                        QTest::keyClick(dialog, Qt::Key_Escape);
                    else
                        dialog->findChild<QPushButton *>("overwriteCancel")->click();
                }
        });
        answer.start(1);
        UStringVector paths;
        paths.Add(us(archive));
        CDismissal dismissal;
        QCOMPARE(::ExtractArchives(paths, us(output.path()), false, false, UInt32(-1), service),
                 keep ? S_OK : E_ABORT);
        QVERIFY(applyAvailable);
        QCOMPARE(prompts, 1);
        QCOMPARE(error, S_OK);
        QCOMPARE(readFile(output.filePath(name)), QByteArray("preserved bytes"));
        QCOMPARE(readFile(output.filePath(other)), QByteArray("second preserved bytes"));
        QCOMPARE(readFile(archive), original);
        auto copies = QDir(output.path()).entryList(QDir::Files | QDir::Hidden);
        copies.removeAll(name);
        copies.removeAll(other);
        QList<QByteArray> contents;
        for (const auto &copy : copies)
            contents.append(readFile(output.filePath(copy)));
        std::sort(contents.begin(), contents.end());
        QCOMPARE(contents, keep ? (QList<QByteArray>{"archive bytes", "second archive bytes"})
                                : QList<QByteArray>());
        QVERIFY(!backend.busy());
    }
};
QTEST_MAIN(ShellArchiveServicesTests)
#include "shell_archive_services_tests.moc"
