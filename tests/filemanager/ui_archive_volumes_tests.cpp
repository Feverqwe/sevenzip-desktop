#include "7zip/UI/FileManager/resourceGui.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "support/progress_dialog.h"
#include <QPushButton>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
const QString kName = QStringLiteral("密碼 с пробелом.bin");
class CDialogAnswers {
    QTimer timer_;

  public:
    QString Password;
    int PasswordPrompts = 0;
    int Errors = 0;
    CDialogAnswers() {
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
            if (QWidget *widget = QApplication::activeModalWidget()) {
                if (auto *password = dynamic_cast<CPasswordDialog *>(widget)) {
                    ++PasswordPrompts;
                    password->findChild<QLineEdit *>("passwordEdit")->setText(Password);
                    password->accept();
                } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    if (box->icon() == QMessageBox::Critical || box->icon() == QMessageBox::Warning)
                        ++Errors;
                    box->accept();
                } else if (auto *result = TestProgressDialog::ResultWindow(widget)) {
                    const auto report = TestProgressDialog::ReportText(*result);
                    if (!report.isEmpty() && !report.contains(QString::fromWCharArray(
                                                 LangString(IDS_MESSAGE_NO_ERRORS))))
                        ++Errors;
                    TestProgressDialog::CloseResult(*result);
                }
            }
        });
        timer_.start(1);
    }
};
} // namespace

class UiArchiveVolumesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    bool create(ArchiveBackend &backend, const QString &directory, const QString &format,
                const QString &password, QString &first) {
        const QString source = QDir(directory).filePath(kName);
        const QString other = QDir(directory).filePath("keep.txt");
        if (!writeFile(source, QByteArray(400000, 'x')))
            return false;
        if (!writeFile(other, "kept"))
            return false;
        const QString target = QDir(directory).filePath("volumes." + format);
        CompressionOptions options;
        options.Level = 0;
        options.VolumeSizes.Add(128 << 10);
        const HRESULT result =
            SourceCreationFixture::Create(backend, target, {source, other}, password,
                                          !password.isEmpty() && format == "7z", options);
        first = target + ".001";
        return result == S_OK && QFileInfo::exists(target + ".003");
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void sourceConsumersRoundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("password");
        QTest::newRow("zip") << QString("zip") << QString();
        QTest::newRow("7z") << QString("7z") << QString();
        QTest::newRow("zip-password") << QString("zip") << QString("fixture123");
        QTest::newRow("7z-header-password") << QString("7z") << QString("fixture123");
    }
    void sourceConsumersRoundTrip() {
        QFETCH(QString, format);
        QFETCH(QString, password);
        QTemporaryDir fixture, output;
        QVERIFY(fixture.isValid());
        QVERIFY(output.isValid());
        ArchiveBackend backend;
        QString first;
        QVERIFY(create(backend, fixture.path(), format, password, first));
        const QByteArray firstBytes = readFile(first);
        CDialogAnswers answers;
        answers.Password = password;
        MainWindow window(&backend);
        window.show();
        window.openArchive(first);
        QTRY_COMPARE(table(window)->rowCount(), 2);
        const int selected = rowWithPath(window, kName);
        QVERIFY(selected >= 0);
        table(window)->selectRow(selected);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        QVERIFY(!action(window, "deleteAction")->isEnabled());
        QVERIFY(!action(window, "renameAction")->isEnabled());
        window.testArchive();
        QVERIFY(!backend.busy());
        QCOMPARE(answers.Errors, 0);
        QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList());
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        window.extractTo(output.path(), false, password, options);
        QCOMPARE(readFile(output.filePath(kName)), QByteArray(400000, 'x'));
        QCOMPARE(readFile(output.filePath("keep.txt")), QByteArray("kept"));
        QCOMPARE(readFile(first), firstBytes);
        QVERIFY(!backend.busy());
        QCOMPARE(answers.Errors, 0);
        if (format == "7z" && !password.isEmpty())
            QVERIFY(answers.PasswordPrompts > 0);
    }
    void refreshNoticesMissingLaterVolumeAndAllowsRetry() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ArchiveBackend backend;
        QString first;
        QVERIFY(create(backend, fixture.path(), "7z", {}, first));
        const QString second = first.left(first.size() - 3) + "002";
        const QString parked = fixture.filePath("parked volume");
        CDialogAnswers answers;
        MainWindow window(&backend);
        window.show();
        window.openArchive(first);
        QTRY_COMPARE(table(window)->rowCount(), 2);
        QVERIFY(QFile::rename(second, parked));
        auto *refresh = action(window, "refreshAction");
        QVERIFY(refresh->isEnabled());
        refresh->trigger();
        auto *retry = window.findChild<QPushButton *>("navigationRetry0");
        QTRY_VERIFY(retry && retry->isVisible());
        QCOMPARE(table(window)->rowCount(), 2);
        const QString retained = fixture.filePath("retained output");
        const int extractionErrors = answers.Errors;
        window.extractTo(retained);
        QCOMPARE(readFile(QDir(retained).filePath(kName)), QByteArray(400000, 'x'));
        QCOMPARE(answers.Errors, extractionErrors);
        QVERIFY(retry->isVisible());
        QVERIFY(QFile::rename(parked, second));
        retry->click();
        QTRY_VERIFY(!retry->isVisible());
        QCOMPARE(table(window)->rowCount(), 2);
        const int errors = answers.Errors;
        window.testArchive();
        QCOMPARE(answers.Errors, errors);
    }

    void missingAndCorruptVolumesReportErrors_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("missing-open") << 0;
        QTest::newRow("corrupt-test") << 1;
    }
    void missingAndCorruptVolumesReportErrors() {
        QFETCH(int, mode);
        QTemporaryDir fixture, output;
        QVERIFY(fixture.isValid());
        QVERIFY(output.isValid());
        ArchiveBackend backend;
        QString first;
        QVERIFY(create(backend, fixture.path(), "7z", {}, first));
        const QString second = first.left(first.size() - 3) + "002";
        CDialogAnswers answers;
        MainWindow window(&backend);
        window.show();
        if (mode == 0) {
            const QByteArray missingVolume = readFile(second);
            QVERIFY(QFile::remove(second));
            window.openArchive(first);
            QVERIFY(answers.Errors > 0);
            QVERIFY(!backend.busy());
            const int errors = answers.Errors;
            QVERIFY(writeFile(second, missingVolume));
            window.openArchive(first);
            QTRY_COMPARE(table(window)->rowCount(), 2);
            QVERIFY(action(window, "testArchiveAction")->isEnabled());
            window.testArchive();
            QCOMPARE(answers.Errors, errors);
            return;
        }
        const QByteArray firstBytes = readFile(first);
        QVERIFY(writeFile(second, QByteArray(readFile(second).size(), 'z')));
        window.openArchive(first);
        QTRY_COMPARE(table(window)->rowCount(), 2);
        window.testArchive();
        QVERIFY(!backend.busy());
        QVERIFY(answers.Errors > 0);
        QCOMPARE(readFile(first), firstBytes);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
    }
};
QTEST_MAIN(UiArchiveVolumesTests)
#include "ui_archive_volumes_tests.moc"
