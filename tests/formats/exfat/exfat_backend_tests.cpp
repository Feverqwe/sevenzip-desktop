#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivereadonlyview.h"
#include "archive/resources/qtarchivefileresource.h"
#include "formats/exfat/support/exfat_fixture.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class ExfatBackendTests final : public QObject {
    Q_OBJECT
  private slots:
    void discoversBrowsesAndExtractsReadOnly_data() {
        QTest::addColumn<int>("layout");
        QTest::newRow("exfat") << -1;
        QTest::newRow("texfat-first") << 0;
        QTest::newRow("texfat-second") << 1;
        QTest::newRow("texfat-fragmented-bitmap") << 2;
        QTest::newRow("zero-size-directories") << 3;
        QTest::newRow("dirty-texfat-first") << 4;
        QTest::newRow("media-texfat-second") << 5;
        QTest::newRow("dirty-media-exfat") << 6;
    }
    void discoversBrowsesAndExtractsReadOnly() {
        QFETCH(int, layout);
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QDir prepared(QString::fromUtf8(SEVENZIP_EXFAT_RUNTIME));
#ifdef Q_OS_WIN
        const QString engineName = "7z.exe", coreName = "7z.dll", suffix = ".dll";
#else
        const QString engineName = "7z", coreName = "7z.so", suffix = ".so";
#endif
        const QString engine = runtime.filePath(engineName);
        const QString module = runtime.filePath(coreName);
        QVERIFY(QFile::copy(prepared.filePath(engineName), engine));
        QVERIFY(QFile::copy(prepared.filePath(coreName), module));
        QVERIFY(QDir(runtime.path()).mkdir("Formats"));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_EXFAT_MODULE),
                            runtime.filePath(QStringLiteral("Formats/формат 空") + suffix)));
        ExfatFixture fixture;
        if (layout >= 0 && layout <= 2)
            fixture = TexfatFixture(layout == 2 ? 1 : unsigned(layout), 512, layout == 2);
        if (layout == 4 || layout == 5)
            fixture = TexfatFixture(unsigned(layout - 4));
        if (layout >= 4)
            fixture.image[106] = char(layout == 4 ? 2 : layout == 5 ? 5 : 6);
        if (layout == 3) {
            fixture.fat(10, 11);
            fixture.fat(11, 0xffffffff);
            const int bitmap = fixture.clusterOffset(3) + 1;
            fixture.image[bitmap] = char(quint8(fixture.image[bitmap]) | 2);
            fixture.emptyDirectory(31, QStringLiteral("пустой 空"));
            fixture.emptyDirectory(34, QStringLiteral("empty2"));
        }
        const QString image = runtime.filePath(QStringLiteral("карта памяти 空.img"));
        QFile source(image);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(fixture.image), qint64(fixture.image.size()));
        source.close();
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        const auto catalog = backend.codecCatalog();
        QVERIFY(catalog->formatIndex("exFAT") >= 0);
        QVERIFY(!catalog->canUpdate("exFAT"));
        QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
        ArchiveInfo info;
        connect(&backend, &ArchiveBackend::infoReady, this,
                [&](const ArchiveInfo &result) { info = result; });
        QList<ArchiveEntry> entries;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &result) { entries = result; });
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        backend.list(image);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QCOMPARE(listed.size(), 1);
        if (layout >= 4)
            QVERIFY(info.properties.contains("Read-only limitations"));
        QCOMPARE(entries.size(), layout == 3 ? 7 : 5);
        finished.clear();
        backend.test(image);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        finished.clear();
        const QString output = runtime.filePath("output");
        backend.extract(image, output);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QFile fragmented(QDir(output).filePath(fixture.fragmentedName));
        QVERIFY(fragmented.open(QIODevice::ReadOnly));
        QCOMPARE(fragmented.readAll(), QByteArray(512, 'A') + QByteArray(188, 'B'));
        QFile nested(QDir(output).filePath(fixture.directoryName + '/' + fixture.nestedName));
        QVERIFY(nested.open(QIODevice::ReadOnly));
        QCOMPARE(nested.readAll(), QByteArray("tail") + QByteArray(1020, '\0'));
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(backend.openNativeArchive(image, {}, nullptr, reader), S_OK);
        auto *owner = new QtArchiveFileResource(image);
        CMyComPtr<IUnknown> retained = owner;
        owner->AttachNativeOpen(reader);
        owner->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        int extractions = 0;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error,
                                                    [&](const QString &, quint64) {
                                                        ++extractions;
                                                        return false;
                                                    }),
                 S_OK);
        bool cacheRequired = true;
        QCOMPARE(view->needsCache(cacheRequired), S_OK);
        QVERIFY(!cacheRequired);
        const auto file =
            std::find_if(view->entries().begin(), view->entries().end(),
                         [&](const auto &entry) { return entry.Name == fixture.fragmentedName; });
        QVERIFY(file != view->entries().end());
        const auto range = view->read(file->Id, 500, 200);
        QCOMPARE(range.Result, S_OK);
        QCOMPARE(range.Data, QByteArray(12, 'A') + QByteArray(188, 'B'));
        QCOMPARE(extractions, 0);
        if (layout == 3) {
            for (const auto &name : {QStringLiteral("пустой 空"), QStringLiteral("empty2")}) {
                const QDir directory(QDir(output).filePath(name));
                QVERIFY(directory.exists());
                QVERIFY(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
            }
        }
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), fixture.image);
    }
};
QTEST_GUILESS_MAIN(ExfatBackendTests)
#include "exfat_backend_tests.moc"
