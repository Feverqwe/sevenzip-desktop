#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_creation_fixture.h"
#include "archive/support/technical_listing_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/support/window_state_support.h"
#include <QProcess>

namespace {
bool runEngine(const QString &directory, const QStringList &arguments,
               QByteArray *output = nullptr) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QStringLiteral("7z"), arguments);
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return false;
    if (output)
        *output = process.readAllStandardOutput();
    return true;
}
} // namespace

class UiUpdateModesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    quint32 moduleVersion_ = 0;
    void selectFilesystemSource(MainWindow &window, ArchiveBackend &backend,
                                const QString &source) {
#ifdef SEVENZIP_CODEC_MODULE
        QCOMPARE(backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), moduleVersion_),
                 S_OK);
#endif
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(QFileInfo(source).absolutePath());
        emit address->returnPressed();
        QTRY_VERIFY(table(window)->isEnabled());
        const QString name = QFileInfo(source).fileName().normalized(QString::NormalizationForm_C);
        int row = -1;
        for (int i = 0; i < table(window)->rowCount(); ++i)
            if (table(window)->model()->index(i, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name)
                row = i;
        QVERIFY(row >= 0);
        table(window)->selectRow(row);
    }
    static void setOutput(CreateArchiveDialog &dialog, const QString &archive,
                          const QString &format) {
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText(format);
        dialog.findChild<QLineEdit *>("archiveName")->setText(archive);
    }

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Source Add requires the official native module");
#else
        ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(probe.result(), S_OK);
        moduleVersion_ = probe.version();
#endif
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void addRetainsInputs_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("zip-success") << QString("zip") << false;
        QTest::newRow("7z-success") << QString("7z") << false;
        QTest::newRow("zip-cancel") << QString("zip") << true;
        QTest::newRow("7z-cancel") << QString("7z") << true;
    }
    void addRetainsInputs() {
        QFETCH(QString, format);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("result." + format);
        const QString source = fixture.filePath(QStringLiteral("добавленный файл.txt"));
        const QString keep = fixture.filePath("keep.txt");
        QFile file(keep);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep bytes"), qint64(10));
        file.close();
        QVERIFY(runEngine(fixture.path(), {"a", "-t" + format, "--", archive, "keep.txt"}));
        QFile original(archive);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray archiveBefore = original.readAll();
        original.close();
        file.setFileName(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("new bytes"), qint64(9));
        file.close();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        selectFilesystemSource(window, backend, source);
        CreateArchiveDialog create(fixture.path(), {source}, &window, backend.codecCatalog());
        bool accepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = &create;
            QVERIFY(dialog);
            setOutput(*dialog, archive, format);
            if (cancel)
                dialog->reject();
            else
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            accepted = dialog->result() == QDialog::Accepted;
        });
        create.exec();
        if (create.result() == QDialog::Accepted)
            QCOMPARE(SourceCreationFixture::Create(backend, archive, create.sources(),
                                                   create.password(), create.encryptHeaders(),
                                                   create.compressionOptions()),
                     S_OK);
        QCOMPARE(accepted, !cancel);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("new bytes"));
        if (cancel) {
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), archiveBefore);
        } else {
            QByteArray listing;
            QVERIFY(runEngine(fixture.path(), {"l", "-slt", "-sccUTF-8", "--", archive}, &listing));
            QVERIFY(QString::fromUtf8(listing)
                        .normalized(QString::NormalizationForm_C)
                        .contains(QStringLiteral("добавленный файл.txt")));
            const QString extracted = fixture.filePath("extracted");
            QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
            QFile restored(QDir(extracted).filePath(QStringLiteral("добавленный файл.txt")));
            QVERIFY(restored.open(QIODevice::ReadOnly));
            QCOMPARE(restored.readAll(), QByteArray("new bytes"));
        }
    }

    void protectedAddUsesEnteredArchivePassword() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("protected.7z");
        const QString added = QDir(root).filePath("added.txt");
        QVERIFY(QDir().mkpath(root));
        QFile keep(QDir(root).filePath("keep.txt"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("old"), qint64(3));
        keep.close();
        QVERIFY(
            runEngine(root, {"a", "-t7z", "-pfixture123", "-mhe=on", "--", archive, "keep.txt"}));
        QFile source(added);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("new"), qint64(3));
        source.close();
        ArchiveBackend backend;
        MainWindow window(&backend);
        int passwordPrompts = 0;
        QTimer passwordAnswer;
        connect(&passwordAnswer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "passwordDialog")
                return;
            ++passwordPrompts;
            dialog->findChild<QLineEdit *>("passwordEdit")->setText("fixture123");
            dialog->accept();
        });
        passwordAnswer.start(1);
        window.show();
        selectFilesystemSource(window, backend, added);
        CreateArchiveDialog create(fixture.path(), {added}, &window, backend.codecCatalog());
        bool accepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = &create;
            if (!dialog)
                return;
            setOutput(*dialog, archive, "7z");
            dialog->findChild<QLineEdit *>("archivePassword")->setText("fixture123");
            dialog->findChild<QLineEdit *>("archivePasswordConfirmation")->setText("fixture123");
            dialog->findChild<QCheckBox *>("archiveEncryptHeaders")->setChecked(true);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            accepted = dialog->result() == QDialog::Accepted;
        });
        create.exec();
        if (create.result() == QDialog::Accepted)
            QCOMPARE(SourceCreationFixture::Create(backend, archive, create.sources(),
                                                   create.password(), create.encryptHeaders(),
                                                   create.compressionOptions()),
                     S_OK);
        QVERIFY(accepted);
        QCOMPARE(passwordPrompts, 0);
        passwordAnswer.stop();
        QByteArray listing;
        QVERIFY(runEngine(fixture.path(), {"l", "-slt", "-pfixture123", "--", archive}, &listing));
        QVERIFY(listing.contains("Path = added.txt"));
        QCOMPARE(listing.count("Encrypted = +"), 2);
    }

    void addDialogUpdatesExistingEntryWithSelectedCompression_data() {
        QTest::addColumn<bool>("freshenUnchanged");
        QTest::newRow("update-newer") << false;
        QTest::newRow("freshen-older-keeps-entry-time") << true;
    }
    void addDialogUpdatesExistingEntryWithSelectedCompression() {
        QFETCH(bool, freshenUnchanged);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("source file.txt"));
        const QString archive = fixture.filePath("update.zip");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("old bytes"), qint64(9));
        QVERIFY(file.flush());
        const QDateTime entryTime = QDateTime::fromSecsSinceEpoch(946684800, QTimeZone(0));
        QVERIFY(file.setFileTime(entryTime, QFileDevice::FileModificationTime));
        file.close();
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        const QString unselected = fixture.filePath(QStringLiteral("не выбран.txt"));
        QFile untouched(unselected);
        QVERIFY(untouched.open(QIODevice::WriteOnly));
        QCOMPARE(untouched.write("archived only"), qint64(13));
        QVERIFY(untouched.flush());
        QVERIFY(untouched.setFileTime(entryTime.addDays(-1), QFileDevice::FileModificationTime));
        untouched.close();
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source, unselected}));
        QVERIFY(untouched.remove());
        QFile archiveFile(archive);
        QVERIFY(archiveFile.open(QIODevice::ReadWrite));
        QVERIFY(archiveFile.setFileTime(entryTime.addDays(1), QFileDevice::FileModificationTime));
        archiveFile.close();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("new bytes"), qint64(9));
        QVERIFY(file.flush());
        const QDateTime modified = entryTime.addSecs(freshenUnchanged ? -10 : 10);
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        NCompression::CInfo preferences;
        NCompression::CFormatOptions profile;
        profile.FormatID = "zip";
        profile.SetArcMTime.SetTrueTrue();
        preferences.Formats.Add(profile);
        preferences.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        MainWindow window(&backend);
        window.show();
        selectFilesystemSource(window, backend, source);
        CreateArchiveDialog create(fixture.path(), {source}, &window, backend.codecCatalog());
        bool dialogSeen = false;
        bool sourceSelected = false;
        bool dialogAccepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = &create;
            if (!dialog)
                return;
            dialogSeen = true;
            auto *mode = dialog->findChild<QComboBox *>("archiveUpdateMode");
            auto *level = dialog->findChild<QComboBox *>("compressionLevel");
            if (!mode || !level) {
                dialog->reject();
                return;
            }
            setOutput(*dialog, archive, "zip");
            sourceSelected = dialog->sources() == QStringList{source};
            mode->setCurrentIndex(
                mode->findData(int(freshenUnchanged ? NCompressDialog::NUpdateMode::kFresh
                                                    : NCompressDialog::NUpdateMode::kUpdate)));
            level->setCurrentIndex(level->findData(0));
            QVERIFY(dialog->compressionOptions().SetArcMTime.Val);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            dialogAccepted = dialog->result() == QDialog::Accepted;
        });
        create.exec();
        if (create.result() == QDialog::Accepted)
            QCOMPARE(SourceCreationFixture::Create(backend, create.target(), create.sources(),
                                                   create.password(), create.encryptHeaders(),
                                                   create.compressionOptions()),
                     S_OK);
        QVERIFY(dialogSeen);
        QVERIFY(sourceSelected);
        QVERIFY(dialogAccepted);
        QCOMPARE(QFileInfo(archive).lastModified(), freshenUnchanged ? entryTime : modified);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(QDir().mkpath(extracted));
        QVERIFY(runEngine(extracted, {"x", "-y", "--", archive}));
        QFile updated(QDir(extracted).filePath(QStringLiteral("source file.txt")));
        QVERIFY(updated.open(QIODevice::ReadOnly));
        QCOMPARE(updated.readAll(), QByteArray(freshenUnchanged ? "old bytes" : "new bytes"));
        QFile retained(QDir(extracted).filePath(QFileInfo(unselected).fileName()));
        QVERIFY(retained.open(QIODevice::ReadOnly));
        QCOMPARE(retained.readAll(), QByteArray("archived only"));
        QByteArray listing;
        QVERIFY(runEngine(fixture.path(), {"l", "-slt", "-sccUTF-8", "--", archive}, &listing));
        if (!freshenUnchanged)
            QVERIFY(listing.contains("Method = Store"));
    }

    void addDialogUsesSelectedPathMode_data() {
        QTest::addColumn<int>("pathMode");
        QTest::newRow("full") << int(NWildcard::k_FullPath);
        QTest::newRow("absolute") << int(NWildcard::k_AbsPath);
    }
    void addDialogUsesSelectedPathMode() {
        QFETCH(int, pathMode);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("nested/selected.txt");
        const QString archive = fixture.filePath("paths.zip");
        QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("full path"), qint64(9));
        file.close();
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        MainWindow window(&backend);
        window.show();
        selectFilesystemSource(window, backend, source);
        CreateArchiveDialog create(fixture.path(), {source}, &window, backend.codecCatalog());
        bool accepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = &create;
            if (!dialog)
                return;
            auto *pathModeControl = dialog->findChild<QComboBox *>("archivePathMode");
            if (!pathModeControl) {
                dialog->reject();
                return;
            }
            setOutput(*dialog, archive, "zip");
            pathModeControl->setCurrentIndex(pathModeControl->findData(pathMode));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            accepted = dialog->result() == QDialog::Accepted;
        });
        create.exec();
        if (create.result() == QDialog::Accepted)
            QCOMPARE(SourceCreationFixture::Create(backend, archive, create.sources(),
                                                   create.password(), create.encryptHeaders(),
                                                   create.compressionOptions()),
                     S_OK);
        QVERIFY(accepted);
        QByteArray listing;
        QVERIFY(runEngine(fixture.path(), {"l", "-slt", "-sccUTF-8", "--", archive}, &listing));
        const QString selectedPath = QFileInfo(source).canonicalFilePath();
        QString logical = selectedPath;
        if (pathMode == int(NWildcard::k_FullPath)) {
#ifdef _WIN32
            logical = selectedPath.mid(QDir::rootPath().length());
#else
            logical = selectedPath.mid(1);
#endif
        }
        QVERIFY(NormalizeTechnicalListing(listing).contains("Path = " + logical.toUtf8()));
    }
};

QTEST_MAIN(UiUpdateModesTests)
#include "ui_update_modes_tests.moc"
