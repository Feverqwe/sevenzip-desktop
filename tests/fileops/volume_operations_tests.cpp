#include "fileops/volumes/nativevolumeworker.h"
#include "fileops/volumes/volumeoperations.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class VolumeOperationsTests : public QObject {
    Q_OBJECT
    QTemporaryDir languageDirectory;
#ifdef Q_OS_WIN
    static DWORD nativeWriteError(const QString &path) {
        NWindows::NFile::NIO::COutFile file;
        return file.Open_EXISTING(us2fs(ToUString(path))) ? ERROR_SUCCESS : ::GetLastError();
    }
#endif
    void language(bool russian) {
        const QString path = languageDirectory.filePath("language.ini");
        SaveRegLang(UString(russian ? "ru" : "-"), path);
        ReloadLang(path);
    }

  private slots:
    void init() { language(false); }
    void cleanup() { language(false); }
    void sourceSequenceDigits_data() {
        QTest::addColumn<quint64>("count");
        QTest::addColumn<int>("digits");
        QTest::newRow("one") << quint64(1) << 3;
        QTest::newRow("999") << quint64(999) << 3;
        QTest::newRow("1000") << quint64(1000) << 4;
        QTest::newRow("9999") << quint64(9999) << 4;
        QTest::newRow("10000") << quint64(10000) << 5;
        QTest::newRow("uint64-max") << std::numeric_limits<quint64>::max() << 20;
    }
    void sourceSequenceDigits() {
        QFETCH(quint64, count);
        QFETCH(int, digits);
        CVolSeqName sequence;
        sequence.UnchangedPart = "part.";
        sequence.SetNumDigits(count);
        QCOMPARE(ToQString(sequence.GetNextName()),
                 "part." + QString("1").rightJustified(digits, '0'));
    }
    void sourceSequenceCarries() {
        CVolSeqName sequence;
        QVERIFY(sequence.ParseName(UString("part.01")));
        for (unsigned next = 2; next <= 101; ++next)
            QCOMPARE(ToQString(sequence.GetNextName()),
                     "part." + QString::number(next).rightJustified(2, '0'));
    }
    void combinesSourceContiguousPrefix_data() {
        QTest::addColumn<int>("emptyPart");
        QTest::newRow("empty-first") << 1;
        QTest::newRow("empty-middle") << 2;
        QTest::newRow("empty-last") << 3;
    }
    void combinesSourceContiguousPrefix() {
        QFETCH(int, emptyPart);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString base = fixture.filePath(QStringLiteral("части space.bin"));
        QStringList expectedPaths;
        QByteArray expected;
        for (int part : {1, 2, 3, 5}) {
            const QString path = base + QString(".%1").arg(part, 3, 10, QLatin1Char('0'));
            const QByteArray bytes = part == emptyPart ? QByteArray() : QByteArray::number(part);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
            file.close();
            if (part < 4) {
                expectedPaths << path;
                expected += bytes;
            }
        }
        QString error;
        const auto parts = VolumeOperation::inspectParts(base + ".001", &error);
        QCOMPARE(parts.paths, expectedPaths);
        QCOMPARE(parts.totalSize, expected.size());
        const QString output = fixture.filePath("combined.bin");
        VolumeOperation operation(VolumeOperation::Mode::Combine, base + ".001", output);
        QSignalSpy results(&operation, &VolumeOperation::result);
        operation.start();
        QVERIFY(operation.wait());
        QCOMPARE(results.size(), 1);
        QVERIFY2(results.first().first().toBool(), qPrintable(results.first()[1].toString()));
        QFile combined(output);
        QVERIFY(combined.open(QIODevice::ReadOnly));
        QCOMPARE(combined.readAll(), expected);
        QFile later(base + ".005");
        QVERIFY(later.open(QIODevice::ReadOnly));
        QCOMPARE(later.readAll(), QByteArray("5"));
    }

    void parsesSizes() {
        QCOMPARE(VolumeOperation::parseSize("10M"), qint64(10) << 20);
        QCOMPARE(VolumeOperation::parseSize("650M - CD"), qint64(650) << 20);
        QCOMPARE(VolumeOperation::parseSize("23040M - BD"), qint64(23040) << 20);
        QCOMPARE(VolumeOperation::parseSize("1B"), qint64(1));
        QCOMPARE(VolumeOperation::parseSize("10M 20M"), qint64(0));
        QCOMPARE(VolumeOperation::parseSize("18446744073709551616"), qint64(0));
        QCOMPARE(VolumeOperation::parseSize("9223372036854775808"), qint64(0));
        QCOMPARE(VolumeOperation::parseSize(" 2 k "), qint64(2048));
        QCOMPARE(VolumeOperation::parseSize("0M"), qint64(0));
        QCOMPARE(VolumeOperation::parseSize("999999999999T"), qint64(0));
    }
    void parsesSizeSequences() {
        QCOMPARE(VolumeOperation::parseSizes(" 10M 20M "),
                 (QList<qint64>{qint64(10) << 20, qint64(20) << 20}));
        QCOMPARE(VolumeOperation::parseSizes("10 20 30B"), (QList<qint64>{10, 20, 30}));
        QVERIFY(VolumeOperation::parseSizes("10M 0M").isEmpty());
        QVERIFY(VolumeOperation::parseSizes("10M 9223372036854775808").isEmpty());
        QVERIFY(VolumeOperation::parseSizes(" ").isEmpty());
        QVERIFY(VolumeOperation::parseSizes("wrong").isEmpty());
    }
    void splitsWithSizeSequence_data() {
        QTest::addColumn<QList<qint64>>("sizes");
        QTest::addColumn<QList<qint64>>("expected");
        QTest::newRow("repeat-last") << QList<qint64>{10, 20} << QList<qint64>{10, 20, 20, 20, 5};
        QTest::newRow("three-sizes") << QList<qint64>{10, 20, 30} << QList<qint64>{10, 20, 30, 15};
        QTest::newRow("unused-tail") << QList<qint64>{10, 100, 30} << QList<qint64>{10, 65};
    }
    void splitsWithSizeSequence() {
        QFETCH(QList<qint64>, sizes);
        QFETCH(QList<qint64>, expected);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath("последовательность space.bin");
        QByteArray bytes;
        for (int i = 0; i < 75; ++i)
            bytes.append(char(i));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(75));
        file.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, dir.path(), sizes);
        QSignalSpy spy(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QCOMPARE(spy.size(), 1);
        QVERIFY(spy.first().first().toBool());
        QByteArray restored;
        for (qsizetype i = 0; i < expected.size(); ++i) {
            QFile part(source + QStringLiteral(".%1").arg(i + 1, 3, 10, QLatin1Char('0')));
            QVERIFY(part.open(QIODevice::ReadOnly));
            QCOMPARE(part.size(), expected[i]);
            restored += part.readAll();
        }
        QCOMPARE(restored, bytes);
        QVERIFY(!QFileInfo::exists(
            source + QStringLiteral(".%1").arg(expected.size() + 1, 3, 10, QLatin1Char('0'))));
        const QString invalidDestination = dir.filePath("invalid");
        QVERIFY(QDir().mkpath(invalidDestination));
        VolumeOperation invalid(VolumeOperation::Mode::Split, source, invalidDestination,
                                QList<qint64>{10, -1});
        QSignalSpy failed(&invalid, &VolumeOperation::result);
        invalid.start();
        QVERIFY(invalid.wait());
        QVERIFY(!failed.first().first().toBool());
        QVERIFY(QDir(invalidDestination).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void splitsAndCombinesUnicodeFile_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("English") << false;
        QTest::newRow("Russian") << true;
    }
    void splitsAndCombinesUnicodeFile() {
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath("архив space.bin");
        const QByteArray bytes = QByteArray("тест") + QByteArray(250000, 'x');
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, dir.path(), 100000);
        QSignalSpy splitResult(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QCOMPARE(splitResult.size(), 1);
        QVERIFY(splitResult.first().first().toBool());
        QCOMPARE(splitResult.first()[1].toString(),
                 russian ? QStringLiteral("Файл разделён.") : QString("File split."));
        const QString first = source + ".001";
        QString error;
        QCOMPARE(VolumeOperation::discoverParts(first, &error).size(), 3);
        const QString output = dir.filePath("restored.bin");
        VolumeOperation combine(VolumeOperation::Mode::Combine, first, output);
        QSignalSpy combineResult(&combine, &VolumeOperation::result);
        combine.start();
        QVERIFY(combine.wait());
        QVERIFY(combineResult.first().first().toBool());
        QCOMPARE(combineResult.first()[1].toString(),
                 russian ? QStringLiteral("Файлы объединены.") : QString("Files combined."));
        QFile result(output);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), bytes);
    }
    void rejectsCollisionAndMissingPart_data() { splitsAndCombinesUnicodeFile_data(); }
    void rejectsCollisionAndMissingPart() {
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath("source.bin");
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        sourceFile.write(QByteArray(30, 'a'));
        sourceFile.close();
        QFile collision(source + ".001");
        QVERIFY(collision.open(QIODevice::WriteOnly));
        collision.write("existing");
        collision.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, dir.path(), 10);
        QSignalSpy spy(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QVERIFY(!spy.first().first().toBool());
        QVERIFY(spy.first()[1].toString().contains(source + ".001"));
        QVERIFY(collision.open(QIODevice::ReadOnly));
        QCOMPARE(collision.readAll(), QByteArray("existing"));
        collision.close();
        QVERIFY(!QFileInfo::exists(source + ".002"));
        QFile second(source + ".002");
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("second");
        second.close();
        QFile third(source + ".004");
        QVERIFY(third.open(QIODevice::WriteOnly));
        third.write("third");
        third.close();
        QString error;
        QCOMPARE(VolumeOperation::discoverParts(source + ".001", &error),
                 (QStringList{source + ".001", source + ".002"}));
    }
    void lateSplitCollisionRollsBackPublishedPrefix() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("части 空 with space.bin"));
        const QByteArray bytes(3 << 20, 'x');
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(bytes), qint64(bytes.size()));
        input.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, fixture.path(), 1 << 20);
        bool injected = false;
        connect(
            &split, &VolumeOperation::advanced, &split,
            [&](qint64 completed) {
                if (injected || completed != bytes.size())
                    return;
                QFile occupied(source + ".002");
                injected = occupied.open(QIODevice::WriteOnly) && occupied.write("other") == 5;
            },
            Qt::DirectConnection);
        QSignalSpy results(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QVERIFY(injected);
        QCOMPARE(results.size(), 1);
        QVERIFY(!results.first().first().toBool());
        QVERIFY(!split.isCanceled());
        QFile occupied(source + ".002");
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("other"));
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), bytes);
        QCOMPARE(
            QDir(fixture.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
            (QStringList{QFileInfo(source).fileName(), QFileInfo(source).fileName() + ".002"}));
    }

    void cancellationRemovesPartialOutput_data() { splitsAndCombinesUnicodeFile_data(); }
    void cancellationRemovesPartialOutput() {
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath("large.bin");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(8 << 20));
        file.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, dir.path(),
                              QList<qint64>{1 << 20, 2 << 20});
        connect(
            &split, &VolumeOperation::advanced, &split,
            [&split](qint64 bytes) {
                if (bytes >= (1 << 20))
                    split.cancel();
            },
            Qt::DirectConnection);
        QSignalSpy spy(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QVERIFY(!spy.first().first().toBool());
        QVERIFY(split.isCanceled());
        QCOMPARE(spy.first()[1].toString(),
                 russian ? QStringLiteral("Операция отменена.") : QString("Operation canceled."));
        QVERIFY(!QFileInfo::exists(source + ".001"));
        QCOMPARE(QDir(dir.path()).entryList({"*.sevenzip-*"}, QDir::Files).size(), 0);
    }
    void combineRollback_data() {
        QTest::addColumn<int>("failure");
        QTest::addColumn<bool>("russian");
        const char *names[] = {"cancel", "remove-next-part", "grow-current-part", "occupied-output",
                               "grow-completed-part"};
        for (bool russian : {false, true})
            for (int failure = 0; failure < 5; ++failure)
                QTest::newRow(
                    qPrintable(QString::fromLatin1(names[failure]) + (russian ? "-ru" : "-en")))
                    << failure << russian;
    }
    void combineRollback() {
        QFETCH(int, failure);
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("части с пробелом.bin"));
        for (const QString &suffix : {".001", ".002"}) {
            QFile part(base + suffix);
            QVERIFY(part.open(QIODevice::WriteOnly));
            QCOMPARE(part.write(QByteArray(2 << 20, 'x')), qint64(2 << 20));
        }
        if (failure == 3) {
            QFile occupied(base);
            QVERIFY(occupied.open(QIODevice::WriteOnly));
            QCOMPARE(occupied.write("existing"), qint64(8));
        }
        VolumeOperation combine(VolumeOperation::Mode::Combine, base + ".001", base);
        bool triggered = false;
        bool changed = false;
#ifdef Q_OS_WIN
        DWORD writeError = ERROR_SUCCESS;
#endif
        connect(
            &combine, &VolumeOperation::advanced, &combine,
            [&](qint64 bytes) {
                if (bytes == 0 || triggered || (failure == 4 && bytes < (3 << 20)))
                    return;
                triggered = true;
                if (failure == 0)
                    combine.cancel();
                else if (failure == 1)
                    changed = QFile::remove(base + ".002");
                else if (failure == 2 || failure == 4) {
#ifdef Q_OS_WIN
                    if (failure == 2)
                        writeError = nativeWriteError(base + ".001");
                    else
#endif
                    {
                        QFile current(base + ".001");
                        changed = current.open(QIODevice::Append) && current.write("extra") == 5;
                    }
                }
            },
            Qt::DirectConnection);
        QSignalSpy result(&combine, &VolumeOperation::result);
        combine.start();
        QVERIFY(combine.wait());
        QCOMPARE(result.size(), 1);
#ifdef Q_OS_WIN
        if (failure == 2) {
            QVERIFY(triggered);
            QCOMPARE(writeError, DWORD(ERROR_SHARING_VIOLATION));
            QVERIFY(result.first().first().toBool());
            QFile output(base);
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll(), QByteArray(4 << 20, 'x'));
            QCOMPARE(QFileInfo(base + ".001").size(), qint64(2 << 20));
            QVERIFY(
                QDir(dir.path()).entryList({"*.sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
            return;
        }
#endif
        QVERIFY(!result.first().first().toBool());
        if (failure == 1 || failure == 2 || failure == 4)
            QVERIFY(changed);
        if (failure == 0)
            QCOMPARE(result.first()[1].toString(), russian ? QStringLiteral("Операция отменена.")
                                                           : QString("Operation canceled."));
        QCOMPARE(combine.isCanceled(), failure == 0);
        if (failure == 1)
            QCOMPARE(result.first()[1].toString(),
                     (russian ? QStringLiteral("Часть файла недоступна: %1")
                              : QString("File part is unavailable: %1"))
                         .arg(base + ".002"));
        if (failure == 2 || failure == 4)
            QCOMPARE(result.first()[1].toString(),
                     (russian ? QStringLiteral("Часть файла изменилась: %1")
                              : QString("File part changed: %1"))
                         .arg(base + ".001"));
        if (failure == 3) {
            QFile occupied(base);
            QVERIFY(occupied.open(QIODevice::ReadOnly));
            QCOMPARE(occupied.readAll(), QByteArray("existing"));
        } else
            QVERIFY(!QFileInfo::exists(base));
        QVERIFY(QDir(dir.path()).entryList({"*.sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void splitRejectsSourceChange_data() {
        QTest::addColumn<bool>("grow");
        QTest::addColumn<bool>("russian");
        for (bool russian : {false, true}) {
            QTest::newRow(russian ? "shrink-ru" : "shrink-en") << false << russian;
            QTest::newRow(russian ? "grow-ru" : "grow-en") << true << russian;
        }
    }
    void splitRejectsSourceChange() {
        QFETCH(bool, grow);
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("изменённый источник.bin"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(3 << 20, 'x')), qint64(3 << 20));
        file.close();
        VolumeOperation split(VolumeOperation::Mode::Split, source, dir.path(), 1 << 20);
        bool changed = false;
        bool attempted = false;
#ifdef Q_OS_WIN
        DWORD writeError = ERROR_SUCCESS;
#endif
        connect(
            &split, &VolumeOperation::advanced, &split,
            [&](qint64 bytes) {
                if (bytes != (1 << 20))
                    return;
                attempted = true;
#ifdef Q_OS_WIN
                writeError = nativeWriteError(source);
#else
                QFile input(source);
                changed =
                    input.open(QIODevice::ReadWrite) && input.resize(grow ? (4 << 20) : (1 << 20));
#endif
            },
            Qt::DirectConnection);
        QSignalSpy result(&split, &VolumeOperation::result);
        split.start();
        QVERIFY(split.wait());
        QVERIFY(attempted);
#ifdef Q_OS_WIN
        QCOMPARE(writeError, DWORD(ERROR_SHARING_VIOLATION));
        QCOMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QByteArray restored;
        for (const QString &path : {source, source + ".001", source + ".002", source + ".003"}) {
            QFile output(path);
            QVERIFY(output.open(QIODevice::ReadOnly));
            if (path == source)
                QCOMPARE(output.readAll(), QByteArray(3 << 20, 'x'));
            else
                restored += output.readAll();
        }
        QCOMPARE(restored, QByteArray(3 << 20, 'x'));
        QVERIFY(QDir(dir.path()).entryList({"*.sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
#else
        QVERIFY(changed);
        QCOMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(result.first()[1].toString(),
                 russian ? QStringLiteral("Исходный файл изменился во время разделения.")
                         : QString("Source file changed while splitting."));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden),
                 QStringList{QFileInfo(source).fileName()});
#endif
    }

    void combineSummary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("части с пробелами.bin"));
        for (int i = 1; i <= 5; ++i) {
            QFile file(base + QStringLiteral(".%1").arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(i, 'x')), qint64(i));
        }
        QString error;
        const auto parts = VolumeOperation::inspectParts(base + ".001", &error);
        QCOMPARE(parts.paths.size(), 5);
        QCOMPARE(parts.totalSize, qint64(15));
        CCombineVolumePlan combiner;
        combiner.InputDirPrefix = us2fs(ToUString(dir.path() + '/'));
        combiner.TotalSize = quint64(parts.totalSize);
        for (const QString &part : parts.paths)
            combiner.Names.Add(us2fs(ToUString(QFileInfo(part).fileName())));
        const QString info = ToQString(combiner.GetInfo());
        QVERIFY(info.contains("Files: 5"));
        QVERIFY(info.contains("15 bytes"));
        QVERIFY(info.contains("части с пробелами.bin.001"));
        QVERIFY(info.contains("части с пробелами.bin.002"));
        QVERIFY(info.contains("части с пробелами.bin.005"));
        QVERIFY(!info.contains("части с пробелами.bin.003"));
        QVERIFY(info.contains("..."));
        QVERIFY(!QFileInfo::exists(base));
        QVERIFY(QFile::remove(base + ".003"));
        const auto prefix = VolumeOperation::inspectParts(base + ".001", &error);
        QCOMPARE(prefix.paths, (QStringList{base + ".001", base + ".002"}));
        QCOMPARE(prefix.totalSize, qint64(3));
    }
};

QTEST_MAIN(VolumeOperationsTests)
#include "volume_operations_tests.moc"
