#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/sfx/archivesfxmodule.h"
#include "archive/support/source_creation_fixture.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
class SfxCreationTests final : public QObject {
    Q_OBJECT
    QString engine_, module_, sfx_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#if !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Official native SFX fixture unavailable; platform SFX consumer unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        sfx_ = QString::fromUtf8(SEVENZIP_SFX_FIXTURE);
        if (!QFileInfo::exists(sfx_))
            QSKIP("Run scripts/build-sfx.sh to provide the official native SFX fixture");
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        version_ = backend.codecCatalog()->version();
        const auto sfx = backend.sfxModule(sfx_);
        QVERIFY2(sfx.available(), qPrintable(sfx.error()));
#endif
    }
    void provenanceRejectsChanges() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("copied module.sfx");
        const auto bytes = read(sfx_);
        QVERIFY(write(path, bytes));
        QVERIFY(write(path + ".json", read(sfx_ + ".json")));
        QVERIFY(ArchiveSfxModule::Read(path, version_).available());
        QVERIFY(!ArchiveSfxModule::Read(path, version_ + 1).available());
        QJsonObject metadata = QJsonDocument::fromJson(read(path + ".json")).object();
        metadata["system"] = "other platform";
        QVERIFY(write(path + ".json", QJsonDocument(metadata).toJson()));
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
        QVERIFY(write(path + ".json", read(sfx_ + ".json")));
        QByteArray changed = bytes;
        changed[changed.size() / 2] = char(changed[changed.size() / 2] ^ 1);
        QVERIFY(write(path, changed));
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
        QFile::remove(path + ".json");
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
    }
    void matchingMetadataDoesNotAcceptInvalidExecutable() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("invalid native module.sfx");
        const QByteArray bytes("This is not an executable module.");
        QVERIFY(write(path, bytes));
        QJsonObject metadata = QJsonDocument::fromJson(read(sfx_ + ".json")).object();
        metadata["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        QVERIFY(write(path + ".json", QJsonDocument(metadata).toJson()));
        QVERIFY(ArchiveSfxModule::Read(path, version_).available());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QVERIFY(!backend.sfxModule(path).available());
        const QString source = fixture.filePath(QStringLiteral("данные with spaces.txt"));
        QVERIFY(write(source, "source bytes"));
        const QString target = fixture.filePath("output");
        CompressionOptions options;
        options.format = "7z";
        options.SFXMode = true;
        options.SfxModule = path;
        QVERIFY(SourceCreationFixture::Create(backend, target, {source}, {}, false, options, false,
                                              SourceCreationFixture::OptionsOwner::Native) != S_OK);
        QVERIFY(!QFileInfo::exists(target));
        QCOMPARE(read(source), QByteArray("source bytes"));
        QCOMPARE(read(path), bytes);
    }
    void backendCreatesSelfTestingExecutable_data() {
        QTest::addColumn<bool>("multiRoot");
        QTest::addColumn<bool>("volumes");
        QTest::addColumn<QString>("outcome");
        for (bool multiRoot : {false, true})
            for (bool volumes : {false, true})
                for (const QString &outcome : {"success", "missing", "cancel", "occupied"})
                    QTest::newRow(
                        qPrintable(QString::number(multiRoot) + QString::number(volumes) + outcome))
                        << multiRoot << volumes << outcome;
    }
    void backendCreatesSelfTestingExecutable() {
        QFETCH(bool, multiRoot);
        QFETCH(bool, volumes);
        QFETCH(QString, outcome);
        QTemporaryDir fixture, second;
        QVERIFY(fixture.isValid() && second.isValid());
        const QString name = QStringLiteral("имя 空 with spaces.bin");
        const QByteArray bytes("binary\0fixture\xff", 15);
        QVERIFY(write(fixture.filePath(name), bytes));
        QVERIFY(write(second.filePath("other.txt"), "other root"));
        QStringList sources{fixture.filePath(name)};
        if (multiRoot)
            sources.append(second.filePath("other.txt"));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        CompressionOptions options;
        options.format = "7z";
        options.SFXMode = true;
        options.SfxModule = outcome == "missing" ? fixture.filePath("missing.sfx") : sfx_;
        if (volumes)
            options.VolumeSizes.Add(32);
        const QString base = fixture.filePath("self extracting archive");
#ifdef Q_OS_WIN
        const QString target = base + ".exe";
#else
        const QString target = base;
#endif
        const QString occupied = volumes ? base + ".7z.001" : target;
        if (outcome == "occupied")
            QVERIFY(write(occupied, "foreign output"));
        const QByteArray originalModule = read(sfx_);
        const QString password = multiRoot ? QStringLiteral("local fixture password") : QString();
        const HRESULT result = SourceCreationFixture::Create(
            backend, target, sources, password, multiRoot, options, outcome == "cancel",
            SourceCreationFixture::OptionsOwner::Native);
        QCOMPARE(result == S_OK, outcome == "success");
        QCOMPARE(read(fixture.filePath(name)), bytes);
        QCOMPARE(read(sfx_), originalModule);
        if (outcome != "success") {
            if (outcome == "occupied")
                QCOMPARE(read(occupied), QByteArray("foreign output"));
            if (outcome != "occupied" || volumes)
                QVERIFY(!QFileInfo::exists(target));
            if (volumes && outcome != "occupied")
                QVERIFY(QDir(fixture.path()).entryList({"*.7z.*"}, QDir::Files).isEmpty());
            QVERIFY(QDir(fixture.path())
                        .entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden)
                        .isEmpty());
            return;
        }
        QVERIFY(QFileInfo(target).isExecutable());
        if (!multiRoot && !volumes) {
            std::shared_ptr<ArchiveNativeOpen> reader;
            QCOMPARE(backend.openNativeArchive(target, "7z", nullptr, reader), S_OK);
            QVERIFY(reader);
            UInt32 count = 0;
            QCOMPARE(reader->arc().Archive->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(1));
        }
        if (volumes) {
            QCOMPARE(read(target), originalModule);
            QVERIFY(QFileInfo::exists(base + ".7z.002"));
            QFile first(base + ".7z.001");
            QVERIFY(first.open(QIODevice::ReadOnly));
            QCOMPARE(first.read(6), QByteArray::fromHex("377abcaf271c"));
        }
        QProcess process;
        QStringList testArgs{"t"};
        if (!password.isEmpty())
            testArgs.append("-p" + password);
#ifdef Q_OS_WIN
        // SFXWin has no test command: its source always extracts. Test the
        // embedded archive/payload with the official engine, then run the GUI.
        testArgs << "--" << (volumes ? base + ".7z.001" : target);
        process.start(engine_, testArgs);
#else
        process.start(target, testArgs);
#endif
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const QString output = fixture.filePath("extracted");
        QStringList extractArgs{"x", "-o" + output, "-y"};
#ifdef Q_OS_WIN
        extractArgs.removeFirst(); // SfxWin.cpp accepts only -o/-y/-p.
#endif
        if (!password.isEmpty())
            extractArgs.append("-p" + password);
        process.start(target, extractArgs);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(read(output + "/" + name), bytes);
        QCOMPARE(QFileInfo::exists(output + "/other.txt"), multiRoot);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SfxCreationTests)
#include "sfx_creation_tests.moc"
