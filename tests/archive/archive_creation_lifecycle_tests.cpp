#include "Common/StringConvert.h"
#include "archive/backend/archivebackend.h"
#include "archive/operations/EnumDirItems.h"
#include "archive/resources/qtarchivedeleteinputs.h"
#include "archive/support/source_creation_fixture.h"
#include "platform/support/native_link_fixture.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
HRESULT snapshot(CDirItems &items, const QStringList &paths) {
    if (items.SymLinks) {
        NWildcard::CCensor censor;
        for (const QString &path : paths)
            censor.AddPreItem_NoWildcard(path.toStdWString().c_str());
        censor.AddPathsToCensor(NWildcard::k_RelatPath);
        return EnumerateItems(censor, NWildcard::k_RelatPath, UString(), items);
    }
    FStringVector names;
    for (const QString &path : paths)
        names.Add(us2fs(path.toStdWString().c_str()));
    return items.EnumerateItems2(FString(), UString(), names, nullptr);
}
CByteBuffer processedItems(const CDirItems &items, const QStringList &skipped = {}) {
    CByteBuffer processed(items.Items.Size());
    for (unsigned i = 0; i < items.Items.Size(); ++i) {
        const QString path =
            QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(items.GetPhyPath(i)).Ptr()));
        processed[i] = skipped.contains(path) ? 0 : 1;
    }
    return processed;
}
} // namespace

class ArchiveCreationLifecycleTests final : public QObject {
    Q_OBJECT

  private slots:
    void nativeStoredLinksDeleteOnlyProcessedUnchangedInputs() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath("цель с пробелом.txt");
        QVERIFY(writeFile(target, "outside bytes"));
        const QString processed = fixture.filePath("processed link");
        const QString skipped = fixture.filePath("skipped link");
        const QString changed = fixture.filePath("changed link");
        for (const QString &path : {processed, skipped, changed})
            QVERIFY(CreateNativeSymbolicLink(path, target));
        CDirItems items;
        items.SymLinks = true;
        CBoolPair symLinks;
        symLinks.SetTrueTrue();
        QCOMPARE(snapshot(items, {processed, skipped, changed}), S_OK);
        QtArchiveDeleteInputs inputs(items, processedItems(items, {skipped}), symLinks);
        QVERIFY(QFile::remove(changed));
        QVERIFY(writeFile(changed, "replacement file"));
        QtUpdateDeletionCallback callback;
        QCOMPARE(inputs.Delete(callback), S_OK);
        QVERIFY(!QFileInfo(processed).isSymLink());
        QVERIFY(QFileInfo(skipped).isSymLink());
        QCOMPARE(readFile(changed), QByteArray("replacement file"));
        QCOMPARE(readFile(target), QByteArray("outside bytes"));
    }
    void nativeProcessedAndChangedFiles() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString processed = fixture.filePath("processed.txt");
        const QString skipped = fixture.filePath("skipped.txt");
        const QString changed = fixture.filePath("changed.txt");
        for (const QString &path : {processed, skipped, changed})
            QVERIFY(writeFile(path, "original"));
        CDirItems items;
        QCOMPARE(snapshot(items, {processed, skipped, changed}), S_OK);
        QtArchiveDeleteInputs inputs(items, processedItems(items, {skipped}), CBoolPair());
        QVERIFY(writeFile(changed, "changed longer bytes"));
        QtUpdateDeletionCallback callback;
        QCOMPARE(inputs.Delete(callback), S_OK);
        QVERIFY(!QFileInfo::exists(processed));
        QCOMPARE(readFile(skipped), QByteArray("original"));
        QCOMPARE(readFile(changed), QByteArray("changed longer bytes"));
    }
    void nativeTimeChangesPreserveSameSizeFiles() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        QVERIFY(writeFile(source, "original"));
        CDirItems items;
        QCOMPARE(snapshot(items, {source}), S_OK);
        QtArchiveDeleteInputs inputs(items, processedItems(items), CBoolPair());
        QFile file(source);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QFileInfo(source).lastModified().addDays(-1),
                                 QFileDevice::FileModificationTime));
        file.close();
        QtUpdateDeletionCallback callback;
        QCOMPARE(inputs.Delete(callback), S_OK);
        QCOMPARE(readFile(source), QByteArray("original"));
#ifndef Q_OS_WIN
        CDirItems metadataItems;
        QCOMPARE(snapshot(metadataItems, {source}), S_OK);
        QtArchiveDeleteInputs metadata(metadataItems, processedItems(metadataItems), CBoolPair());
        // On POSIX the source CTime is inode metadata time. Changing permission
        // preserves size/mtime, and must prevent deletion of that changed input.
        NWindows::NFile::NFind::CFileInfo changedInfo;
        const auto metadataChanged = [&] {
            if (!QFile::setPermissions(source,
                                       QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup) ||
                !changedInfo.Find(metadataItems.GetPhyPath(0)))
                return false;
            return Compare_FiTime(&changedInfo.CTime, &metadataItems.Items[0].CTime) != 0;
        };
        // Some filesystems coalesce consecutive metadata changes in one tick.
        // Establish a real source CTime change before testing its deletion guard.
        QTRY_VERIFY_WITH_TIMEOUT(metadataChanged(), 2000);
        QCOMPARE(metadata.Delete(callback), S_OK);
        QCOMPARE(readFile(source), QByteArray("original"));
#endif
    }
    void nativeCancellationAndEmptyDirectoryOrder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source folder");
        QVERIFY(QDir().mkpath(root + "/nested/empty"));
        const QString a = root + "/первый.txt", b = root + "/второй.txt";
        QVERIFY(writeFile(a, "a") && writeFile(b, "b"));
        CDirItems cancelledItems;
        QCOMPARE(snapshot(cancelledItems, {a, b}), S_OK);
        QtArchiveDeleteInputs cancelled(cancelledItems, processedItems(cancelledItems),
                                        CBoolPair());
        QtUpdateDeletionCallback stop;
        int calls = 0;
        stop.Progress = [&calls](const FString &, bool) { return ++calls == 2 ? E_ABORT : S_OK; };
        QCOMPARE(cancelled.Delete(stop), E_ABORT);
        QVERIFY(!QFileInfo::exists(a));
        QCOMPARE(readFile(b), QByteArray("b"));
        CDirItems remainingItems;
        QCOMPARE(snapshot(remainingItems, {root}), S_OK);
        QtArchiveDeleteInputs remaining(remainingItems, processedItems(remainingItems),
                                        CBoolPair());
        QtUpdateDeletionCallback callback;
        QStringList deletedDirectories;
        callback.Progress = [&](const FString &path, bool directory) {
            if (directory)
                deletedDirectories.append(
                    QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(path).Ptr())));
            return S_OK;
        };
        QCOMPARE(remaining.Delete(callback), S_OK);
        QVERIFY(!QFileInfo::exists(root));
        QVERIFY(deletedDirectories.indexOf(root + "/nested/empty") <
                deletedDirectories.indexOf(root + "/nested"));
        QCOMPARE(deletedDirectories.last(), root);
    }
    void sourceCreationRemovesArchivedInputs_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("volumes");
        QTest::newRow("zip") << QString("zip") << false;
        QTest::newRow("7z") << QString("7z") << false;
        QTest::newRow("zip-volumes") << QString("zip") << true;
        QTest::newRow("7z-volumes") << QString("7z") << true;
    }
    void sourceCreationRemovesArchivedInputs() {
        QFETCH(QString, format);
        QFETCH(bool, volumes);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source folder");
        QVERIFY(QDir().mkpath(root + "/empty"));
        const QString input = root + "/файл с пробелом.txt";
        const QByteArray bytes(8192, 'x');
        QVERIFY(writeFile(input, bytes));
        const QString archive = fixture.filePath("result." + format);
        CompressionOptions options;
        options.Level = 0;
        options.DeleteAfterCompressing = true;
        if (volumes)
            options.VolumeSizes.Add(2048);
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {root}, {}, false, options), S_OK);
        QVERIFY(!QFileInfo::exists(root));
        const QString result = volumes ? archive + ".001" : archive;
        QVERIFY(QFileInfo::exists(result));
        QProcess engine;
        const QString extracted = fixture.filePath("extracted");
        engine.start("7z", {"x", "-y", "-o" + extracted, "--", result});
        QVERIFY(engine.waitForFinished(15000));
        QCOMPARE(engine.exitCode(), 0);
        QCOMPARE(readFile(extracted + "/source folder/файл с пробелом.txt"), bytes);
    }
    void cancelledNativeCreationPreservesEverySource() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath("a")) && QDir().mkpath(fixture.filePath("b")));
        const QString a = fixture.filePath("a/один.txt"), b = fixture.filePath("b/two.txt");
        QVERIFY(writeFile(a, "a") && writeFile(b, "b"));
        ArchiveBackend backend;
        CompressionOptions options;
        options.DeleteAfterCompressing = true;
        const QString archive = fixture.filePath("result.7z");
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {a, b}, {}, false, options, true),
                 E_ABORT);
        QCOMPARE(readFile(a), QByteArray("a"));
        QCOMPARE(readFile(b), QByteArray("b"));
        QVERIFY(!QFileInfo::exists(archive));
    }
};
QTEST_MAIN(ArchiveCreationLifecycleTests)
#include "archive_creation_lifecycle_tests.moc"
