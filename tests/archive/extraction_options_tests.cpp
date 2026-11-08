#include "archive/backend/archivebackend.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "archive/support/native_archive_fixture.h"
#include "fileops/links/qtlinkservices.h"
#include "platform/support/native_link_fixture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class ExtractionOptionsTests : public QObject {
    Q_OBJECT

    static void writeFile(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
    }

    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    static void await(ArchiveBackend &backend, bool success) {
        QSignalSpy result(&backend, &ArchiveBackend::finished);
        QVERIFY(result.wait(30000));
        QCOMPARE(result.takeFirst().at(0).toBool(), success);
    }

  private slots:
    void overwriteCancellationLeavesReplacementRunning_data() {
        QTest::addColumn<bool>("hashReplacement");
        QTest::newRow("read-to-hash") << true;
        QTest::newRow("read-to-read") << false;
    }
    void overwriteCancellationLeavesReplacementRunning() {
        QFETCH(bool, hashReplacement);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath(QStringLiteral("архив 空.7z"));
        QVERIFY(NativeArchiveFixture::Write(archive, {{"file.txt", "new bytes"}}));
        const QString destination = temp.filePath("output with spaces");
        QVERIFY(QDir().mkpath(destination));
        const QString output = destination + "/file.txt";
        writeFile(output, "original bytes");
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        backend.list(archive);
        await(backend, true);
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        int dismissals = 0;
        bool replacementRunningOnReturn = false;
        connect(&backend, &ArchiveBackend::dismissOverwriteRequested, &backend, [&] {
            // Bound the unfixed recursive dismissal while checking that one
            // cancellation dismisses its prompt only once.
            if (++dismissals != 1)
                return;
            backend.cancelAndWait();
            if (hashReplacement)
                backend.hashMethods({archive}, {"CRC32"});
            else
                backend.test(archive);
        });
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend, [&] {
            backend.cancelAndWait();
            replacementRunningOnReturn = backend.busy() && completed.size() == 1;
        });
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        backend.extract(archive, destination, {}, {}, options);
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(replacementRunningOnReturn);
        QCOMPARE(dismissals, 1);
        QVERIFY(!completed.first().first().toBool());
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(readFile(output), QByteArray("original bytes"));
    }

    void destinationChangesWhileOverwritePending() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath("fixture.7z");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"file.txt", "new bytes"}}));
        const QString destination = temp.filePath("output");
        const QString preserved = temp.filePath("preserved output");
        const QString outside = temp.filePath("outside");
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(QDir().mkpath(outside));
        writeFile(destination + "/file.txt", "original bytes");
        writeFile(outside + "/file.txt", "outside bytes");
        bool linked = false;
        // Remove a directory reparse point before QTemporaryDir deletes its
        // target. Windows cannot remove that dangling entry through Qt alone.
        const auto removeLink = qScopeGuard([&] {
            if (linked)
                QtRemoveFileSystemEntry(destination);
        });
        ArchiveBackend backend(nullptr, temp.filePath("missing console"));
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        backend.list(archive);
        await(backend, true);
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend, [&] {
            if (!QDir().rename(destination, preserved)) {
                backend.cancel();
                return;
            }
            linked = CreateNativeSymbolicLink(destination, outside);
            backend.provideOverwriteAnswer(linked ? NOverwriteAnswer::kYes
                                                  : NOverwriteAnswer::kCancel);
        });
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        backend.extract(archive, destination, {}, {}, options);
        await(backend, false);
        if (!linked)
            QSKIP("This filesystem does not permit the test directory symlink");
        QCOMPARE(readFile(preserved + "/file.txt"), QByteArray("original bytes"));
        QCOMPARE(readFile(outside + "/file.txt"), QByteArray("outside bytes"));
        QVERIFY(backend.lastResult() != E_ABORT);
    }
    void nativeDirectorySelection_data() {
        QTest::addColumn<bool>("unsafeChild");
        QTest::newRow("selected directory and current paths") << false;
        QTest::newRow("unsafe descendant refuses the whole selection") << true;
    }
    void nativeDirectorySelection() {
        QFETCH(bool, unsafeChild);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString folder = QStringLiteral("папка 空 with spaces");
        const QString name = QStringLiteral("δ selected.txt");
        NativeArchiveFixture::Item directory;
        directory.Path = folder;
        directory.Directory = true;
        NativeArchiveFixture::Item selected;
        selected.Path = folder + '/' + name;
        selected.Bytes = "selected bytes";
        const UInt64 ticks = 116444736000000000ULL + 1234567;
        selected.Modified = FILETIME{UInt32(ticks), UInt32(ticks >> 32)};
        QList<NativeArchiveFixture::Item> items{
            directory, selected, {"unselected.txt", "unselected"}};
        if (unsafeChild)
            items.append({folder + "/../escape.txt", "unsafe bytes"});
        const QString archive = temp.filePath("fixture.7z");
        QVERIFY(NativeArchiveFixture::Write(archive, items));
        const QByteArray original = readFile(archive);
        ArchiveBackend backend(nullptr, temp.filePath("missing console"));
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        backend.list(archive);
        await(backend, true);
        const QString output = temp.filePath("output");
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        backend.extract(archive, output, {folder});
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toBool(), !unsafeChild);
        QVERIFY(!QFileInfo::exists(output + "/unselected.txt"));
        QVERIFY(!QFileInfo::exists(temp.filePath("escape.txt")));
        if (unsafeChild) {
            QVERIFY(!QFileInfo::exists(output + '/' + folder + '/' + name));
            return;
        }
        QCOMPARE(readFile(output + '/' + folder + '/' + name), QByteArray("selected bytes"));

        const QString flat = temp.filePath("current paths");
        QVERIFY(QDir().mkpath(flat));
        writeFile(flat + '/' + name, "old bytes");
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
        options.currentFolder = folder;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        QSignalSpy prompts(&backend, &ArchiveBackend::overwriteRequired);
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend,
                [&](const OverwriteQuestion &question) {
                    const auto answer = qScopeGuard([&] {
                        QTimer::singleShot(0, &backend, [&] {
                            backend.provideOverwriteAnswer(NOverwriteAnswer::kYes);
                        });
                    });
                    QCOMPARE(QDir::fromNativeSeparators(question.Existing.Path), flat + '/' + name);
                    QCOMPARE(QDir::fromNativeSeparators(question.Incoming.Path),
                             folder + '/' + name);
                    QCOMPARE(question.Existing.Size, std::optional<UInt64>(9));
                    QVERIFY(question.Existing.Modified.has_value());
                    QCOMPARE(question.Incoming.Size, std::optional<UInt64>(selected.Bytes.size()));
                    QVERIFY(question.Incoming.Modified.has_value());
                    QCOMPARE(question.Incoming.Modified->dwLowDateTime,
                             selected.Modified->dwLowDateTime);
                    QCOMPARE(question.Incoming.Modified->dwHighDateTime,
                             selected.Modified->dwHighDateTime);
                    QVERIFY(backend.busy());
                });
        done.clear();
        backend.extract(archive, flat, {folder + '/' + name}, {}, options);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QCOMPARE(prompts.size(), 1);
        QCOMPARE(readFile(flat + '/' + name), QByteArray("selected bytes"));
        QVERIFY(!QFileInfo::exists(flat + '/' + folder));
        QCOMPARE(readFile(archive), original);
    }
    void timestampFilenameRemainsInsideDestination() {
#ifdef Q_OS_WIN
        QSKIP("Colons are not ordinary Windows filename characters");
#else
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("2024-05-07T02:00:02Z.sql.gz");
        const QString source = fixture.filePath(name);
        const QString archive = fixture.filePath("timestamp.zip");
        const QString destination = fixture.filePath("output");
        writeFile(source, "fixture SQL gzip bytes");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        QString error;
        const QString output = QDir(destination).filePath(name);
        QVERIFY2(ArchiveBackend::validateExtractionOutput(destination, output, false, &error),
                 qPrintable(error));
        backend.extract(archive, destination);
        await(backend, true);
        QCOMPARE(readFile(output), QByteArray("fixture SQL gzip bytes"));
        QVERIFY(!ArchiveBackend::validateExtractionOutput(
            destination, fixture.filePath("escape:outside.txt"), false, &error));
        QVERIFY(QDir().mkpath(destination));
        const QString linked = QDir(destination).filePath("link:inside");
        if (QFile::link(fixture.path(), linked)) {
            QVERIFY(!ArchiveBackend::validateExtractionOutput(destination, linked + "/escape.txt",
                                                              false, &error));
            QVERIFY(!QFileInfo::exists(fixture.filePath("escape.txt")));
        }
#endif
    }
    void askOverwriteAfterCancelledProcessCanSucceed() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("файл с пробелом.txt");
        const QString source = fixture.filePath(name);
        const QString archive = fixture.filePath("fixture.zip");
        const QString output = fixture.filePath("output");
        writeFile(source, "new archive bytes");
        QVERIFY(QDir().mkpath(output));
        writeFile(QDir(output).filePath(name), "old bytes");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        const auto cancellation =
            connect(&backend, &ArchiveBackend::phaseStarted, &backend, &ArchiveBackend::cancel);
        backend.testSourceArchive(archive, {});
        await(backend, false);
        disconnect(cancellation);
        QCOMPARE(backend.lastResult(), E_ABORT);
        QSignalSpy requested(&backend, &ArchiveBackend::overwriteRequired);
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend,
                [&] { backend.provideOverwriteAnswer(NOverwriteAnswer::kYes); });
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        backend.extract(archive, output, {}, {}, options);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(readFile(QDir(output).filePath(name)), QByteArray("new archive bytes"));
        QCOMPARE(backend.lastResult(), S_OK);
        QVERIFY(!backend.busy());
    }
    void currentFolderValidationUsesOutputRelativePath() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QDir().mkpath(source));
        writeFile(QDir(source).filePath(QStringLiteral("δ.txt")), "archive bytes");
        const QString other = temp.filePath(QStringLiteral("другая папка"));
        QVERIFY(QDir().mkpath(other));
        writeFile(QDir(other).filePath(QStringLiteral("δ.txt")), "other bytes");
        const QString archive = temp.filePath("fixture.zip");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source, other}));
        backend.list(archive);
        await(backend, true);

        const QString output = temp.filePath("output");
        QVERIFY(QDir().mkpath(output + QStringLiteral("/δ.txt")));
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
        options.currentFolder = QStringLiteral("папка с пробелом");
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        QString error;
        QVERIFY(!backend.validateExtraction(output, {QStringLiteral("папка с пробелом/δ.txt")},
                                            options, &error));
        QVERIFY(error.contains(QStringLiteral("δ.txt")));

        error.clear();
        QVERIFY(!backend.validateExtraction(output, {QStringLiteral("другая папка/δ.txt")}, options,
                                            &error));
        QVERIFY(error.contains("outside the current archive folder"));
    }

    void askDecisions_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QStringLiteral("7z");
        QTest::newRow("zip") << QStringLiteral("zip");
    }

    void askDecisions() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("источник с пробелом"));
        const QString output = temp.filePath("output");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(output + QStringLiteral("/источник с пробелом")));
        for (const QString &name : {QStringLiteral("α.txt"), QStringLiteral("β.txt"),
                                    QStringLiteral("γ.txt"), QStringLiteral("δ.txt")}) {
            writeFile(source + "/" + name, "new");
            writeFile(output + QStringLiteral("/источник с пробелом/") + name, "old");
        }
        writeFile(output + QStringLiteral("/источник с пробелом/γ_1.txt"), "occupied rename");
        const QString archive = temp.filePath("fixture." + format);
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);

        QSignalSpy requested(&backend, &ArchiveBackend::overwriteRequired);
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend,
                [&backend](const OverwriteQuestion &question) {
                    backend.provideOverwriteAnswer(
                        question.Incoming.Path.endsWith(QStringLiteral("α.txt"))
                            ? NOverwriteAnswer::kNo
                        : question.Incoming.Path.endsWith(QStringLiteral("β.txt"))
                            ? NOverwriteAnswer::kYes
                        : question.Incoming.Path.endsWith(QStringLiteral("γ.txt"))
                            ? NOverwriteAnswer::kAutoRenameOnce
                            : NOverwriteAnswer::kNo);
                });
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        backend.extract(archive, output, {}, {}, options);
        await(backend, true);
        QCOMPARE(requested.size(), 4);
        QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/α.txt")),
                 QByteArray("old"));
        QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/β.txt")),
                 QByteArray("new"));
        QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/γ.txt")),
                 QByteArray("old"));
        const QStringList names = QDir(output + QStringLiteral("/источник с пробелом"))
                                      .entryList(QDir::Files | QDir::NoDotAndDotDot);
        QCOMPARE(names.size(), 6);
        QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/γ_1.txt")),
                 QByteArray("occupied rename"));
        for (const QString &name : names)
            if (name != QStringLiteral("α.txt") && name != QStringLiteral("β.txt") &&
                name != QStringLiteral("γ.txt") && name != QStringLiteral("δ.txt") &&
                name != QStringLiteral("γ_1.txt"))
                QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/") + name),
                         QByteArray("new"));

        QCOMPARE(readFile(output + QStringLiteral("/источник с пробелом/δ.txt")),
                 QByteArray("old"));
        const QString flat = temp.filePath("flat output");
        QVERIFY(QDir().mkpath(flat));
        writeFile(flat + QStringLiteral("/β.txt"), "flat old");
        ExtractionOptions flatOptions;
        flatOptions.pathMode = ExtractionOptions::PathMode::NoPaths;
        flatOptions.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        backend.extract(archive, flat, {QStringLiteral("источник с пробелом/β.txt")}, {},
                        flatOptions);
        await(backend, true);
        QCOMPARE(readFile(flat + QStringLiteral("/β.txt")), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(flat + QStringLiteral("/α.txt")));
    }

    void askAllAndCancel() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source");
        QVERIFY(QDir().mkpath(source));
        for (const QString &name : {QStringLiteral("one"), QStringLiteral("two")})
            writeFile(source + "/" + name, "new");
        const QString archive = temp.filePath("fixture.7z");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        for (const int answer : {NOverwriteAnswer::kNoToAll, NOverwriteAnswer::kYesToAll,
                                 NOverwriteAnswer::kAutoRename, NOverwriteAnswer::kCancel}) {
            const QString output = temp.filePath(QStringLiteral("output-%1").arg(answer));
            QVERIFY(QDir().mkpath(output + "/source"));
            writeFile(output + "/source/one", "old");
            writeFile(output + "/source/two", "old");
            QSignalSpy requested(&backend, &ArchiveBackend::overwriteRequired);
            QSignalSpy finished(&backend, &ArchiveBackend::finished);
            const auto connection =
                connect(&backend, &ArchiveBackend::overwriteRequired, &backend, [&backend, answer] {
                    if (answer == NOverwriteAnswer::kCancel)
                        backend.cancel();
                    else
                        backend.provideOverwriteAnswer(answer);
                });
            backend.extract(archive, output, {}, {}, options);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
            QCOMPARE(finished.first().first().toBool(), answer != NOverwriteAnswer::kCancel);
            QCOMPARE(requested.size(), 1);
            QCOMPARE(readFile(output + "/source/one"),
                     answer == NOverwriteAnswer::kYesToAll ? QByteArray("new") : QByteArray("old"));
            QCOMPARE(readFile(output + "/source/two"),
                     answer == NOverwriteAnswer::kYesToAll ? QByteArray("new") : QByteArray("old"));
            const QDir directory(output + "/source");
            QStringList copies = directory.entryList(QDir::Files | QDir::Hidden);
            copies.removeAll("one");
            copies.removeAll("two");
            QCOMPARE(copies.size(), answer == NOverwriteAnswer::kAutoRename ? 2 : 0);
            for (const QString &copy : copies)
                QCOMPARE(readFile(directory.filePath(copy)), QByteArray("new"));
            disconnect(connection);
        }
    }

#ifdef Q_OS_WIN
    void lockedReplacementCanBeRetried() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto archive = temp.filePath("replacement.zip");
        const auto output = temp.filePath("output");
        const auto target = QDir(output).filePath(QStringLiteral("a-файл с пробелом.txt"));
        const auto other = QDir(output).filePath("z-other.txt");
        QVERIFY(NativeArchiveFixture::Write(
            archive, {{QFileInfo(target).fileName(), "new bytes"}, {"z-other.txt", "other bytes"}},
            "zip"));
        QVERIFY(QDir().mkpath(output));
        writeFile(target, "old bytes");
        ArchiveBackend backend;
        backend.list(archive);
        await(backend, true);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        {
            const HANDLE locked =
                ::CreateFileW(NativeFixturePath(target), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            QVERIFY(locked != INVALID_HANDLE_VALUE);
            const auto close = qScopeGuard([&] { ::CloseHandle(locked); });
            backend.extract(archive, output, {}, {}, options);
            QTRY_COMPARE(finished.size(), 1);
            const auto failure = finished.takeFirst();
            QVERIFY(!failure[0].toBool());
            const auto details = failure[1].toString();
            QVERIFY2(details.contains("Cannot replace output file") &&
                         details.contains(QFileInfo(target).fileName()),
                     qPrintable(details));
        }
        QCOMPARE(readFile(target), QByteArray("old bytes"));
        QCOMPARE(readFile(other), QByteArray("other bytes"));
        QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden),
                 (QStringList{QFileInfo(target).fileName(), QFileInfo(other).fileName()}));
        backend.extract(archive, output, {}, {}, options);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.takeFirst().first().toBool());
        QCOMPARE(readFile(target), QByteArray("new bytes"));
    }
#endif

    void damagedEntryDoesNotReplaceExistingFile_data() {
        QTest::addColumn<bool>("readOnly");
        QTest::newRow("writable") << false;
        QTest::newRow("read-only") << true;
    }
    void damagedEntryDoesNotReplaceExistingFile() {
        QFETCH(bool, readOnly);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto clean = temp.filePath("clean.zip");
        const auto damaged = temp.filePath("damaged.zip");
        const auto output = temp.filePath("output");
        const auto target = QDir(output).filePath(QStringLiteral("данные.bin"));
        const QByteArray good("another entry");
        const QByteArray incoming("stored replacement bytes");
        QVERIFY(NativeArchiveFixture::Write(clean, {{"a.txt", good}, {"данные.bin", incoming}},
                                            "zip", {}, true, true));
        auto bytes = readFile(clean);
        const auto offset = bytes.indexOf(incoming);
        QVERIFY(offset > 0);
        bytes[offset] = char(bytes.at(offset) ^ 0x55);
        writeFile(damaged, bytes);
        QVERIFY(QDir().mkpath(output));
        writeFile(target, "original bytes");
        if (readOnly)
            QVERIFY(QFile::setPermissions(target, QFile::ReadOwner | QFile::ReadUser));
        const auto permissions = QFile::permissions(target);

        ArchiveBackend backend;
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        backend.list(damaged);
        await(backend, true);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        backend.extract(damaged, output, {}, {}, options);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY(!finished.takeFirst().first().toBool());
        QCOMPARE(readFile(target), QByteArray("original bytes"));
        QCOMPARE(QFile::permissions(target), permissions);
        QCOMPARE(readFile(QDir(output).filePath("a.txt")), good);
        QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden).size(), 2);

        backend.list(clean);
        await(backend, true);
        finished.clear();
        backend.extract(clean, output, {}, {}, options);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY(finished.takeFirst().first().toBool());
        QCOMPARE(readFile(target), incoming);
        QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden).size(), 2);
    }

    void encryptedOverwrite_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QStringLiteral("7z");
        QTest::newRow("zip") << QStringLiteral("zip");
    }

    void encryptedOverwrite() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("secret.txt");
        const QString output = temp.filePath("output");
        QVERIFY(QDir().mkpath(output));
        writeFile(source, "new secret");
        writeFile(output + "/secret.txt", "old secret");
        const QString archive = temp.filePath("encrypted." + format);
        const QString password = QStringLiteral("test-only-password");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, {}, password));
        backend.list(archive, password);
        await(backend, true);
        connect(&backend, &ArchiveBackend::overwriteRequired, &backend,
                [&backend] { backend.provideOverwriteAnswer(NOverwriteAnswer::kYes); });
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        QSignalSpy rejected(&backend, &ArchiveBackend::finished);
        backend.extract(archive, output, {}, QStringLiteral("incorrect-test-password"), options);
        QTRY_COMPARE_WITH_TIMEOUT(rejected.size(), 1, 30000);
        QVERIFY(!rejected.takeFirst().first().toBool());
        // Source ArchiveExtractCallback can leave unverified/partial output on
        // a data-password failure. The backend must report that as failure.
        QVERIFY(backend.lastResult() != S_OK);
        QCOMPARE(readFile(output + "/secret.txt"), QByteArray("old secret"));
        QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden), QStringList{"secret.txt"});
        backend.extract(archive, output, {}, password, options);
        await(backend, true);
        QCOMPARE(readFile(output + "/secret.txt"), QByteArray("new secret"));
    }

    void askWithoutConsumerFailsBeforeEngineWrite() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("file.txt");
        const QString archive = temp.filePath("fixture.7z");
        const QString output = temp.filePath("output");
        QVERIFY(QDir().mkpath(output));
        writeFile(source, "new");
        writeFile(output + "/file.txt", "old");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        backend.extract(archive, output, {}, {}, options);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().first().toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(readFile(output + "/file.txt"), QByteArray("old"));
    }

    void modes_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QStringLiteral("7z");
        QTest::newRow("zip") << QStringLiteral("zip");
    }

    void modes() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("source folder"));
        const QString destination = temp.filePath(QStringLiteral("новая папка/вложенный output"));
        QVERIFY(QDir().mkpath(source + QStringLiteral("/вложено")));
        writeFile(source + QStringLiteral("/вложено/δ file.txt"), "archive version");
        writeFile(source + QStringLiteral("/new.txt"), "new file");
        const QString archive = temp.filePath(QStringLiteral("fixture.") + format);
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);

        ExtractionOptions flat;
        flat.pathMode = ExtractionOptions::PathMode::NoPaths;
        backend.extract(archive, destination, {}, {}, flat);
        await(backend, true);
        QCOMPARE(readFile(destination + QStringLiteral("/δ file.txt")),
                 QByteArray("archive version"));
        QVERIFY(
            !QFileInfo::exists(destination + QStringLiteral("/source folder/вложено/δ file.txt")));

        writeFile(destination + QStringLiteral("/δ file.txt"), "existing version");
        QSignalSpy rejected(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, {}, flat);
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!rejected.takeFirst().at(0).toBool());
        QCOMPARE(readFile(destination + QStringLiteral("/δ file.txt")),
                 QByteArray("existing version"));

        flat.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        QSignalSpy completion(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, {}, flat);
        await(backend, true);
        QCOMPARE(completion.size(), 1);
        QCOMPARE(completion.takeFirst().at(1).toString(),
                 QCoreApplication::translate("ProgressRedesign",
                                             "Completed with skipped items or messages"));
        QCOMPARE(readFile(destination + QStringLiteral("/δ file.txt")),
                 QByteArray("existing version"));

        flat.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        backend.extract(archive, destination, {}, {}, flat);
        await(backend, true);
        QCOMPARE(completion.size(), 1);
        QCOMPARE(completion.takeFirst().at(1).toString(),
                 QCoreApplication::translate("archivebackend_completion", "Extraction completed."));
        QCOMPARE(readFile(destination + QStringLiteral("/δ file.txt")),
                 QByteArray("archive version"));

        const QString full = temp.filePath(QStringLiteral("full paths"));
        QVERIFY(QDir().mkpath(full));
        backend.extract(archive, full);
        await(backend, true);
        QCOMPARE(readFile(full + QStringLiteral("/source folder/вложено/δ file.txt")),
                 QByteArray("archive version"));
    }

    void renameConflicts_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("7z") << QStringLiteral("7z");
        QTest::newRow("zip") << QStringLiteral("zip");
    }

    void renameConflicts() {
        QFETCH(QString, format);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("источник");
        QVERIFY(QDir().mkpath(source));
        writeFile(source + "/δ file.txt", "archive version");
        const QString archive = temp.filePath("fixture." + format);
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);

        const QString relative = QStringLiteral("источник/δ file.txt");
        for (const auto mode : {ExtractionOptions::OverwriteMode::RenameExtracted,
                                ExtractionOptions::OverwriteMode::RenameExisting}) {
            const QString destination = temp.filePath(
                mode == ExtractionOptions::OverwriteMode::RenameExtracted ? "renamed extracted"
                                                                          : "renamed existing");
            QVERIFY(QDir().mkpath(destination + "/источник"));
            writeFile(destination + "/" + relative, "existing version");
            ExtractionOptions options;
            options.overwriteMode = mode;
            backend.extract(archive, destination, {}, {}, options);
            await(backend, true);

            const QByteArray expectedOriginal =
                mode == ExtractionOptions::OverwriteMode::RenameExtracted
                    ? QByteArray("existing version")
                    : QByteArray("archive version");
            QCOMPARE(readFile(destination + "/" + relative), expectedOriginal);
            const QStringList names =
                QDir(destination + "/источник").entryList(QDir::Files | QDir::NoDotAndDotDot);
            QCOMPARE(names.size(), 2);
            for (const QString &name : names) {
                if (name == QStringLiteral("δ file.txt"))
                    continue;
                QCOMPARE(readFile(destination + "/источник/" + name),
                         mode == ExtractionOptions::OverwriteMode::RenameExtracted
                             ? QByteArray("archive version")
                             : QByteArray("existing version"));
            }
        }
    }

    void flatNameCollision() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("sources");
        const QString destination = temp.filePath("output");
        QVERIFY(QDir().mkpath(source + "/first"));
        QVERIFY(QDir().mkpath(source + "/second"));
        QVERIFY(QDir().mkpath(destination));
        writeFile(source + "/first/same.txt", "first");
        writeFile(source + "/second/same.txt", "second");
        const QString archive = temp.filePath("collision.zip");
        ArchiveBackend backend;
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        backend.list(archive);
        await(backend, true);
        ExtractionOptions flat;
        flat.pathMode = ExtractionOptions::PathMode::NoPaths;
        QSignalSpy rejected(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, {}, flat);
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!rejected.takeFirst().at(0).toBool());
        QVERIFY(!QFileInfo::exists(destination + "/same.txt"));

        backend.extract(archive, destination, {"sources/first/same.txt"}, {}, flat);
        await(backend, true);
        QCOMPARE(readFile(destination + "/same.txt"), QByteArray("first"));
    }

    void skipRejectsLinkedDestination() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath("fixture.7z");
        const QString destination = temp.filePath("destination");
        QVERIFY(QDir().mkpath(destination));
        writeFile(temp.filePath("outside.txt"), "outside");
        if (!CreateNativeSymbolicLink(destination + "/file.txt", temp.filePath("outside.txt")))
            QSKIP("This filesystem does not permit test symlinks");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"file.txt", "archive bytes"}}));
        ArchiveBackend backend(nullptr, temp.filePath("missing console"));
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        backend.list(archive);
        await(backend, true);
        ExtractionOptions skip;
        skip.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        QSignalSpy rejected(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, {}, skip);
        QCOMPARE(rejected.size(), 1);
        QVERIFY(!rejected.takeFirst().at(0).toBool());
        QCOMPARE(readFile(temp.filePath("outside.txt")), QByteArray("outside"));
    }

    void cancellation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath("fixture.7z");
        const QString destination = temp.filePath("output");
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(NativeArchiveFixture::Write(archive, {{"file.txt", "archive bytes"}}));
        ArchiveBackend backend(nullptr, temp.filePath("missing console"));
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        backend.list(archive);
        await(backend, true);
        ExtractionOptions flat;
        flat.pathMode = ExtractionOptions::PathMode::NoPaths;
        flat.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        const auto cancel =
            connect(&backend, &ArchiveBackend::phaseStarted, &backend, &ArchiveBackend::cancel);
        QSignalSpy result(&backend, &ArchiveBackend::finished);
        backend.extract(archive, destination, {}, {}, flat);
        QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty(), 3000);
        QVERIFY(!result.takeFirst().at(0).toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(backend.lastResult(), E_ABORT);
        QVERIFY(!QFileInfo::exists(destination + "/file.txt"));
        disconnect(cancel);
        backend.extract(archive, destination, {}, {}, flat);
        await(backend, true);
        QCOMPARE(readFile(destination + "/file.txt"), QByteArray("archive bytes"));
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    ExtractionOptionsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "extraction_options_tests.moc"
