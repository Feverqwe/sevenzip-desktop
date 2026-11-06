#include "archive/backend/archivebackend.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "support/archive_expert.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class CompressionMemoryTests final : public QObject {
    Q_OBJECT
  private slots:
    void choices_data() {
        QTest::addColumn<QString>("formatName");
        QTest::addColumn<QString>("methodName");
        QTest::newRow("PPMd-7z") << QString("7z") << QString("PPMd");
        QTest::newRow("BZip2-7z") << QString("7z") << QString("BZip2");
        QTest::newRow("BZip2-zip") << QString("zip") << QString("BZip2");
    }
    void choices() {
        QFETCH(QString, formatName);
        QFETCH(QString, methodName);
        QTemporaryDir dir;
        CreateArchiveDialog dialog(dir.path());
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText(formatName);
        dialog.findChild<QComboBox *>("compressionMethod")->setCurrentText(methodName);
        auto *dictionary = dialog.findChild<QComboBox *>("compressionDictionary");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        QVERIFY(dictionary->isEnabled());
        const bool bzip = methodName == "BZip2";
        QCOMPARE(dictionary->itemText(0),
                 bzip ? QStringLiteral("*  900 KB") : QStringLiteral("*  16 MB"));
        dictionary->setCurrentIndex(1);
        level->setCurrentIndex(level->findData(9u));
        method->setCurrentText(methodName);
        QCOMPARE(dictionary->itemText(0),
                 bzip ? QStringLiteral("*  900 KB") : QStringLiteral("*  256 MB"));
        if (bzip) {
            level->setCurrentIndex(level->findData(1u));
            method->setCurrentText(methodName);
            QCOMPARE(dictionary->itemText(0), QStringLiteral("*  100 KB"));
            level->setCurrentIndex(level->findData(3u));
            method->setCurrentText(methodName);
            QCOMPARE(dictionary->itemText(0), QStringLiteral("*  500 KB"));
        }
        QCOMPARE(dialog.compressionOptions().Dict64, UInt64(-1));
        dialog.show();
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            dictionary->setCurrentIndex(0);
            for (QSize size : {QSize(760, 720), QSize(640, 480)}) {
                editor.resize(size);
                editor.findChild<QScrollArea *>("dialogContentScroll")
                    ->ensureWidgetVisible(dictionary, 0, 80);
                QTRY_VERIFY(UiInspection::fullyVisible(*dictionary, editor));
                QVERIFY(UiInspection::writeReport(
                    editor,
                    QString("memory-%1-%2-%3").arg(methodName, formatName).arg(size.width())));
            }
        }));
        QCOMPARE(dialog.compressionOptions().Dict64, UInt64(-1));
        level->setCurrentIndex(level->findData(0u));
        QVERIFY(!dictionary->isEnabled());
        QCOMPARE(dialog.compressionOptions().Dict64, UInt64(-1));
    }
    void fixedDictionary_data() {
        QTest::addColumn<QString>("methodName");
        QTest::addColumn<QString>("sizeText");
        QTest::newRow("Deflate") << QString("Deflate") << QString("32 KB");
        QTest::newRow("Deflate64") << QString("Deflate64") << QString("64 KB");
    }
    void fixedDictionary() {
        QFETCH(QString, methodName);
        QFETCH(QString, sizeText);
        QTemporaryDir dir;
        CreateArchiveDialog dialog(dir.path());
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        dialog.findChild<QComboBox *>("compressionMethod")->setCurrentText(methodName);
        auto *dictionary = dialog.findChild<QComboBox *>("compressionDictionary");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        QVERIFY(!dictionary->isEnabled());
        QCOMPARE(dictionary->itemText(0), QStringLiteral("*  %1").arg(sizeText));
        for (int value : {1, 3, 5, 7, 9}) {
            level->setCurrentIndex(level->findData(unsigned(value)));
            method->setCurrentText(methodName);
            QCOMPARE(dictionary->itemText(0), QStringLiteral("*  %1").arg(sizeText));
            QCOMPARE(dialog.compressionOptions().Dict64, UInt64(-1));
        }
        dialog.show();
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            for (QSize size : {QSize(760, 720), QSize(640, 480)}) {
                editor.resize(size);
                editor.findChild<QScrollArea *>("dialogContentScroll")
                    ->ensureWidgetVisible(dictionary, 0, 80);
                QTRY_VERIFY(UiInspection::fullyVisible(*dictionary, editor));
                QVERIFY(UiInspection::writeReport(
                    editor, QString("memory-%1-zip-%2").arg(methodName).arg(size.width())));
            }
        }));
    }
    void operations_data() {
        QTest::addColumn<QString>("formatName");
        QTest::addColumn<QString>("methodName");
        QTest::addColumn<quint64>("validSize");
        QTest::newRow("PPMd-7z") << QString("7z") << QString("PPMd") << quint64(1 << 20);
        QTest::newRow("BZip2-7z-small") << QString("7z") << QString("BZip2") << quint64(100 << 10);
        QTest::newRow("BZip2-zip-large")
            << QString("zip") << QString("BZip2") << quint64(900 << 10);
        QTest::newRow("Deflate") << QString("zip") << QString("Deflate") << quint64(0);
        QTest::newRow("Deflate64") << QString("zip") << QString("Deflate64") << quint64(0);
    }
    void operations() {
        QFETCH(QString, formatName);
        QFETCH(QString, methodName);
        QFETCH(quint64, validSize);
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        const QByteArray contents(8192, 'x');
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();
        CompressionOptions options;
        options.Method = methodName.toStdWString().c_str();
        options.Dict64 = validSize ? validSize : UInt64(-1);
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const QString target = dir.filePath("memory." + formatName);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source}, {}, false, options),
                 S_OK);
        QProcess process;
        process.start("7z", {"t", "--", target});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 0);
        const QString extracted = dir.filePath("unpacked");
        process.start("7z", {"x", "-o" + extracted, "--", target});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 0);
        QFile restored(QDir(extracted).filePath(QFileInfo(source).fileName()));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), contents);
        const QString invalid = dir.filePath("invalid." + formatName);
        options.Dict64 = UInt64(1) << 63;
        QVERIFY(SourceCreationFixture::Create(backend, invalid, {source}, {}, false, options) !=
                S_OK);
        QVERIFY(!QFileInfo::exists(invalid));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(8 << 20));
        file.close();
        options.Dict64 = validSize ? validSize : UInt64(-1);
        QCOMPARE(
            SourceCreationFixture::Create(backend, invalid, {source}, {}, false, options, true),
            E_ABORT);
        QVERIFY(!QFileInfo::exists(invalid));
        QVERIFY(QFileInfo::exists(target));
        QVERIFY(QDir(dir.path()).entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
};
QTEST_MAIN(CompressionMemoryTests)
#include "compression_memory_tests.moc"
