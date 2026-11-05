#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/filesystemfolderloader.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "fileops/scanner/filesystemscanner.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class FilesystemScannerTests : public QObject {
    Q_OBJECT
    static void createFiles(const QString &directory, int count) {
        QVERIFY(QDir(directory).mkpath(QStringLiteral("папка с пробелом/sub")));
        for (int i = 0; i < count; ++i) {
            QFile file(QDir(directory).filePath(
                QStringLiteral("папка с пробелом/sub/📦-текущий-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("fixture"), qint64(7));
        }
    }
    static void appendPaths(IFolderFolder *folder, quint32 first, quint32 count,
                            QStringList &paths) {
        QVERIFY(folder);
        UInt32 total = 0;
        QCOMPARE(folder->GetNumberOfItems(&total), S_OK);
        QVERIFY(first <= total && count <= total - first);
        const QString directory = ToQString(GetFolderPath(folder));
        for (quint32 i = first; i < first + count; ++i) {
            NWindows::NCOM::CPropVariant name, prefix;
            QCOMPARE(folder->GetProperty(i, kpidName, &name), S_OK);
            QCOMPARE(name.vt, VARTYPE(VT_BSTR));
            QCOMPARE(folder->GetProperty(i, kpidPrefix, &prefix), S_OK);
            const QString relative =
                (prefix.vt == VT_BSTR ? ToQString(UString(prefix.bstrVal)) : QString()) +
                ToQString(UString(name.bstrVal));
            paths.append(QDir(directory).filePath(QDir::fromNativeSeparators(relative)));
        }
    }
  private slots:
    void publishedFolderKeepsNativeNotificationsAfterWorkerExit_data() {
        QTest::addColumn<bool>("bound");
        QTest::newRow("new-folder") << false;
        QTest::newRow("bound-folder") << true;
    }
    void publishedFolderKeepsNativeNotificationsAfterWorkerExit() {
        QFETCH(bool, bound);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("исходный файл 空.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("before"), qint64(6));
        file.close();
        FilesystemScanner scanner;
        CMyComPtr<IFolderFolder> original;
        if (bound)
            QCOMPARE(CreateFilesystemFolder(fixture.path(), &original), S_OK);
        CMyComPtr<IFolderFolder> retained;
        connect(&scanner, &FilesystemScanner::folderReady, &scanner,
                [&](IFolderFolder *folder) { retained = folder; });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QVERIFY(scanner.start(fixture.path(), false, original));
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(retained);
        if (bound)
            QCOMPARE(retained.Interface(), original.Interface());
        CMyComPtr<IFolderWasChanged> changes;
        QCOMPARE(retained.QueryInterface(IID_IFolderWasChanged, &changes), S_OK);
        QVERIFY(changes);
        Int32 changed = -1;
        QCOMPARE(changes->WasChanged(&changed), S_OK);
        QCOMPARE(changed, Int32(0));
#ifdef Q_OS_WIN
        // The published native owner must still observe subsequent real writes.
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("after"), qint64(5));
        file.close();
        bool observedChange = false;
        HRESULT pollResult = S_OK;
        const auto observeChange = [&] {
            if (!observedChange) {
                pollResult = changes->WasChanged(&changed);
                observedChange = pollResult == S_OK && changed != 0;
            }
            return observedChange;
        };
        // QTRY_VERIFY reevaluates its condition after the successful poll.
        // WasChanged consumes notifications, so retain the observed signal.
        QTRY_VERIFY(observeChange());
        QCOMPARE(pollResult, S_OK);
#endif
    }

    void nonFlatWorkerKeepsNativeDirectChildrenOnly() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        createFiles(fixture.path(), 300);
        FilesystemScanner scanner;
        CMyComPtr<IFolderFolder> retained;
        connect(&scanner, &FilesystemScanner::folderReady, this,
                [&](IFolderFolder *folder) { retained = folder; });
        QStringList paths;
        connect(&scanner, &FilesystemScanner::batchReady, this, [&](quint32 first, quint32 count) {
            QCOMPARE(first, quint32(0));
            appendPaths(retained, first, count, paths);
        });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        scanner.start(fixture.path(), false);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(retained);
        UInt32 count = 0;
        QCOMPARE(retained->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(paths, QStringList{fixture.filePath(QStringLiteral("папка с пробелом"))});
    }
    void completedWorkerHasOnlyLatestProgressWaitingForGui() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        createFiles(fixture.path(), 2048);
        FilesystemScanner scanner;
        QSignalSpy progress(&scanner, &FilesystemScanner::advanced);
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        scanner.start(fixture.path());
        auto *loader = scanner.findChild<FilesystemFolderLoader *>();
        QVERIFY(loader);
        QVERIFY(loader->wait(15000));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
        QVERIFY(!progress.isEmpty());
        QVERIFY(progress.count() <= 2);
        QCOMPARE(progress.last().first().toULongLong(), quint64(2050));
    }
    void sourceOwnershipAndOrderedBatches() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFiles(temp.path(), 300);
        FilesystemScanner scanner;
        CMyComPtr<IFolderFolder> retained;
        QStringList displayedPaths;
        bool ordered = true;
        bool batchesOnGui = true;
        connect(&scanner, &FilesystemScanner::folderReady, this,
                [&](IFolderFolder *folder) { retained = folder; });
        connect(&scanner, &FilesystemScanner::batchReady, this, [&](quint32 first, quint32 count) {
            ordered &= first == quint32(displayedPaths.size());
            batchesOnGui &= QThread::currentThread() == thread();
            appendPaths(retained, first, count, displayedPaths);
        });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        QSignalSpy progress(&scanner, &FilesystemScanner::advanced);
        bool timerRan = false;
        QTimer::singleShot(0, &scanner, [&] { timerRan = true; });
        scanner.start(temp.path());
        QVERIFY(scanner.isRunning());
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(timerRan && ordered && batchesOnGui);
        QVERIFY(failed.isEmpty());
        QVERIFY(!progress.isEmpty());
        QVERIFY(!scanner.isRunning());
        QVERIFY(retained);
        UInt32 count = 0;
        QCOMPARE(retained->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(302));
        QCOMPARE(displayedPaths.size(), qsizetype(count));
        QStringList expectedPaths{temp.filePath(QStringLiteral("папка с пробелом")),
                                  temp.filePath(QStringLiteral("папка с пробелом/sub"))};
        for (int i = 0; i < 300; ++i)
            expectedPaths.append(
                temp.filePath(QStringLiteral("папка с пробелом/sub/📦-текущий-%1.txt").arg(i)));
        for (QString &path : displayedPaths)
            path = QFileInfo(path).canonicalFilePath();
        for (QString &path : expectedPaths)
            path = QFileInfo(path).canonicalFilePath();
        displayedPaths.sort();
        expectedPaths.sort();
        QCOMPARE(displayedPaths, expectedPaths);
        scanner.cancel();
        QCOMPARE(retained->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(302));
    }
    void largeFlatListingYieldsBetweenOrderedBatches() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFiles(temp.path(), 2048);
        FilesystemScanner scanner;
        quint32 expectedIndex = 0;
        bool ordered = true;
        bool eventLoopResumed = false;
        bool yieldedBeforeLastBatch = false;
        bool scheduledHeartbeat = false;
        connect(&scanner, &FilesystemScanner::batchReady, this, [&](quint32 first, quint32 count) {
            ordered &= first == expectedIndex;
            expectedIndex += count;
            if (expectedIndex == 2050)
                yieldedBeforeLastBatch = eventLoopResumed;
            if (!scheduledHeartbeat) {
                scheduledHeartbeat = true;
                QTimer::singleShot(0, &scanner, [&] { eventLoopResumed = true; });
            }
        });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        QElapsedTimer elapsed;
        elapsed.start();
        scanner.start(temp.path());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QVERIFY(failed.isEmpty());
        QVERIFY(ordered);
        QVERIFY(yieldedBeforeLastBatch);
        QCOMPARE(expectedIndex, quint32(2050));
        qInfo("flat source scan/projection: 2050 items in %lld ms", elapsed.elapsed());
        QVERIFY(elapsed.elapsed() < 15000);
    }
    void cancellationDuringPresentationRejectsStaleGeneration() {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid() && second.isValid());
        createFiles(first.path(), 300);
        QFile file(second.filePath("only.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        FilesystemScanner scanner;
        QStringList paths;
        bool restarted = false;
        CMyComPtr<IFolderFolder> retained;
        connect(&scanner, &FilesystemScanner::folderReady, this,
                [&](IFolderFolder *folder) { retained = folder; });
        connect(&scanner, &FilesystemScanner::batchReady, this, [&](quint32 first, quint32 count) {
            if (!restarted) {
                restarted = true;
                scanner.start(second.path());
                return;
            }
            appendPaths(retained, first, count, paths);
        });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        scanner.start(first.path());
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(restarted);
        QCOMPARE(paths, QStringList{second.filePath("only.txt")});
        QVERIFY(failed.isEmpty());
    }
    void cancelBeforeLoadAndDestroyActiveScanner() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFiles(temp.path(), 30);
        FilesystemFolderLoader loader(temp.path());
        loader.cancel();
        loader.start();
        QVERIFY(loader.wait(3000));
        QCOMPARE(loader.result(), E_ABORT);
        QVERIFY(loader.takeFolder() == nullptr);
        auto scanner = std::make_unique<FilesystemScanner>();
        QSignalSpy completed(scanner.get(), &FilesystemScanner::completed);
        QSignalSpy failed(scanner.get(), &FilesystemScanner::failed);
        scanner->start(temp.path());
        scanner.reset();
        QVERIFY(completed.isEmpty() && failed.isEmpty());
    }
    void closeTimeJoinDiscardsAlreadyQueuedCompletionAndAllowsLaterRestart_data() {
        QTest::addColumn<bool>("bound");
        QTest::newRow("new-folder") << false;
        QTest::newRow("bound-folder") << true;
    }
    void closeTimeJoinDiscardsAlreadyQueuedCompletionAndAllowsLaterRestart() {
        QFETCH(bool, bound);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFiles(temp.path(), 30);
        FilesystemScanner scanner;
        CMyComPtr<IFolderFolder> original;
        if (bound)
            QCOMPARE(CreateFilesystemFolder(temp.path(), &original), S_OK);
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        int owners = 0;
        connect(&scanner, &FilesystemScanner::folderReady, this, [&](IFolderFolder *folder) {
            ++owners;
            if (bound)
                QCOMPARE(folder, original.Interface());
        });
        QVERIFY(scanner.start(temp.path(), true, original));
        auto *loader = scanner.findChild<FilesystemFolderLoader *>();
        QVERIFY(loader);
        const QPointer<FilesystemFolderLoader> guard(loader);
        // Block without pumping GUI events: finished is queued at this point.
        QVERIFY(loader->wait(3000));
        scanner.cancelAndWait();
        QVERIFY(!scanner.isRunning());
        QVERIFY(guard.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents();
        QCOMPARE(owners, 0);
        QVERIFY(completed.isEmpty() && failed.isEmpty());
        QVERIFY(scanner.start(temp.path(), true, original));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(owners, 1);
        QVERIFY(failed.isEmpty());
    }
    void restartFromNativeProgressDiscardsOldOwner() {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid() && second.isValid());
        createFiles(first.path(), 300);
        QFile file(second.filePath("only.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        FilesystemScanner scanner;
        bool restarted = false;
        int owners = 0;
        QStringList paths;
        CMyComPtr<IFolderFolder> retained;
        connect(&scanner, &FilesystemScanner::advanced, this, [&](quint64) {
            if (!restarted) {
                restarted = true;
                scanner.start(second.path());
            }
        });
        connect(&scanner, &FilesystemScanner::folderReady, this, [&](IFolderFolder *folder) {
            ++owners;
            retained = folder;
        });
        connect(&scanner, &FilesystemScanner::batchReady, this,
                [&](quint32 first, quint32 count) { appendPaths(retained, first, count, paths); });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        scanner.start(first.path());
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(restarted);
        QCOMPARE(owners, 1);
        QCOMPARE(paths, QStringList{second.filePath("only.txt")});
        QVERIFY(failed.isEmpty());
    }
    void navigationBeforeQueuedCompletionDiscardsOldResult_data() {
        QTest::addColumn<bool>("oldFailure");
        QTest::newRow("old-success") << false;
        QTest::newRow("old-failure") << true;
    }
    void navigationBeforeQueuedCompletionDiscardsOldResult() {
        QFETCH(bool, oldFailure);
        QTemporaryDir first, second;
        QVERIFY(first.isValid() && second.isValid());
        createFiles(first.path(), 30);
        QFile file(second.filePath(QStringLiteral("only 空.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("latest folder"), qint64(13));
        file.close();
        FilesystemScanner scanner;
        CMyComPtr<IFolderFolder> retained;
        QStringList paths;
        int owners = 0;
        connect(&scanner, &FilesystemScanner::folderReady, this, [&](IFolderFolder *folder) {
            ++owners;
            retained = folder;
        });
        connect(&scanner, &FilesystemScanner::batchReady, this,
                [&](quint32 first, quint32 count) { appendPaths(retained, first, count, paths); });
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        QSignalSpy progress(&scanner, &FilesystemScanner::advanced);
        QVERIFY(scanner.start(oldFailure ? first.filePath("missing") : first.path()));
        auto *loader = scanner.findChild<FilesystemFolderLoader *>();
        QVERIFY(loader);
        // Finish the native read without dispatching its queued GUI completion,
        // then navigate. The old success/error must not retire the new listing.
        QVERIFY(loader->wait(3000));
        QVERIFY(scanner.start(second.path()));
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(owners, 1);
        QVERIFY(failed.isEmpty());
        QCOMPARE(paths, QStringList{second.filePath(QStringLiteral("only 空.txt"))});
        QVERIFY(!progress.isEmpty());
        for (const auto &update : progress)
            QCOMPARE(update.first().toULongLong(), quint64(1));
        QVERIFY(!scanner.isRunning());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("latest folder"));
    }
    void invalidDirectoryHasNoPartialListing() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FilesystemScanner scanner;
        QSignalSpy failed(&scanner, &FilesystemScanner::failed);
        QSignalSpy completed(&scanner, &FilesystemScanner::completed);
        QSignalSpy batches(&scanner, &FilesystemScanner::batchReady);
        scanner.start(temp.filePath("missing"));
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(completed.isEmpty() && batches.isEmpty());
        QVERIFY(!scanner.isRunning());
    }
    void receiverCanDestroyScanner_data() {
        QTest::addColumn<bool>("inBatch");
        QTest::newRow("folder-ready") << false;
        QTest::newRow("batch-ready") << true;
    }
    void receiverCanDestroyScanner() {
        QFETCH(bool, inBatch);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFiles(temp.path(), 30);
        auto scanner = std::make_unique<FilesystemScanner>();
        connect(scanner.get(), &FilesystemScanner::folderReady, this, [&](IFolderFolder *) {
            if (!inBatch)
                scanner.reset();
        });
        connect(scanner.get(), &FilesystemScanner::batchReady, this, [&](quint32, quint32) {
            if (inBatch)
                scanner.reset();
        });
        scanner->start(temp.path());
        QTRY_VERIFY(!scanner);
    }
};
QTEST_GUILESS_MAIN(FilesystemScannerTests)
#include "filesystem_scanner_tests.moc"
