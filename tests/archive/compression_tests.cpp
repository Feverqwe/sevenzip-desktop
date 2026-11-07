#include "archive/backend/archivebackend.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/CompressDialog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/UpdateGUI.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

class CompressionTests : public QObject {
    Q_OBJECT

  private slots:
    void invalidNativeInfoIsRejected_data() {
        QTest::addColumn<QString>("field");
        for (const char *field : {"level", "method", "threads", "dictionary", "order"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void invalidNativeInfoIsRejected() {
        QFETCH(QString, field);
        CompressionOptions options;
        options.Level = 5;
        options.Method = L"LZMA2";
        if (field == "level")
            options.Level = 10;
        else if (field == "method")
            options.Method = L"invalid";
        else if (field == "threads")
            options.NumThreads = 999999;
        else if (field == "dictionary")
            options.Dict64 = UInt64(1) << 63;
        else
            options.Order = 1;
        CObjectVector<CProperty> properties;
        QString error;
        QVERIFY(
            !ArchiveBackend::compressionProperties("7z", {}, false, options, &properties, &error));
        QVERIFY(!error.isEmpty());
    }
    void formatOptionsAndPasswordConfirmation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("файл с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("content");
        file.close();

        CreateArchiveDialog dialog(temp.path(), {source});
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        auto *threads = dialog.findChild<QComboBox *>("compressionThreads");
        auto *dictionary = dialog.findChild<QComboBox *>("compressionDictionary");
        auto *wordSize = dialog.findChild<QComboBox *>("compressionWordSize");
        auto *solidBlock = dialog.findChild<QComboBox *>("compressionSolidBlock");
        auto *encryption = dialog.findChild<QComboBox *>("archiveEncryption");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *confirmation = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        auto *showPassword = dialog.findChild<QCheckBox *>("archiveShowPassword");
        QVERIFY(format && level && method && threads && dictionary && wordSize && solidBlock &&
                encryption && password && confirmation && showPassword);
        QCOMPARE(method->currentText(), QStringLiteral("*  LZMA2"));
        QVERIFY(dictionary->isEnabled());
        QVERIFY(wordSize->isEnabled());
        QVERIFY(solidBlock->isEnabled());
        level->setCurrentIndex(level->findData(9u));
        dictionary->setCurrentIndex(dictionary->findData(QVariant::fromValue(quint64(4 << 20))));
        wordSize->setCurrentIndex(wordSize->findData(64));
        solidBlock->setCurrentIndex(solidBlock->findData(24u));
        QCOMPARE(dialog.compressionOptions().Dict64, quint64(4 << 20));
        QCOMPARE(dialog.compressionOptions().Order, 64);
        QCOMPARE(dialog.compressionOptions().SolidBlockSize, quint64(16 << 20));
        level->setCurrentIndex(0);
        QVERIFY(!dictionary->isEnabled());
        QVERIFY(!wordSize->isEnabled());
        QVERIFY(!solidBlock->isEnabled());
        QCOMPARE(dialog.compressionOptions().Dict64, UInt64(-1));
        QVERIFY(!dialog.compressionOptions().SolidIsSpecified);
        level->setCurrentIndex(level->findData(9u));
        method->setCurrentText("BZip2");
        QVERIFY(dictionary->isEnabled());
        QVERIFY(!wordSize->isEnabled());
        solidBlock->setCurrentIndex(solidBlock->findData(0u));
        threads->setCurrentIndex(threads->findData(2u));
        const CompressionOptions options = dialog.compressionOptions();
        QCOMPARE(options.Level, 9);
        QCOMPARE(QString::fromWCharArray(options.Method.Ptr()), QStringLiteral("BZip2"));
        QCOMPARE(options.NumThreads, 2);
        QVERIFY(options.SolidIsSpecified);
        QCOMPARE(options.Dict64, UInt64(-1));
        QCOMPARE(options.Order, UInt32(-1));
        QCOMPARE(options.SolidBlockSize, quint64(0));

        format->setCurrentText("zip");
        QVERIFY(method->findText("PPMd") < 0);
        QCOMPARE(method->currentText(), QStringLiteral("*  Deflate"));
        QVERIFY(!solidBlock->isEnabled());
        auto *passwordEnabled = dialog.findChild<QCheckBox *>("archivePasswordEnabled");
        QVERIFY(passwordEnabled);
        passwordEnabled->click();
        encryption->setCurrentIndex(1);
        QVERIFY(password->isEnabled());
        QVERIFY(confirmation->isEnabled());
        QCOMPARE(confirmation->echoMode(), QLineEdit::Password);
        showPassword->setChecked(true);
        QCOMPARE(password->echoMode(), QLineEdit::Normal);
        QCOMPARE(confirmation->echoMode(), QLineEdit::Normal);
    }

    void compactDialogKeepsConfirmationAndOptionsAccessible() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CreateArchiveDialog dialog(temp.path());
        auto *passwordEnabled = dialog.findChild<QCheckBox *>("archivePasswordEnabled");
        QVERIFY(passwordEnabled);
        ExpandArchiveSections(dialog);
        passwordEnabled->click();
        dialog.resize(640, 480);
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(dialog.height() <= 480);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        const QRect buttonRect(buttons->mapTo(&dialog, QPoint()), buttons->size());
        QVERIFY(dialog.rect().contains(buttonRect));
        auto *scroll = dialog.findChild<QScrollArea *>();
        auto *password = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        QVERIFY(scroll && password);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        // ensureWidgetVisible(QLineEdit) follows its input cursor rectangle,
        // which can already be visible while the field's border is clipped.
        // Exercise scrolling to the complete field, including that border.
        const QPoint bottom = password->mapTo(scroll->widget(), password->rect().bottomRight());
        scroll->ensureVisible(bottom.x(), bottom.y());
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "encryption-compact-confirmation"));
        const QRect passwordRect(password->mapTo(scroll->viewport(), QPoint()), password->size());
        QVERIFY(scroll->viewport()->rect().contains(passwordRect));
    }

    void updateDialogKeepsModeAndCompressionAccessible() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CreateArchiveDialog dialog(temp.path(), {source});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        auto *mode = dialog.findChild<QComboBox *>("archiveUpdateMode");
        auto *pathMode = dialog.findChild<QComboBox *>("archivePathMode");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *scroll = dialog.findChild<QScrollArea *>();
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(mode && pathMode && level && scroll && buttons);
        QCOMPARE(mode->findData(int(NCompressDialog::NUpdateMode::kSync)), -1);
        ExpandArchiveSections(dialog);
        QCOMPARE(pathMode->currentData().toInt(), int(NWildcard::k_RelatPath));
        QCOMPARE(dialog.compressionOptions().UpdateMode, NCompressDialog::NUpdateMode::kAdd);
        for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
            dialog.resize(size);
            dialog.show();
            QCoreApplication::processEvents();
            QVERIFY(
                dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint()), buttons->size())));
            for (QWidget *control : {static_cast<QWidget *>(mode), static_cast<QWidget *>(pathMode),
                                     static_cast<QWidget *>(level)}) {
                scroll->ensureWidgetVisible(control);
                QCoreApplication::processEvents();
                QVERIFY(scroll->viewport()->rect().contains(
                    QRect(control->mapTo(scroll->viewport(), QPoint()), control->size())));
            }
            QVERIFY(UiInspection::writeReport(
                dialog, size.width() == 760 ? "update-options-normal" : "update-options-compact"));
        }
    }

    void cancelledUpdateDoesNotSaveCompressionProfile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settingsPath.isEmpty());
        const QByteArray previous = QFile(settingsPath).exists() ? [&] {
            QFile settings(settingsPath);
            if (!settings.open(QIODevice::ReadOnly))
                return QByteArray();
            return settings.readAll();
        }()
                                                                 : QByteArray();
        const bool existed = QFile(settingsPath).exists();
        CreateArchiveDialog dialog(temp.path(), {source});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        QVERIFY(level);
        level->setCurrentIndex(level->findData(0u));
        dialog.reject();
        QCOMPARE(QFile(settingsPath).exists(), existed);
        if (existed) {
            QFile settings(settingsPath);
            QVERIFY(settings.open(QIODevice::ReadOnly));
            QCOMPARE(settings.readAll(), previous);
        }
    }

    void mismatchedPasswordBlocksSubmission() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CreateArchiveDialog dialog(temp.path(), {source});
        dialog.findChild<QComboBox *>("archiveEncryption")->setCurrentIndex(0);
        dialog.findChild<QLineEdit *>("archivePassword")->setText("first");
        auto *confirmation = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        confirmation->setText("second");
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QTimer::singleShot(0, &dialog, [&dialog] {
            QTimer::singleShot(0, &dialog, [] {
                for (QWidget *widget : QApplication::topLevelWidgets()) {
                    if (auto *warning = qobject_cast<QMessageBox *>(widget))
                        warning->accept();
                }
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QTimer::singleShot(0, &dialog, &QDialog::reject);
        });
        QCOMPARE(dialog.exec(), QDialog::Rejected);
        QVERIFY(dialog.target().isEmpty());

        confirmation->setText("first");
        QTimer::singleShot(0, buttons->button(QDialogButtonBox::Ok), &QAbstractButton::click);
        QCOMPARE(dialog.exec(), QDialog::Accepted);
        QCOMPARE(dialog.password(), QStringLiteral("first"));
    }

    void createWithCompressionOptions_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("method");
        QTest::addColumn<int>("level");
        QTest::newRow("7z-solid-off") << "7z" << "LZMA2" << 9;
        QTest::newRow("7z-level-two") << "7z" << "LZMA2" << 2;
        QTest::newRow("7z-PPMd-order") << "7z" << "PPMd" << 7;
        QTest::newRow("zip-bzip2") << "zip" << "BZip2" << 9;
    }

    void createWithCompressionOptions() {
        QFETCH(QString, format);
        QFETCH(QString, method);
        QFETCH(int, level);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes(8192, 'a');
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();

        const QString target = temp.filePath("archive." + format);
        CompressionOptions options;
        options.Level = level;
        options.Method = method.toStdWString().c_str();
        options.NumThreads = method == "PPMd" && format == "7z" ? UInt32(-1) : 2;
        options.Order = method == "LZMA2" || method == "PPMd" ? 8 : UInt32(-1);
        // Exercise level/method/solid transport with a bounded dictionary.
        // Level 9's default needs >2 GiB even for this 8 KiB fixture and can
        // correctly fail the source memory gate in the shared runner guest.
        options.Dict64 = method == "LZMA2" || method == "PPMd" ? UInt64(1 << 20) : UInt64(-1);
        options.SolidIsSpecified = format == "7z";
        options.SolidBlockSize = 0;
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source}, {}, false, options),
                 S_OK);
        QVERIFY(QFile::exists(target));

        QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
        backend.list(target);
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 15000);
        const QList<ArchiveEntry> entries =
            qvariant_cast<QList<ArchiveEntry>>(listed.takeFirst().at(0));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().path, QStringLiteral("данные с пробелом.txt"));

        QProcess inspect;
        inspect.start("7z", {"l", "-slt", "-sccUTF-8", "--", target});
        QVERIFY(inspect.waitForFinished(15000));
        QCOMPARE(inspect.exitCode(), 0);
        const QString listing = QString::fromUtf8(inspect.readAllStandardOutput());
        QVERIFY2(listing.contains("Method = " + method, Qt::CaseInsensitive), qPrintable(listing));
        const QString output = temp.filePath("extracted");
        inspect.start("7z", {"x", "-y", "-o" + output, "--", target});
        QVERIFY(inspect.waitForFinished(15000));
        QCOMPARE(inspect.exitCode(), 0);
        QFile extracted(QDir(output).filePath(QFileInfo(source).fileName()));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), bytes);
    }

    void createsArchiveWithDictionaryWordAndSolidBlock() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(8192, 'a'));
        file.close();

        CompressionOptions options;
        options.Method = "LZMA2";
        options.Dict64 = 1 << 20;
        options.Order = 64;
        options.SolidIsSpecified = true;
        options.SolidBlockSize = 16 << 20;
        const QString target = temp.filePath("archive.7z");
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source}, {}, false, options),
                 S_OK);

        QProcess inspect;
        inspect.start("7z", {"l", "-slt", "-sccUTF-8", "--", target});
        QVERIFY(inspect.waitForFinished(15000));
        QCOMPARE(inspect.exitCode(), 0);
        const QByteArray listing = inspect.readAllStandardOutput();
        QVERIFY(listing.contains("Method = LZMA2"));
        QVERIFY(listing.contains("Path = " + QFileInfo(source).fileName().toUtf8()));
    }

    void rejectsIncompatibleDictionaryBeforeCreatingTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CompressionOptions options;
        options.Method = "BZip2";
        options.Dict64 = 4 << 20;
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const QString target = temp.filePath("invalid.7z");
        QVERIFY(SourceCreationFixture::Create(backend, target, {source}, {}, false, options) !=
                S_OK);
        QVERIFY(!QFile::exists(target));
    }

    void rejectsInvalidOptionsWithoutCreatingTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("file.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CompressionOptions options;
        options.Method = "invalid";
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const QString target = temp.filePath("invalid.7z");
        QVERIFY(SourceCreationFixture::Create(backend, target, {source}, {}, false, options) !=
                S_OK);
        QVERIFY(!QFile::exists(target));
    }
};

QTEST_MAIN(CompressionTests)
#include "compression_tests.moc"
