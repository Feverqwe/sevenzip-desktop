#include "Common/StringConvert.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/native_archive_fixture.h"
#include "formats/cpio/support/dot_path_cpio_fixture.h"
#include "localization/LangUtils.h"
#include "platform/memory/qtmemorylockservices.h"
#include "settings/RegistryUtils.h"
#include <QPointer>
#include <QScopeGuard>
#include <memory>

class ArchiveBackendTests : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT

  private slots:
    void readCompletionMayReleaseBackend_data() {
        QTest::addColumn<QString>("stage");
        for (const char *stage : {"diagnostic", "info", "entries"})
            QTest::newRow(stage) << QString::fromLatin1(stage);
    }
    void readCompletionMayReleaseBackend() {
        QFETCH(QString, stage);
        QTemporaryDir fixture;
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        if (stage == "diagnostic")
            writeFile(archive, "ordinary bytes");
        else
            QVERIFY(NativeArchiveFixture::Write(archive, {{"payload.txt"}}, "zip"));
        QFile file(archive);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray original = file.readAll();
        file.close();
        auto backend = std::make_unique<ArchiveBackend>();
        QPointer<ArchiveBackend> retained(backend.get());
        int finished = 0;
        connect(backend.get(), &ArchiveBackend::finished, this, [&] { ++finished; });
        const auto release = [&](const QString &current) {
            if (stage == current)
                backend.reset();
        };
        connect(backend.get(), &ArchiveBackend::outputReceived, this,
                [&] { release("diagnostic"); });
        connect(backend.get(), &ArchiveBackend::infoReady, this, [&] { release("info"); });
        connect(backend.get(), &ArchiveBackend::entriesReady, this, [&] { release("entries"); });
        backend->list(archive);
        QTRY_VERIFY_WITH_TIMEOUT(retained.isNull(), 15000);
        QCOMPARE(finished, 0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
    void readDiagnosticCanStartAnotherArchive() {
        QTemporaryDir fixture;
        const QString first = fixture.filePath(QStringLiteral("не архив 空.bin"));
        const QString second = fixture.filePath("next.zip");
        writeFile(first, "ordinary bytes");
        QVERIFY(NativeArchiveFixture::Write(second, {{"next.txt"}}, "zip"));
        ArchiveBackend backend;
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        QSignalSpy entries(&backend, &ArchiveBackend::entriesReady);
        QStringList diagnostics;
        connect(&backend, &ArchiveBackend::outputReceived, this, [&](const QString &message) {
            diagnostics.append(message);
            if (diagnostics.size() == 1)
                backend.list(second);
        });
        backend.list(first);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);
        QVERIFY(!completed.first().first().toBool());
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!diagnostics.isEmpty());
        QVERIFY(diagnostics.first().contains(first));
        QVERIFY(!diagnostics.first().contains(second));
        QCOMPARE(completed.first()[1].toString(), diagnostics.join('\n'));
        QCOMPARE(entries.size(), 1);
        const auto items = qvariant_cast<QList<ArchiveEntry>>(entries.first().first());
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().path, QString("next.txt"));
        QVERIFY(backend.hasListedSourceArchive(second));
        QVERIFY(!backend.busy());
    }
    void rejectedRequestDoesNotReusePreviousResult_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<bool>("cancelled");
        for (const char *operation : {"extract", "test"})
            for (bool cancelled : {false, true})
                QTest::newRow(qPrintable(
                    QString("%1-after-%2").arg(operation, cancelled ? "cancel" : "success")))
                    << QString::fromLatin1(operation) << cancelled;
    }
    void rejectedRequestDoesNotReusePreviousResult() {
        QFETCH(QString, operation);
        QFETCH(bool, cancelled);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("архив с пробелом.zip"));
        QVERIFY(NativeArchiveFixture::Write(archive, {{"a.txt"}}, "zip"));
        QFile saved(archive);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray before = saved.readAll();
        saved.close();
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        QCOMPARE(backend.lastResult(), S_OK);
        if (cancelled) {
            const auto cancellation =
                connect(&backend, &ArchiveBackend::phaseStarted, &backend, &ArchiveBackend::cancel);
            backend.testSourceArchive(archive, {});
            await(backend, false);
            disconnect(cancellation);
            QCOMPARE(backend.lastResult(), E_ABORT);
        }
        QSignalSpy refused(&backend, &ArchiveBackend::finished);
        if (operation == "extract")
            backend.extract(archive, fixture.filePath("output"), {"missing.txt"});
        else
            backend.test(fixture.filePath("unlisted.zip"));
        QCOMPARE(refused.size(), 1);
        QVERIFY(!refused[0][0].toBool());
        QVERIFY(FAILED(backend.lastResult()));
        QVERIFY(backend.lastResult() != E_ABORT);
        QVERIFY(!backend.busy());
        QFile original(archive);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), before);
    }
    void nativePathListingFailure_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<HRESULT>("result");
        QTest::newRow("not-archive") << "bytes" << S_FALSE;
        QTest::newRow("specific-type") << "type" << S_FALSE;
        QTest::newRow("io-failure") << "missing" << HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    void nativePathListingFailure() {
        QFETCH(QString, kind);
        QFETCH(HRESULT, result);
        QTemporaryDir fixture;
        const QString archive = fixture.filePath(QStringLiteral("не архив 空 with spaces.bin"));
        if (kind != "missing")
            writeFile(archive, "ordinary bytes");
        ArchiveBackend backend;
        if (kind == "type")
            backend.openFolder(archive, "zip", {});
        else
            backend.list(archive);
        await(backend, false);
        QCOMPARE(backend.lastResult(), result);
        if (kind != "missing") {
            QFile source(archive);
            QVERIFY(source.open(QIODevice::ReadOnly));
            QCOMPARE(source.readAll(), QByteArray("ordinary bytes"));
        }
    }
    void passwordCallbackWithoutConsole() {
        QTemporaryDir fixture;
        const QString archive = fixture.filePath("fixture.7z");
        QVERIFY(
            NativeArchiveFixture::Write(archive, {{"payload.txt"}}, "7z", "fixture-only-password"));
        ArchiveBackend backend;
        QSignalSpy prompt(&backend, &ArchiveBackend::passwordRequired);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        connect(&backend, &ArchiveBackend::passwordRequired, &backend,
                [&] { backend.providePassword("fixture-only-password"); });
        backend.list(archive);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(prompt.size(), 1);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
    }
    void alternateStreamOutputValidation_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("declared");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("real-stream") << "folder/file.bin" << "stream δ" << true << true;
        QTest::newRow("native-separators") << "folder\\file.bin" << "stream δ" << true << true;
        QTest::newRow("ordinary-colon") << "folder/file.bin" << "stream" << false << false;
        QTest::newRow("unsafe-base") << "../file.bin" << "stream" << true << false;
        QTest::newRow("unsafe-stream") << "folder/file.bin" << "../escape" << true << false;
        QTest::newRow("nested-stream") << "folder/file.bin" << "dir/file" << true << false;
        QTest::newRow("nested-ads") << "folder/file.bin" << "stream:extra" << true << false;
    }
    void alternateStreamOutputValidation() {
        QFETCH(QString, base);
        QFETCH(QString, name);
        QFETCH(bool, declared);
        QFETCH(bool, accepted);
        QTemporaryDir fixture;
        const QString entry = base + ':' + name;
        ArchiveBackend backend;
        const QString archive = fixture.filePath("fixture.7z");
        const QList<NativeArchiveFixture::Item> items =
            accepted ? QList<NativeArchiveFixture::Item>{{base, "base"}, {entry, "x", true}}
                     : QList<NativeArchiveFixture::Item>{{entry, "x", declared}};
        QVERIFY(NativeArchiveFixture::Write(archive, items, accepted ? "wim" : "7z"));
        backend.list(archive);
        await(backend, true);
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
        options.currentFolder = base + ':';
        options.currentFolder.replace('\\', '/');
        QString selected = entry;
        selected.replace('\\', '/');
        QString error;
        QCOMPARE(
            backend.validateExtraction(fixture.filePath("output"), {selected}, options, &error),
            accepted);
        if (accepted) {
            QVERIFY(QDir().mkpath(fixture.filePath("output/" + name)));
            QVERIFY(!backend.validateExtraction(fixture.filePath("output"), {selected}, options,
                                                &error));
        }
    }
    void extractionValidationLanguagePreservesRawPath() {
        QTemporaryDir fixture;
        const QString languageSettings = fixture.filePath("language.ini");
        const QString entry = QStringLiteral("../путь с пробелом 密碼.txt");
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), languageSettings);
            ReloadLang(languageSettings);
        });
        ArchiveBackend backend;
        const QString archive = fixture.filePath("fixture.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{entry}}, "zip"));
        QString listedPath;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &entries) {
                    if (!entries.isEmpty())
                        listedPath = entries.first().path;
                });
        for (const char *language : {"-", "ru", "-"}) {
            SaveRegLang(UString(language), languageSettings);
            ReloadLang(languageSettings);
            QSignalSpy listed(&backend, &ArchiveBackend::finished);
            backend.list(archive);
            QVERIFY(listed.wait(30000));
            const auto opened = listed.takeFirst();
            QVERIFY(opened.at(0).toBool());
            QCOMPARE(opened.at(1).toString(), QString(language) == "ru"
                                                  ? QStringLiteral("Архив открыт.")
                                                  : QString("Archive opened."));
            QSignalSpy result(&backend, &ArchiveBackend::finished);
            backend.extract(archive, QString());
            QCOMPARE(result.size(), 1);
            const auto empty = result.takeFirst();
            QVERIFY(!empty.at(0).toBool());
            QCOMPARE(empty.at(1).toString(), QString(language) == "ru"
                                                 ? QStringLiteral("Укажите папку назначения.")
                                                 : QString("Specify a destination folder."));
            backend.extract(archive, fixture.path());
            QCOMPARE(result.size(), 1);
            const auto unsafe = result.takeFirst();
            QVERIFY(!unsafe.at(0).toBool());
            QCOMPARE(unsafe.at(1).toString(),
                     (QString(language) == "ru"
                          ? QStringLiteral("Архив содержит небезопасный путь или ссылку: ")
                          : QString("Archive contains an unsafe path or link: ")) +
                         listedPath);
            QVERIFY(!backend.busy());
            QVERIFY(!QFileInfo::exists(fixture.filePath("путь с пробелом 密碼.txt")));
        }
    }
    void roundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QString("7z");
        QTest::newRow("zip") << QString("zip");
    }
    void roundTrip() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.path() + "/имя с пробелом";
        QVERIFY(QDir().mkpath(source + "/вложено"));
        writeFile(source + "/обычный файл.txt", "hello");
        writeFile(source + "/вложено/δ space.txt", "nested");
        ArchiveBackend backend;
        const QString archive = temp.path() + "/archive." + format;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        QVERIFY(QFileInfo::exists(archive));

        QList<ArchiveEntry> entries;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &value) { entries = value; });
        backend.list(archive);
        await(backend, true);
        QVERIFY(entries.size() >= 4);
        QString nested;
        for (const auto &entry : entries) {
            if (entry.path.endsWith("δ space.txt")) {
                nested = entry.path;
                QCOMPARE(entry.size, qint64(6));
                QVERIFY(entry.packedSize >= 0);
                QVERIFY(!entry.modified.isEmpty());
            }
        }
        QVERIFY(!nested.isEmpty());

        const QString selected = temp.path() + "/selected";
        QVERIFY(QDir().mkpath(selected));
        backend.extract(archive, selected, {nested});
        await(backend, true);
        QVERIFY(QFileInfo::exists(selected + "/" + nested));
        QVERIFY(!QFileInfo::exists(selected + "/имя с пробелом/обычный файл.txt"));

        const QString all = temp.path() + "/all";
        QVERIFY(QDir().mkpath(all));
        backend.extract(archive, all);
        await(backend, true);
        QFile file(all + "/имя с пробелом/обычный файл.txt");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("hello"));
    }
    void encryptedRoundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z-hidden-names") << QStringLiteral("7z");
        QTest::newRow("zip-aes256") << QStringLiteral("zip");
    }
    void encryptedRoundTrip() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString fixturePassword = format == "7z" ? QStringLiteral("test-only-δ-пароль")
                                                       : QStringLiteral("test-only-password");
        const QString source = temp.filePath(QStringLiteral("δ file with spaces.txt"));
        const QString archive = temp.filePath(QStringLiteral("encrypted.") + format);
        const QString destination = temp.filePath(QStringLiteral("extracted files"));
        writeFile(source, "protected content");
        QVERIFY(QDir().mkpath(destination));
        ArchiveBackend backend;
        QSignalSpy output(&backend, &ArchiveBackend::outputReceived);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, format, fixturePassword,
                                                 format == "7z"));
        QVERIFY(QFileInfo::exists(archive));
        QSignalSpy info(&backend, &ArchiveBackend::infoReady);
        if (format == "7z") {
            QSignalSpy wrong(&backend, &ArchiveBackend::finished);
            backend.list(archive, QStringLiteral("incorrect-test-password"));
            QVERIFY(wrong.wait(30000));
            QVERIFY(!wrong.takeFirst().at(0).toBool());
            QVERIFY(info.isEmpty());
        }
        QSignalSpy prompt(&backend, &ArchiveBackend::passwordRequired);
        connect(&backend, &ArchiveBackend::passwordRequired, &backend,
                [&backend, &fixturePassword] { backend.providePassword(fixturePassword); });
        backend.list(archive);
        await(backend, true);
        if (format == "7z")
            QCOMPARE(prompt.size(), 1);
        QCOMPARE(info.size(), 1);
        QVERIFY(qvariant_cast<ArchiveInfo>(info.takeFirst().at(0)).encrypted);
        QSignalSpy wrong(&backend, &ArchiveBackend::finished);
        backend.test(archive, QStringLiteral("incorrect-test-password"));
        QVERIFY(wrong.wait(30000));
        const auto failure = wrong.takeFirst();
        QVERIFY(!failure.at(0).toBool());
        QVERIFY(!failure.at(1).toString().isEmpty());
        QVERIFY(backend.lastResult() != S_OK);
        QVERIFY(!failure.at(1).toString().contains(fixturePassword));
        backend.test(archive, fixturePassword);
        await(backend, true);
        QSignalSpy extractFailure(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, QStringLiteral("incorrect-test-password"));
        QVERIFY(extractFailure.wait(30000));
        QVERIFY(!extractFailure.takeFirst().at(0).toBool());
        // The official extractor can retain unverified output on a password/data error.
        // Keep the successful retry independent of that failed extraction's destination.
        for (const QString &file : QDir(destination).entryList(QDir::Files | QDir::NoDotAndDotDot))
            QVERIFY(QFile::remove(QDir(destination).filePath(file)));
        backend.extract(archive, destination, {}, fixturePassword);
        await(backend, true);
        QFile extracted(destination + "/δ file with spaces.txt");
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("protected content"));
        for (const auto &line : output)
            QVERIFY(!line.at(0).toString().contains(fixturePassword));
    }
    void testAndArchiveInfo_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QStringLiteral("7z");
        QTest::newRow("zip") << QStringLiteral("zip");
    }
    void testAndArchiveInfo() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.path() + QStringLiteral("/δ space.txt");
        const QString archive = temp.path() + "/archive." + format;
        writeFile(source, "verified");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        QSignalSpy info(&backend, &ArchiveBackend::infoReady);
        backend.list(archive);
        await(backend, true);
        QCOMPARE(info.size(), 1);
        const ArchiveInfo details = qvariant_cast<ArchiveInfo>(info.takeFirst().at(0));
        QCOMPARE(details.format.toLower(), format);
        QCOMPARE(details.physicalSize, QFileInfo(archive).size());
        QCOMPARE(details.files, 1);
        QCOMPARE(details.unpackedSize, qint64(8));
        QSignalSpy result(&backend, &ArchiveBackend::finished);
        backend.test(archive);
        QVERIFY(result.wait(30000));
        QVERIFY(result.takeFirst().at(0).toBool());
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 2);
        QFile corrupt(archive);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("broken");
        corrupt.close();
        backend.test(archive);
        QCOMPARE(result.size(), 1);
        QVERIFY(!result.takeFirst().at(0).toBool());
    }
    void selectedExtractionIgnoresUnselectedCollision() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.path() + "/fixture.zip";
        const QString destination = temp.path() + "/out";
        QVERIFY(QDir().mkpath(destination));
        writeFile(destination + "/occupied.txt", "keep");
        const QString source = temp.path() + "/source";
        QVERIFY(QDir().mkpath(source));
        writeFile(source + "/occupied.txt", "old");
        writeFile(source + "/chosen.txt", "yes");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(
            archive, {source + "/occupied.txt", source + "/chosen.txt"}));
        backend.list(archive);
        await(backend, true);
        QSignalSpy result(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {"chosen.txt"});
        QVERIFY(result.wait(30000));
        QVERIFY(result.takeFirst().at(0).toBool());
        QFile existing(destination + "/occupied.txt");
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("keep"));
        QFile chosen(destination + "/chosen.txt");
        QVERIFY(chosen.open(QIODevice::ReadOnly));
        QCOMPARE(chosen.readAll(), QByteArray("yes"));
    }
    void testCancellation() {
        QTemporaryDir temp;
        const QString archive = temp.filePath("archive.7z");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"a.txt"}}));
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        connect(&backend, &ArchiveBackend::phaseStarted, &backend, &ArchiveBackend::cancel);
        backend.test(archive);
        await(backend, false);
        QCOMPARE(backend.lastResult(), E_ABORT);
        QVERIFY(!backend.busy());
    }
    void invalidArchive() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.path() + "/broken.7z";
        writeFile(archive, "this is not an archive");
        ArchiveBackend backend;
        QSignalSpy spy(&backend, &ArchiveBackend::finished);
        backend.list(archive);
        QVERIFY(spy.wait(30000));
        QVERIFY(!spy.takeFirst().at(0).toBool());
    }
    void missingNativeModule() {
        ArchiveBackend backend(nullptr, "/no/such/7z");
        QSignalSpy spy(&backend, &ArchiveBackend::finished);
        backend.list("/no/such/archive.7z");
        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 30000);
        const auto result = spy.takeFirst();
        QVERIFY(!result.at(0).toBool());
        QVERIFY(!result.at(1).toString().isEmpty());
        QCOMPARE(backend.lastResult(), E_FAIL);
    }
    void dotPathsInCpio_data() {
        QTest::addColumn<bool>("currentFolder");
        QTest::newRow("full-paths") << false;
        QTest::newRow("current-paths") << true;
    }
    void dotPathsInCpio() {
        QFETCH(bool, currentFolder);
        QTemporaryDir fixture;
        const QString archive = fixture.filePath("Payload.cpio");
        writeFile(archive, DotPathCpioFixture());
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        const QString leaf = QStringLiteral("имя 空.txt");
        ExtractionOptions options;
        QStringList selected;
        if (currentFolder) {
            options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
            options.currentFolder = "./ProxyBridge.app";
            selected << options.currentFolder + '/' + leaf;
        }
        const QString destination = fixture.filePath("output");
        backend.extract(archive, destination, selected, {}, options);
        await(backend, true);
        QFile extracted(
            QDir(destination).filePath(currentFolder ? leaf : "ProxyBridge.app/" + leaf));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("payload bytes"));
    }
    void currentSourceFolderValidation_data() {
        QTest::addColumn<QString>("entry");
        QTest::addColumn<QString>("currentFolder");
        QTest::addColumn<QString>("sourceFolder");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("bound-parent") << "../имя 空.txt" << ".." << ".." << true;
        QTest::newRow("bound-nested-parent")
            << "outer/../inside/имя 空.txt" << "outer/../inside" << "outer/../inside" << true;
        QTest::newRow("unbound-parent") << "../file.txt" << ".." << "" << false;
        QTest::newRow("different-binding") << "../file.txt" << ".." << "other" << false;
        QTest::newRow("remaining-traversal")
            << "../inside/../escape.txt" << "../inside" << "../inside" << false;
        QTest::newRow("prefix-boundary")
            << "../inside-other/file.txt" << "../inside" << "../inside" << false;
        QTest::newRow("reserved-output") << "../CON.txt" << ".." << ".." << false;
    }
    void currentSourceFolderValidation() {
        QFETCH(QString, entry);
        QFETCH(QString, currentFolder);
        QFETCH(QString, sourceFolder);
        QFETCH(bool, accepted);
        QTemporaryDir fixture;
        ArchiveBackend backend;
        const QString archive = fixture.filePath("fixture.7z");
        QVERIFY(NativeArchiveFixture::Write(archive, {{entry}}));
        backend.list(archive);
        await(backend, true);
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
        options.currentFolder = currentFolder;
        QString error;
        const QString output = fixture.filePath("output");
        QCOMPARE(backend.validateExtraction(output, {entry}, options, &error, false, sourceFolder),
                 accepted);
        if (accepted) {
            options.pathMode = ExtractionOptions::PathMode::FullPaths;
            QVERIFY(
                !backend.validateExtraction(output, {entry}, options, &error, false, sourceFolder));
            options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
            QVERIFY(QDir().mkpath(QDir(output).filePath(entry.section('/', -1))));
            QVERIFY(
                !backend.validateExtraction(output, {entry}, options, &error, false, sourceFolder));
        }
    }
    void unsafePath() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.path() + "/outside.txt";
        const QString archive = temp.path() + "/unsafe.zip";
        const QString destination = temp.path() + "/out";
        writeFile(source, "unsafe");
        QVERIFY(QDir().mkpath(destination));
        QProcess maker;
        maker.start("7z", {"a", "-tzip", "-spf", archive, source});
        QVERIFY(maker.waitForFinished(30000));
        QCOMPARE(maker.exitCode(), 0);
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        QSignalSpy spy(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination);
        QCOMPARE(spy.size(), 1);
        QVERIFY(!spy.takeFirst().at(0).toBool());
        QVERIFY(QDir(destination).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
    }
    void unsafeListing_data() {
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QString>("link");
        for (const QString &name : {"../escape.txt", "./../escape.txt", "folder\\..\\escape.txt",
                                    "C:\\escape.txt", "CON.txt"})
            QTest::newRow(qPrintable(name)) << QStringList{name} << QString();
        QTest::newRow("symbolic link") << QStringList{"link"} << QString("../outside");
        QTest::newRow("hard link") << QStringList{"link"} << QString("hard:target");
        QTest::newRow("case duplicate") << QStringList{"same.txt", "SAME.TXT"} << QString();
        QTest::newRow("dot duplicate") << QStringList{"./same.txt", "same.txt"} << QString();
        QTest::newRow("file parent") << QStringList{"parent", "parent/child"} << QString();
        QTest::newRow("dot file parent") << QStringList{"./parent", "parent/child"} << QString();
        QTest::newRow("unicode normalization")
            << QStringList{QStringLiteral("café.txt"), QStringLiteral("cafe\u0301.txt")}
            << QString();
    }
    void unsafeListing() {
        QFETCH(QStringList, names);
        QFETCH(QString, link);
        QTemporaryDir temp;
        QList<NativeArchiveFixture::Item> items;
        for (const auto &name : names) {
            NativeArchiveFixture::Item item{name};
            if (link.startsWith("hard:"))
                item.HardLink = link.mid(5);
            else
                item.SymLink = link;
            items.append(item);
        }
        const QString archive = temp.filePath("fixture.tar");
        QVERIFY(NativeArchiveFixture::Write(archive, items, "tar"));
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        QSignalSpy spy(&backend, &ArchiveBackend::finished);
        backend.extract(archive, temp.filePath("output"));
        QCOMPARE(spy.size(), 1);
        QVERIFY(!spy.takeFirst().at(0).toBool());
        QVERIFY(!backend.busy());
    }
    void unsafeDestination() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.path() + "/имя с пробелом.txt";
        const QString archive = temp.path() + "/safe.zip";
        writeFile(source, "original");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);

        const QString created = temp.path() + "/новая папка/ещё одна";
        backend.extract(archive, created);
        await(backend, true);
        QFile createdFile(created + "/имя с пробелом.txt");
        QVERIFY(createdFile.open(QIODevice::ReadOnly));
        QCOMPARE(createdFile.readAll(), QByteArray("original"));

        const QString occupied = temp.path() + "/occupied";
        QVERIFY(QDir().mkpath(occupied));
        writeFile(occupied + "/имя с пробелом.txt", "keep");
        QSignalSpy occupiedSpy(&backend, &ArchiveBackend::finished);
        backend.extract(archive, occupied);
        QCOMPARE(occupiedSpy.size(), 1);
        QVERIFY(!occupiedSpy.takeFirst().at(0).toBool());
        QFile existing(occupied + "/имя с пробелом.txt");
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("keep"));

        const QString linked = temp.path() + "/linked";
        if (QFile::link(occupied, linked)) {
            QSignalSpy linkedSpy(&backend, &ArchiveBackend::finished);
            backend.extract(archive, linked + "/новая папка");
            QCOMPARE(linkedSpy.size(), 1);
            QVERIFY(!linkedSpy.takeFirst().at(0).toBool());
            QVERIFY(!QFileInfo::exists(occupied + "/новая папка"));
        }
    }
    void cancellation_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("English") << QString("-");
        QTest::newRow("Russian") << QString("ru");
    }
    void cancellation() {
        QFETCH(QString, language);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settings = temp.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        const QString source = temp.path() + "/large.bin";
        writeFile(source, QByteArray(8 * 1024 * 1024, 'x'));
        ArchiveBackend backend;
        QSignalSpy spy(&backend, &ArchiveBackend::finished);
        const QString archive = temp.filePath("cancel.7z");
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        spy.clear();
        connect(&backend, &ArchiveBackend::phaseStarted, &backend, &ArchiveBackend::cancel);
        backend.test(archive);
        QVERIFY(spy.wait(30000));
        const auto result = spy.takeFirst();
        QVERIFY(!result.at(0).toBool());
        QCOMPARE(result.at(1).toString(), language == "ru" ? QStringLiteral("Операция отменена.")
                                                           : QString("Operation canceled."));
        QCOMPARE(backend.lastResult(), E_ABORT);
        QVERIFY(!backend.busy());
        QVERIFY(QFileInfo::exists(archive));
        QVERIFY(QDir(temp.path()).entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void mixedSources() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("файл с пробелом.txt"));
        const QString folder = temp.filePath(QStringLiteral("папка Unicode"));
        QVERIFY(QDir().mkpath(folder));
        writeFile(source, "file");
        writeFile(folder + "/nested.txt", "nested");
        ArchiveBackend backend;
        const QString target = temp.filePath("mixed.7z");
        QVERIFY(NativeArchiveFixture::WriteFiles(target, {source, folder}));
        QList<ArchiveEntry> entries;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &value) { entries = value; });
        backend.list(target);
        await(backend, true);
        QStringList names;
        for (const ArchiveEntry &entry : entries)
            names
                << QDir::fromNativeSeparators(entry.path).normalized(QString::NormalizationForm_C);
        QVERIFY(names.contains(QStringLiteral("файл с пробелом.txt")));
        QVERIFY(names.contains(QStringLiteral("папка Unicode/nested.txt")));
        QVERIFY(QDir(temp.path()).entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ArchiveBackendTests)
#include "archive_backend_tests.moc"
