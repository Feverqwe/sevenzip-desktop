#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/TimeUtils.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/transfer/filetransferoperation.h"
#include "platform/support/native_link_fixture.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <stdexcept>

class FileTransferTests : public QObject {
    Q_OBJECT

  private slots:
    void invalidBatchDoesNotAskToOverwrite_data() {
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("duplicateName");
        QTest::newRow("copy-missing-source") << false << false;
        QTest::newRow("move-missing-source") << true << false;
        QTest::newRow("copy-duplicate-target") << false << true;
        QTest::newRow("move-duplicate-target") << true << true;
    }
    void invalidBatchDoesNotAskToOverwrite() {
        QFETCH(bool, move);
        QFETCH(bool, duplicateName);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("имя 空 с пробелом.txt");
        const QString first = fixture.filePath("first/" + name);
        const QString last = fixture.filePath("last/" + name);
        const QString destination = fixture.filePath("destination");
        const QString target = QDir(destination).filePath(name);
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(QDir().mkpath(QFileInfo(first).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(last).absolutePath()));
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(write(first, "first bytes"));
        if (duplicateName)
            QVERIFY(write(last, "last bytes"));
        QVERIFY(write(target, "existing bytes"));
        FileTransferOperation operation({first, last}, destination, move);
        QSignalSpy prompts(&operation, &FileTransferOperation::overwriteRequested);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        connect(
            &operation, &FileTransferOperation::overwriteRequested, &operation,
            [&] { operation.submitOverwriteAnswer(NOverwriteAnswer::kYes); }, Qt::DirectConnection);
        operation.start();
        QVERIFY(operation.wait(30000));
        QCOMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Failure);
        QCOMPARE(prompts.size(), 0);
        for (const auto &expected : {qMakePair(first, QByteArray("first bytes")),
                                     qMakePair(target, QByteArray("existing bytes"))}) {
            QFile file(expected.first);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), expected.second);
        }
        if (duplicateName) {
            QVERIFY(result.first().at(1).toString().contains(target));
            QFile file(last);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("last bytes"));
        } else {
            QVERIFY(!QFileInfo::exists(last));
            QVERIFY(result.first().at(1).toString().contains(last));
        }
        QCOMPARE(
            QDir(destination).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
            QStringList{name});
    }

    void overwriteSnapshotSurvivesQueuedFilesystemChanges_data() {
        QTest::addColumn<bool>("merge");
        QTest::addColumn<bool>("link");
        QTest::newRow("file") << false << false;
        QTest::newRow("merged child") << true << false;
        QTest::newRow("link object") << false << true;
    }
    void overwriteSnapshotSurvivesQueuedFilesystemChanges() {
        QFETCH(bool, merge);
        QFETCH(bool, link);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString destination = fixture.filePath("destination");
        const QString sourceRoot = fixture.filePath(QStringLiteral("источник 空"));
        const QString source = merge ? QDir(sourceRoot).filePath("child.txt") : sourceRoot;
        const QString targetRoot = QDir(destination).filePath(QFileInfo(sourceRoot).fileName());
        const QString target = merge ? QDir(targetRoot).filePath("child.txt") : targetRoot;
        QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   file.write(bytes) == bytes.size();
        };
        const QString payload = link ? fixture.filePath("link payload") : source;
        QVERIFY(write(payload, QByteArray(1024, 's')));
        if (link && !CreateNativeSymbolicLink(source, payload))
            QSKIP("Native symbolic links are unavailable");
        QVERIFY(write(target, "old bytes"));
        NWindows::NFile::NFind::CFileInfo sourceInfo, targetInfo;
        QVERIFY(sourceInfo.Find(us2fs(QDir::toNativeSeparators(source).toStdWString().c_str())));
        QVERIFY(targetInfo.Find(us2fs(QDir::toNativeSeparators(target).toStdWString().c_str())));
        FILETIME sourceTime, targetTime;
        FiTime_To_FILETIME(sourceInfo.MTime, sourceTime);
        FiTime_To_FILETIME(targetInfo.MTime, targetTime);
        FileTransferOperation operation({sourceRoot}, destination, false);
        std::atomic_bool mutated{false};
        bool inspected = false;
        connect(
            &operation, &FileTransferOperation::overwriteRequested, &operation,
            [&] { mutated = write(payload, "changed source") && write(target, "changed target"); },
            Qt::DirectConnection);
        connect(
            &operation, &FileTransferOperation::overwriteRequested, this,
            [&](const OverwriteQuestion &question) {
                const auto cancel = qScopeGuard([&] { operation.cancel(); });
                inspected = true;
                QVERIFY(mutated.load());
                QCOMPARE(
                    QDir::fromNativeSeparators(question.Existing.Path)
                        .normalized(QString::NormalizationForm_C),
                    QDir::fromNativeSeparators(target).normalized(QString::NormalizationForm_C));
                QCOMPARE(
                    QDir::fromNativeSeparators(question.Incoming.Path)
                        .normalized(QString::NormalizationForm_C),
                    QDir::fromNativeSeparators(source).normalized(QString::NormalizationForm_C));
                QCOMPARE(question.Existing.Size, std::optional<UInt64>(targetInfo.Size));
                QCOMPARE(question.Incoming.Size, std::optional<UInt64>(sourceInfo.Size));
                QVERIFY(question.Existing.Modified && question.Incoming.Modified);
                QCOMPARE(question.Existing.Modified->dwLowDateTime, targetTime.dwLowDateTime);
                QCOMPARE(question.Existing.Modified->dwHighDateTime, targetTime.dwHighDateTime);
                QCOMPARE(question.Incoming.Modified->dwLowDateTime, sourceTime.dwLowDateTime);
                QCOMPARE(question.Incoming.Modified->dwHighDateTime, sourceTime.dwHighDateTime);
            },
            Qt::QueuedConnection);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(operation.wait(30000));
        QVERIFY(inspected);
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Canceled);
        for (const auto &item : {qMakePair(payload, QByteArray("changed source")),
                                 qMakePair(target, QByteArray("changed target"))}) {
            QFile file(item.first);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), item.second);
        }
        QCOMPARE(QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 1);
    }
    void serviceExceptionPreservesMoveData_data() {
        QTest::addColumn<bool>("duringDeletion");
        QTest::newRow("staging") << false;
        QTest::newRow("source-deletion") << true;
    }

    void serviceExceptionPreservesMoveData() {
        QFETCH(bool, duringDeletion);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString destination = temp.filePath(QStringLiteral("назначение"));
        QVERIFY(QDir().mkpath(destination));
        const QString first = temp.filePath(QStringLiteral("первый файл.txt"));
        const QString second = temp.filePath(QStringLiteral("второй файл.txt"));
        const QString target = QDir(destination).filePath(QFileInfo(first).fileName());
        for (const QString &path : {first, second, target}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(path.toUtf8()), qint64(path.toUtf8().size()));
        }
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [second, duringDeletion](const QString &source,
                                                           const QString &stage) {
            if (duringDeletion)
                return false;
            if (source == second)
                throw std::runtime_error("source rename service failed");
            return QDir().rename(source, stage);
        };
        adapter.removeSource = [](const QString &source) -> bool {
            if (!QFile::remove(source))
                return false;
            throw std::runtime_error("source deletion service failed after removing a file");
        };
        FileTransferOperation move({first, second}, destination, true, nullptr, adapter);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move]() { move.submitOverwriteAnswer(NOverwriteAnswer::kYes); },
            Qt::DirectConnection);
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(!result.first().at(1).toString().isEmpty());
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        for (const QString &source : {first, second}) {
            QFile surviving(duringDeletion && source == first
                                ? QDir(destination).filePath(QFileInfo(source).fileName())
                                : source);
            QVERIFY(surviving.open(QIODevice::ReadOnly));
            QCOMPARE(surviving.readAll(), source.toUtf8());
        }
        if (!duringDeletion) {
            QFile previous(target);
            QVERIFY(previous.open(QIODevice::ReadOnly));
            QCOMPARE(previous.readAll(), target.toUtf8());
            QCOMPARE(QDir(destination).entryList(QDir::Files | QDir::Hidden).size(), 1);
        } else {
            QVERIFY(!QFileInfo::exists(first));
            QVERIFY(QFileInfo::exists(second));
        }
    }

    void crossVolumeMovePublishesBeforeDeletingSource() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("вложенная папка")));
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("назначение")));
        const QString file = temp.filePath(QStringLiteral("δ файл с пробелом.txt"));
        const QString folder = temp.filePath(QStringLiteral("вложенная папка"));
        QFile input(file);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("file bytes"), qint64(10));
        input.close();
        QFile nested(QDir(folder).filePath(QStringLiteral("внутри.txt")));
        QVERIFY(nested.open(QIODevice::WriteOnly));
        QCOMPARE(nested.write("nested bytes"), qint64(12));
        nested.close();
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        const QString destination = temp.filePath(QStringLiteral("назначение"));
        const QString occupied = QDir(destination).filePath(QFileInfo(file).fileName());
        QFile previous(occupied);
        QVERIFY(previous.open(QIODevice::WriteOnly));
        QCOMPARE(previous.write("old bytes"), qint64(9));
        previous.close();
        FileTransferOperation move({file, folder}, destination, true, nullptr, adapter);
        QSignalSpy overwrite(&move, &FileTransferOperation::overwriteRequested);
        QSignalSpy result(&move, &FileTransferOperation::result);
        bool oldPreservedAtPrompt = false;
        bool sourcePresentAtPrompt = false;
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move, file, occupied, &oldPreservedAtPrompt, &sourcePresentAtPrompt]() {
                QFile old(occupied);
                oldPreservedAtPrompt =
                    old.open(QIODevice::ReadOnly) && old.readAll() == QByteArray("old bytes");
                sourcePresentAtPrompt = QFileInfo::exists(file);
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(overwrite.size(), 1);
        QVERIFY(oldPreservedAtPrompt);
        QVERIFY(sourcePresentAtPrompt);
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(folder));
        QFile movedFile(QDir(destination).filePath(QFileInfo(file).fileName()));
        QVERIFY(movedFile.open(QIODevice::ReadOnly));
        QCOMPARE(movedFile.readAll(), QByteArray("file bytes"));
        QFile movedNested(QDir(destination).filePath(QStringLiteral("вложенная папка/внутри.txt")));
        QVERIFY(movedNested.open(QIODevice::ReadOnly));
        QCOMPARE(movedNested.readAll(), QByteArray("nested bytes"));
    }

    void crossVolumeDeleteFailureReportsPartialMove() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString first = temp.filePath("first.txt");
        const QString second = temp.filePath("second.txt");
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(path.toUtf8()), qint64(path.toUtf8().size()));
        }
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        adapter.removeSource = [first](const QString &path) {
            return QFileInfo(path).fileName() == QFileInfo(first).fileName() ? QFile::remove(path)
                                                                             : false;
        };
        const QString destination = temp.filePath("destination");
        FileTransferOperation move({first, second}, destination, true, nullptr, adapter);
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(result.first().at(1).toString().contains(QStringLiteral("Could not delete")));
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(!QFileInfo::exists(first));
        QVERIFY(QFileInfo::exists(second));
        for (const QString &path : {first, second}) {
            QFile published(QDir(destination).filePath(QFileInfo(path).fileName()));
            QVERIFY(published.open(QIODevice::ReadOnly));
            QCOMPARE(published.readAll(), path.toUtf8());
        }
    }

    void mixedMoveFailurePreservesConsumedSources_data() {
        QTest::addColumn<bool>("cancelAfterRemoval");
        QTest::newRow("delete failure") << false;
        QTest::newRow("cancel after deletion") << true;
    }

    void mixedMoveFailurePreservesConsumedSources() {
        QFETCH(bool, cancelAfterRemoval);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString destination = temp.filePath(QStringLiteral("назначение 空"));
        QVERIFY(QDir().mkpath(destination));
        QStringList sources;
        for (int i = 0; i < 4; ++i) {
            const QString source = temp.filePath(QStringLiteral("файл %1.txt").arg(i));
            sources << source;
            QFile incoming(source);
            QVERIFY(incoming.open(QIODevice::WriteOnly));
            QCOMPARE(incoming.write(source.toUtf8()), qint64(source.toUtf8().size()));
            QFile previous(QDir(destination).filePath(QFileInfo(source).fileName()));
            QVERIFY(previous.open(QIODevice::WriteOnly));
            QCOMPARE(previous.write("previous"), qint64(8));
        }
        FileTransferOperation *operation = nullptr;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [sources](const QString &source, const QString &stage) {
            return (source == sources.front() || source == sources.back()) &&
                   QDir().rename(source, stage);
        };
        QStringList removed;
        adapter.removeSource = [&](const QString &source) {
            removed << QFileInfo(source).fileName();
            if (!QFile::remove(source))
                return false;
            if (cancelAfterRemoval)
                operation->cancel();
            return cancelAfterRemoval;
        };
        FileTransferOperation move(sources, destination, true, nullptr, adapter);
        operation = &move;
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move] { move.submitOverwriteAnswer(NOverwriteAnswer::kYesToAll); },
            Qt::DirectConnection);
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QCOMPARE(removed, QStringList{QFileInfo(sources[1]).fileName()});
        for (int i = 0; i < sources.size(); ++i) {
            const bool untouched = i == 2;
            QCOMPARE(QFileInfo::exists(sources[i]), untouched);
            if (untouched) {
                QFile original(sources[i]);
                QVERIFY(original.open(QIODevice::ReadOnly));
                QCOMPARE(original.readAll(), sources[i].toUtf8());
            }
            QFile published(QDir(destination).filePath(QFileInfo(sources[i]).fileName()));
            QVERIFY(published.open(QIODevice::ReadOnly));
            QCOMPARE(published.readAll(), untouched ? QByteArray("previous") : sources[i].toUtf8());
        }
        QCOMPARE(QDir(destination)
                     .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
                     .size(),
                 sources.size());
    }

    void crossVolumeCancelBeforeDeleteRollsBackPublishedTargets() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString first = temp.filePath("first.txt");
        const QString second = temp.filePath("second.txt");
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("keep"), qint64(4));
        }
        FileTransferOperation *operation = nullptr;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [&operation, second](const QString &source, const QString &) {
            if (source == second)
                operation->cancel();
            return false;
        };
        const QString destination = temp.filePath("destination");
        FileTransferOperation move({first, second}, destination, true, nullptr, adapter);
        operation = &move;
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(QFileInfo::exists(first));
        QVERIFY(QFileInfo::exists(second));
        QCOMPARE(QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
    }

    void conflictAnswersPreserveAndReplace() {
        for (const int answer :
             {NOverwriteAnswer::kNo, NOverwriteAnswer::kYes, NOverwriteAnswer::kAutoRename}) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            QVERIFY(QDir(temp.path()).mkdir("destination"));
            const QString source = temp.filePath(QStringLiteral("длинное имя с пробелом.txt"));
            const QString target =
                temp.filePath(QStringLiteral("destination/длинное имя с пробелом.txt"));
            QFile incoming(source);
            QVERIFY(incoming.open(QIODevice::WriteOnly));
            QCOMPARE(incoming.write("new"), 3);
            incoming.close();
            QFile existing(target);
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write("old"), 3);
            existing.close();
            FileTransferOperation copy({source}, temp.filePath("destination"), false);
            QSignalSpy requested(&copy, &FileTransferOperation::overwriteRequested);
            QSignalSpy result(&copy, &FileTransferOperation::result);
            QSignalSpy advanced(&copy, &FileTransferOperation::advanced);
            connect(
                &copy, &FileTransferOperation::overwriteRequested, &copy,
                [&copy, answer]() { copy.submitOverwriteAnswer(answer); }, Qt::DirectConnection);
            copy.start();
            QVERIFY(copy.wait(30000));
            QTRY_COMPARE(requested.size(), 1);
            QTRY_COMPARE(result.size(), 1);
            QVERIFY(result.first().first().toBool());
            qint64 bytes = 0;
            for (const auto &event : advanced)
                bytes += event.first().toLongLong();
            QCOMPARE(bytes, answer == NOverwriteAnswer::kNo ? qint64(0) : qint64(3));
            QVERIFY(QFile::exists(source));
            QFile contents(target);
            QVERIFY(contents.open(QIODevice::ReadOnly));
            QCOMPARE(contents.readAll(),
                     answer == NOverwriteAnswer::kYes ? QByteArray("new") : QByteArray("old"));
            if (answer == NOverwriteAnswer::kAutoRename) {
                QFile renamed(
                    temp.filePath(QStringLiteral("destination/длинное имя с пробелом_1.txt")));
                QVERIFY(renamed.open(QIODevice::ReadOnly));
                QCOMPARE(renamed.readAll(), QByteArray("new"));
            }
        }
    }

    void repeatedDecisionAndRollback() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        QStringList sources;
        for (const QString &name : {QStringLiteral("first"), QStringLiteral("second")}) {
            const QString source = temp.filePath(name);
            const QString target = temp.filePath("destination/" + name);
            QFile incoming(source);
            QVERIFY(incoming.open(QIODevice::WriteOnly));
            incoming.write("new");
            incoming.close();
            QFile existing(target);
            QVERIFY(existing.open(QIODevice::WriteOnly));
            existing.write("old");
            existing.close();
            sources << source;
        }
        FileTransferOperation move(sources, temp.filePath("destination"), true);
        QSignalSpy requested(&move, &FileTransferOperation::overwriteRequested);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move]() { move.submitOverwriteAnswer(NOverwriteAnswer::kYesToAll); },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(requested.size(), 1);
        for (const QString &source : sources) {
            QVERIFY(!QFile::exists(source));
            QFile target(temp.filePath("destination/" + QFileInfo(source).fileName()));
            QVERIFY(target.open(QIODevice::ReadOnly));
            QCOMPARE(target.readAll(), QByteArray("new"));
        }

        // A later cancellation restores both the replaced destination and the moved source.
        QFile restoredSource(sources.first());
        QVERIFY(restoredSource.open(QIODevice::WriteOnly));
        restoredSource.write("again");
        restoredSource.close();
        QFile secondSource(sources.last());
        QVERIFY(secondSource.open(QIODevice::WriteOnly));
        secondSource.write("second again");
        secondSource.close();
        FileTransferOperation canceled({sources.first(), sources.last()},
                                       temp.filePath("destination"), true);
        QSignalSpy canceledResult(&canceled, &FileTransferOperation::result);
        connect(
            &canceled, &FileTransferOperation::overwriteRequested, &canceled,
            [&canceled](const OverwriteQuestion &question) {
                if (question.Existing.Path.endsWith("first"))
                    canceled.submitOverwriteAnswer(NOverwriteAnswer::kYes);
                else
                    canceled.cancel();
            },
            Qt::DirectConnection);
        canceled.start();
        QVERIFY(canceled.wait(30000));
        QTRY_COMPARE(canceledResult.size(), 1);
        QVERIFY(!canceledResult.first().first().toBool());
        QFile originalTarget(temp.filePath("destination/first"));
        QVERIFY(originalTarget.open(QIODevice::ReadOnly));
        QCOMPARE(originalTarget.readAll(), QByteArray("new"));
        QFile originalSource(sources.first());
        QVERIFY(originalSource.open(QIODevice::ReadOnly));
        QCOMPARE(originalSource.readAll(), QByteArray("again"));
    }

    void changedFileInvalidatesReplace_data() {
        QTest::addColumn<bool>("duringStaging");
        QTest::addColumn<bool>("incoming");
        QTest::addColumn<bool>("move");
        QTest::newRow("destination-replaced-while-asking") << false << false << true;
        QTest::newRow("destination-edited-while-staging") << true << false << true;
        QTest::newRow("incoming-replaced-before-move") << false << true << true;
        QTest::newRow("incoming-replaced-before-copy") << false << true << false;
    }
    void changedFileInvalidatesReplace() {
        QFETCH(bool, duringStaging);
        QFETCH(bool, incoming);
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("конфликт 空.txt");
        const QString source = fixture.filePath(name);
        const QString output = fixture.filePath("output");
        const QString target = QDir(output).filePath(name);
        const QString changedPath = incoming ? source : target;
        const QString previous = fixture.filePath("previous file.txt");
        QVERIFY(QDir().mkdir(output));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(source, "incoming bytes"));
        QVERIFY(write(target, "old bytes"));
        bool changed = false;
        FileTransferOperation operation({source}, output, move);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        connect(
            &operation, &FileTransferOperation::overwriteRequested, &operation,
            [&](const OverwriteQuestion &) {
                if (!duringStaging)
                    changed =
                        QFile::rename(changedPath, previous) && write(changedPath, "new bytes");
                operation.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        connect(
            &operation, &FileTransferOperation::currentFile, &operation,
            [&](const QString &) {
                if (duringStaging)
                    changed = write(target, "edited bytes");
            },
            Qt::DirectConnection);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(changed);
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(result.first().at(1).toString().contains(changedPath));
        QCOMPARE(read(source), incoming ? QByteArray("new bytes") : QByteArray("incoming bytes"));
        const QByteArray destinationBytes = incoming        ? QByteArray("old bytes")
                                            : duringStaging ? QByteArray("edited bytes")
                                                            : QByteArray("new bytes");
        QCOMPARE(read(target), destinationBytes);
        if (!duringStaging)
            QCOMPARE(read(previous),
                     incoming ? QByteArray("incoming bytes") : QByteArray("old bytes"));
        QCOMPARE(QDir(output).entryList(QDir::Files), QStringList{name});
    }

    void laterBatchSourceChangedDuringFirstConflict() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkdir(output));
        const QString first = fixture.filePath("first.txt");
        const QString second = fixture.filePath(QStringLiteral("second 空.txt"));
        const QString parked = fixture.filePath("parked.txt");
        const QString target = QDir(output).filePath("first.txt");
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(first, "first original"));
        QVERIFY(write(second, "second original"));
        QVERIFY(write(target, "previous destination"));
        bool changed = false;
        FileTransferOperation move({first, second}, output, true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &) {
                changed = QFile::rename(second, parked) && write(second, "replacement");
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(changed);
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(result.first().at(1).toString().contains(second));
        QCOMPARE(read(first), QByteArray("first original"));
        QCOMPARE(read(second), QByteArray("replacement"));
        QCOMPARE(read(parked), QByteArray("second original"));
        QCOMPARE(read(target), QByteArray("previous destination"));
        QCOMPARE(QDir(output).entryList(QDir::Files), QStringList{"first.txt"});
    }

    void replacedDestinationDirectoryIsNotTouchedDuringRollback() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString output = fixture.filePath("output");
        const QString original = fixture.filePath("renamed output");
        QVERIFY(QDir().mkdir(output));
        const QString first = fixture.filePath("first.txt");
        const QString second = fixture.filePath("second.txt");
        const QString target = QDir(output).filePath("first.txt");
        const QString conflict = QDir(output).filePath("second.txt");
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(first, "first source"));
        QVERIFY(write(second, "second source"));
        QVERIFY(write(conflict, "existing conflict"));
        bool replaced = false;
        FileTransferOperation move({first, second}, output, true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &) {
                replaced = QDir().rename(output, original) && QDir().mkdir(output) &&
                           write(target, "unrelated first") && write(conflict, "unrelated second");
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(replaced);
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(result.first().at(1).toString().contains(output));
        QVERIFY(!QFileInfo::exists(first));
        QCOMPARE(read(second), QByteArray("second source"));
        QCOMPARE(read(target), QByteArray("unrelated first"));
        QCOMPARE(read(conflict), QByteArray("unrelated second"));
        QCOMPARE(read(QDir(original).filePath("first.txt")), QByteArray("first source"));
        QCOMPARE(read(QDir(original).filePath("second.txt")), QByteArray("existing conflict"));
    }

    void changedMergeDestinationKeepsBothTrees_data() {
        QTest::addColumn<bool>("editConflict");
        QTest::newRow("conflicting-file") << true;
        QTest::newRow("unrelated-file") << false;
    }
    void changedMergeDestinationKeepsBothTrees() {
        QFETCH(bool, editConflict);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("folder");
        const QString output = fixture.filePath("output");
        const QString destination = QDir(output).filePath("folder");
        const QString child = QStringLiteral("/nested 空/конфликт.txt");
        const QString sibling = QStringLiteral("/nested 空/сосед.txt");
        QVERIFY(QDir().mkpath(QFileInfo(source + child).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(destination + child).absolutePath()));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(source + child, "incoming bytes"));
        QVERIFY(write(destination + child, "existing bytes"));
        QVERIFY(write(destination + sibling, "unrelated bytes"));
        bool edited = false;
        FileTransferOperation move({source}, output, true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &) {
                edited = write(destination + (editConflict ? child : sibling), "external edit");
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(edited);
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(result.first().at(1).toString().contains(destination));
        QCOMPARE(read(source + child), QByteArray("incoming bytes"));
        QCOMPARE(read(destination + child),
                 editConflict ? QByteArray("external edit") : QByteArray("existing bytes"));
        QCOMPARE(read(destination + sibling),
                 editConflict ? QByteArray("unrelated bytes") : QByteArray("external edit"));
        QCOMPARE(QDir(output).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"folder"});
    }

    void backupEditedWhileMovingOtherItemsIsKept_data() {
        QTest::addColumn<bool>("sourceRemovalFails");
        QTest::newRow("completed-batch") << false;
        QTest::newRow("failed-source-removal") << true;
    }
    void backupEditedWhileMovingOtherItemsIsKept() {
#ifdef Q_OS_WIN
        QSKIP("This fixture keeps a Qt file handle open across POSIX rename");
#else
        QFETCH(bool, sourceRemovalFails);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString destination = fixture.filePath("output");
        QVERIFY(QDir().mkdir(destination));
        const QString first = fixture.filePath("first.txt");
        const QString second = fixture.filePath("second.txt");
        const QString target = QDir(destination).filePath("first.txt");
        const QString secondTarget = QDir(destination).filePath("second.txt");
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(first, "incoming first"));
        QVERIFY(write(second, "incoming second"));
        QVERIFY(write(target, "old first"));
        QVERIFY(write(secondTarget, "old second"));
        QFile editor(target);
        QVERIFY(editor.open(QIODevice::ReadWrite | QIODevice::Append));
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [&](const QString &source, const QString &stage) {
            return source == first && QFile::rename(source, stage);
        };
        adapter.removeSource = [&](const QString &source) {
            return !sourceRemovalFails && QFile::remove(source);
        };
        FileTransferOperation move({first, second}, destination, true, nullptr, adapter);
        QSignalSpy result(&move, &FileTransferOperation::result);
        bool edited = false;
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &question) {
                if (question.Existing.Path == secondTarget)
                    edited = editor.write(" external edit") == 14 && editor.flush();
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        editor.close();
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(edited);
        QCOMPARE(move.resultKind(), sourceRemovalFails
                                        ? FileTransferOperation::ResultKind::Failure
                                        : FileTransferOperation::ResultKind::PartialSuccess);
        QCOMPARE(read(target), QByteArray("incoming first"));
        QCOMPARE(read(secondTarget), QByteArray("incoming second"));
        QVERIFY(!QFileInfo::exists(first));
        QCOMPARE(QFileInfo::exists(second), sourceRemovalFails);
        const auto backups =
            QDir(destination).entryList({QStringLiteral("*.sevenzip-*-backup")}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        const QString backup = QDir(destination).filePath(backups.first());
        QCOMPARE(read(backup), QByteArray("old first external edit"));
        QVERIFY(result.first().at(1).toString().contains(backup));
#endif
    }

    void changedSourceAfterCopyIsKept_data() {
        QTest::addColumn<bool>("merge");
        QTest::newRow("file") << false;
        QTest::newRow("merged-child") << true;
    }
    void changedSourceAfterCopyIsKept() {
        QFETCH(bool, merge);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString output = fixture.filePath("output");
        const QString source = fixture.filePath(merge ? "folder" : "first.txt");
        const QString target = QDir(output).filePath(QFileInfo(source).fileName());
        const QString firstFile = merge ? source + "/nested/first.txt" : source;
        const QString published = merge ? target + "/nested/first.txt" : target;
        const QString second = fixture.filePath("second.txt");
        const QString conflict = QDir(output).filePath("second.txt");
        const QString parked = fixture.filePath("old source.txt");
        QVERIFY(QDir().mkpath(QFileInfo(firstFile).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(published).absolutePath()));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(firstFile, "first original"));
        QVERIFY(write(second, "second original"));
        QVERIFY(write(conflict, "second target"));
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        FileTransferOperation move({source, second}, output, true, nullptr, adapter);
        QSignalSpy result(&move, &FileTransferOperation::result);
        bool changed = false;
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &) {
                changed = QFile::rename(firstFile, parked) && write(firstFile, "new source");
                move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(changed);
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QVERIFY(result.first().at(1).toString().contains(source));
        QVERIFY(result.first().at(2).toStringList().isEmpty());
        QCOMPARE(read(firstFile), QByteArray("new source"));
        QCOMPARE(read(parked), QByteArray("first original"));
        QCOMPARE(read(published), QByteArray("first original"));
        QCOMPARE(read(second), QByteArray("second original"));
        QCOMPARE(read(conflict), QByteArray("second target"));
    }

    void fileCreatedAtOriginalPathDuringRemovalIsKept_data() {
        QTest::addColumn<bool>("removalFails");
        QTest::newRow("delete-checked-object") << false;
        QTest::newRow("retain-on-restore-conflict") << true;
    }
    void fileCreatedAtOriginalPathDuringRemovalIsKept() {
        QFETCH(bool, removalFails);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString destination = fixture.filePath("output");
        QVERIFY(QDir().mkdir(destination));
        QFile incoming(source);
        QVERIFY(incoming.open(QIODevice::WriteOnly));
        QCOMPARE(incoming.write("original"), 8);
        incoming.close();
        bool created = false;
        QString checkedPath;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        adapter.removeSource = [&](const QString &checked) {
            checkedPath = checked;
            QFile replacement(source);
            created = replacement.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                      replacement.write("new file") == 8;
            replacement.close();
            return !removalFails && QFile::remove(checked);
        };
        FileTransferOperation move({source}, destination, true, nullptr, adapter);
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(created);
        QCOMPARE(move.resultKind(), removalFails ? FileTransferOperation::ResultKind::Failure
                                                 : FileTransferOperation::ResultKind::Success);
        QFile replacement(source);
        QVERIFY(replacement.open(QIODevice::ReadOnly));
        QCOMPARE(replacement.readAll(), QByteArray("new file"));
        QFile published(QDir(destination).filePath("source.txt"));
        QVERIFY(published.open(QIODevice::ReadOnly));
        QCOMPARE(published.readAll(), QByteArray("original"));
        if (removalFails) {
            QFile retained(checkedPath);
            QVERIFY(retained.open(QIODevice::ReadOnly));
            QCOMPARE(retained.readAll(), QByteArray("original"));
            QVERIFY(result.first().at(1).toString().contains(checkedPath));
        } else {
            QCOMPARE(
                QDir(fixture.path()).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot),
                QStringList{"output"});
        }
    }

    void blockedMoveRollbackReportsRetainedFiles() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkdir(destination));
        const QString first = fixture.filePath(QStringLiteral("01 перенос 空.txt"));
        const QString second = fixture.filePath("02 conflict.txt");
        const QString target = QDir(destination).filePath(QFileInfo(first).fileName());
        const QString secondTarget = QDir(destination).filePath(QFileInfo(second).fileName());
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                   file.write(data) == data.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QVERIFY(write(first, "incoming first"));
        QVERIFY(write(second, "incoming second"));
        QVERIFY(write(target, "old first"));
        QVERIFY(write(secondTarget, "old second"));
        bool recreated = false;
        FileTransferOperation move({first, second}, destination, true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&](const OverwriteQuestion &question) {
                if (question.Existing.Path == target)
                    move.submitOverwriteAnswer(NOverwriteAnswer::kYes);
                else {
                    recreated = write(first, "other application's file");
                    move.cancel();
                }
            },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(recreated);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Failure);
        QCOMPARE(read(first), QByteArray("other application's file"));
        QCOMPARE(read(target), QByteArray("incoming first"));
        QCOMPARE(read(second), QByteArray("incoming second"));
        QCOMPARE(read(secondTarget), QByteArray("old second"));
        const auto backups =
            QDir(destination).entryList({QStringLiteral("*.sevenzip-*-backup")}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        const auto backup = QDir(destination).filePath(backups.first());
        QCOMPARE(read(backup), QByteArray("old first"));
        const auto diagnostic = result.first().at(1).toString();
        QVERIFY(diagnostic.contains(first) && diagnostic.contains(target) &&
                diagnostic.contains(backup));
    }

    void overwriteDecisionAcrossFolderBoundary_data() {
        QTest::addColumn<int>("answer");
        QTest::addColumn<bool>("nestedFirst");
        QTest::addColumn<bool>("move");
        for (const int answer :
             {NOverwriteAnswer::kYes, NOverwriteAnswer::kNo, NOverwriteAnswer::kYesToAll,
              NOverwriteAnswer::kNoToAll, NOverwriteAnswer::kAutoRename,
              NOverwriteAnswer::kAutoRenameOnce, NOverwriteAnswer::kCancel})
            for (bool nestedFirst : {false, true})
                for (bool move : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3")
                                                 .arg(answer)
                                                 .arg(nestedFirst ? "nested-first" : "top-first")
                                                 .arg(move ? "move" : "copy")))
                        << answer << nestedFirst << move;
    }

    void overwriteDecisionAcrossFolderBoundary() {
        QFETCH(int, answer);
        QFETCH(bool, nestedFirst);
        QFETCH(bool, move);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString incoming = temp.filePath("incoming");
        const QString destination = temp.filePath("destination");
        const QString folder = QStringLiteral("вложенная папка");
        const QStringList names{QStringLiteral("верхний файл.txt"),
                                folder + QStringLiteral("/внутренний файл.txt")};
        for (const QString &root : {incoming, destination}) {
            QVERIFY(QDir().mkpath(QDir(root).filePath(folder)));
            for (const QString &name : names) {
                QFile file(QDir(root).filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
                QCOMPARE(file.write(root == incoming ? "new" : "old"), qint64(3));
            }
        }
        QStringList sources{QDir(incoming).filePath(names[0]), QDir(incoming).filePath(folder)};
        if (nestedFirst)
            sources.swapItemsAt(0, 1);
        FileTransferOperation operation(sources, destination, move);
        QStringList prompted;
        connect(
            &operation, &FileTransferOperation::overwriteRequested, &operation,
            [&](const OverwriteQuestion &question) {
                prompted << QDir::fromNativeSeparators(question.Existing.Path)
                                .normalized(QString::NormalizationForm_C);
                int decision = answer;
                if (answer == NOverwriteAnswer::kCancel)
                    decision =
                        prompted.size() == 1 ? NOverwriteAnswer::kYes : NOverwriteAnswer::kCancel;
                else if (prompted.size() == 2)
                    decision = answer == NOverwriteAnswer::kYes ? NOverwriteAnswer::kNo
                                                                : NOverwriteAnswer::kYes;
                operation.submitOverwriteAnswer(decision);
            },
            Qt::DirectConnection);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        const bool canceled = answer == NOverwriteAnswer::kCancel;
        QCOMPARE(result.first().first().toBool(), !canceled);
        QCOMPARE(operation.resultKind(), canceled ? FileTransferOperation::ResultKind::Canceled
                                         : answer != NOverwriteAnswer::kYesToAll &&
                                                 answer != NOverwriteAnswer::kAutoRename &&
                                                 answer != NOverwriteAnswer::kAutoRenameOnce
                                             ? FileTransferOperation::ResultKind::PartialSuccess
                                             : FileTransferOperation::ResultKind::Success);
        const bool all = answer == NOverwriteAnswer::kYesToAll ||
                         answer == NOverwriteAnswer::kNoToAll ||
                         answer == NOverwriteAnswer::kAutoRename;
        QStringList expectedPrompts;
        expectedPrompts << QDir(destination)
                               .filePath(names[nestedFirst ? 1 : 0])
                               .normalized(QString::NormalizationForm_C);
        if (!all)
            expectedPrompts << QDir(destination)
                                   .filePath(names[nestedFirst ? 0 : 1])
                                   .normalized(QString::NormalizationForm_C);
        QCOMPARE(prompted, expectedPrompts);
        for (int i = 0; i < names.size(); ++i) {
            const bool first = i == (nestedFirst ? 1 : 0);
            const bool renamed = answer == NOverwriteAnswer::kAutoRename ||
                                 (answer == NOverwriteAnswer::kAutoRenameOnce && first);
            const bool written =
                !canceled && (renamed || answer == NOverwriteAnswer::kYesToAll ||
                              (answer == NOverwriteAnswer::kYes && first) ||
                              (answer == NOverwriteAnswer::kNo && !first) ||
                              (answer == NOverwriteAnswer::kAutoRenameOnce && !first));
            QFile target(QDir(destination).filePath(names[i]));
            QVERIFY(target.open(QIODevice::ReadOnly));
            QCOMPARE(target.readAll(), written && !renamed ? QByteArray("new") : QByteArray("old"));
            if (renamed) {
                const QFileInfo info(target);
                QFile alternate(info.dir().filePath(info.completeBaseName() + "_1.txt"));
                QVERIFY(alternate.open(QIODevice::ReadOnly));
                QCOMPARE(alternate.readAll(), QByteArray("new"));
            }
            QFile source(QDir(incoming).filePath(names[i]));
            QCOMPARE(source.exists(), !move || !written);
            if (source.exists()) {
                QVERIFY(source.open(QIODevice::ReadOnly));
                QCOMPARE(source.readAll(), QByteArray("new"));
            }
        }
    }

    void directoryConflictMergesAndAsksForNestedFile() {
        for (const int answer : {NOverwriteAnswer::kNo, NOverwriteAnswer::kYes}) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            QVERIFY(QDir(temp.path()).mkpath("incoming/folder"));
            QVERIFY(QDir(temp.path()).mkpath("destination/folder"));
            QFile incoming(temp.filePath("incoming/folder/new"));
            QVERIFY(incoming.open(QIODevice::WriteOnly));
            incoming.write("new");
            incoming.close();
            QFile incomingConflict(temp.filePath("incoming/folder/same"));
            QVERIFY(incomingConflict.open(QIODevice::WriteOnly));
            incomingConflict.write("incoming");
            incomingConflict.close();
            QFile existing(temp.filePath("destination/folder/old"));
            QVERIFY(existing.open(QIODevice::WriteOnly));
            existing.write("old");
            existing.close();
            QFile existingConflict(temp.filePath("destination/folder/same"));
            QVERIFY(existingConflict.open(QIODevice::WriteOnly));
            existingConflict.write("existing");
            existingConflict.close();
            FileTransferOperation copy({temp.filePath("incoming/folder")},
                                       temp.filePath("destination"), false);
            QSignalSpy result(&copy, &FileTransferOperation::result);
            connect(
                &copy, &FileTransferOperation::overwriteRequested, &copy,
                [&copy, answer]() { copy.submitOverwriteAnswer(answer); }, Qt::DirectConnection);
            copy.start();
            QVERIFY(copy.wait(30000));
            QTRY_COMPARE(result.size(), 1);
            QVERIFY(result.first().first().toBool());
            QVERIFY(QFile::exists(temp.filePath("destination/folder/old")));
            QVERIFY(QFile::exists(temp.filePath("destination/folder/new")));
            QFile merged(temp.filePath("destination/folder/same"));
            QVERIFY(merged.open(QIODevice::ReadOnly));
            QCOMPARE(merged.readAll(), answer == NOverwriteAnswer::kYes ? QByteArray("incoming")
                                                                        : QByteArray("existing"));
        }
    }

    void directoryMoveMergeKeepsSkippedSourceFile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath("incoming/folder/sub")));
        QVERIFY(QDir().mkpath(temp.filePath("destination/folder/sub")));
        QFile sourceConflict(temp.filePath("incoming/folder/sub/same"));
        QVERIFY(sourceConflict.open(QIODevice::WriteOnly));
        QCOMPARE(sourceConflict.write("incoming"), qint64(8));
        sourceConflict.close();
        QFile sourceNew(temp.filePath("incoming/folder/sub/new"));
        QVERIFY(sourceNew.open(QIODevice::WriteOnly));
        QCOMPARE(sourceNew.write("new"), qint64(3));
        sourceNew.close();
        QFile targetConflict(temp.filePath("destination/folder/sub/same"));
        QVERIFY(targetConflict.open(QIODevice::WriteOnly));
        QCOMPARE(targetConflict.write("existing"), qint64(8));
        targetConflict.close();
        FileTransferOperation move({temp.filePath("incoming/folder")}, temp.filePath("destination"),
                                   true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move]() { move.submitOverwriteAnswer(NOverwriteAnswer::kNo); }, Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QFile destinationNew(temp.filePath("destination/folder/sub/new"));
        QVERIFY(destinationNew.open(QIODevice::ReadOnly));
        QCOMPARE(destinationNew.readAll(), QByteArray("new"));
        QFile destinationOld(temp.filePath("destination/folder/sub/same"));
        QVERIFY(destinationOld.open(QIODevice::ReadOnly));
        QCOMPARE(destinationOld.readAll(), QByteArray("existing"));
        QVERIFY(!QFileInfo::exists(sourceNew.fileName()));
        QVERIFY(QFileInfo::exists(sourceConflict.fileName()));
    }

    void keepBothReservesTheNamesOfLaterFiles_data() {
        QTest::addColumn<bool>("move");
        QTest::newRow("copy") << false;
        QTest::newRow("move") << true;
    }

    void keepBothReservesTheNamesOfLaterFiles() {
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkdir(destination));
        const QString first = fixture.filePath("report.txt");
        const QString second = fixture.filePath("report_1.txt");
        const QString existing = QDir(destination).filePath("report.txt");
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(first, "incoming"));
        QVERIFY(write(second, "other source"));
        QVERIFY(write(existing, "existing"));
        FileTransferOperation operation({first, second}, destination, move);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        int prompts = 0;
        connect(&operation, &FileTransferOperation::overwriteRequested, &operation, [&] {
            operation.submitOverwriteAnswer(++prompts == 1 ? NOverwriteAnswer::kAutoRenameOnce
                                                           : NOverwriteAnswer::kCancel);
        });
        operation.start();
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(operation.wait(30000));
        QCOMPARE(prompts, 1);
        QVERIFY(result.first().first().toBool());
        for (const auto &[name, expected] :
             {qMakePair("report.txt", "existing"), qMakePair("report_1.txt", "other source"),
              qMakePair("report_2.txt", "incoming")}) {
            QFile file(QDir(destination).filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray(expected));
        }
        QCOMPARE(QFileInfo::exists(first), !move);
        QCOMPARE(QFileInfo::exists(second), !move);
    }

    void directoryMergeAutoRenamesNestedConflict_data() {
        QTest::addColumn<bool>("reserveName");
        QTest::newRow("free-name") << false;
        QTest::newRow("name-in-source-folder") << true;
    }

    void directoryMergeAutoRenamesNestedConflict() {
        QFETCH(bool, reserveName);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath("incoming/folder")));
        QVERIFY(QDir().mkpath(temp.filePath("destination/folder")));
        QFile source(temp.filePath("incoming/folder/same.txt"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("incoming"), qint64(8));
        source.close();
        if (reserveName) {
            QFile other(temp.filePath("incoming/folder/same_1.txt"));
            QVERIFY(other.open(QIODevice::WriteOnly));
            QCOMPARE(other.write("other"), qint64(5));
        }
        QFile target(temp.filePath("destination/folder/same.txt"));
        QVERIFY(target.open(QIODevice::WriteOnly));
        QCOMPARE(target.write("existing"), qint64(8));
        target.close();
        FileTransferOperation copy({temp.filePath("incoming/folder")}, temp.filePath("destination"),
                                   false);
        QSignalSpy result(&copy, &FileTransferOperation::result);
        QSignalSpy prompts(&copy, &FileTransferOperation::overwriteRequested);
        connect(
            &copy, &FileTransferOperation::overwriteRequested, &copy,
            [&copy]() { copy.submitOverwriteAnswer(NOverwriteAnswer::kAutoRenameOnce); },
            Qt::DirectConnection);
        copy.start();
        QVERIFY(copy.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(prompts.size(), 1);
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("existing"));
        const QStringList names =
            QDir(temp.filePath("destination/folder")).entryList(QDir::Files | QDir::NoDotAndDotDot);
        QCOMPARE(names.size(), reserveName ? 3 : 2);
        QFile renamed(temp.filePath(reserveName ? "destination/folder/same_2.txt"
                                                : "destination/folder/same_1.txt"));
        QVERIFY(renamed.open(QIODevice::ReadOnly));
        QCOMPARE(renamed.readAll(), QByteArray("incoming"));
        if (reserveName) {
            QFile other(temp.filePath("destination/folder/same_1.txt"));
            QVERIFY(other.open(QIODevice::ReadOnly));
            QCOMPARE(other.readAll(), QByteArray("other"));
        }
    }

    void directoryMergeCancelRestoresOriginalTree() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath("incoming/folder")));
        QVERIFY(QDir().mkpath(temp.filePath("destination/folder")));
        QFile source(temp.filePath("incoming/folder/same"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("new"), qint64(3));
        source.close();
        QFile target(temp.filePath("destination/folder/same"));
        QVERIFY(target.open(QIODevice::WriteOnly));
        QCOMPARE(target.write("old"), qint64(3));
        target.close();
        FileTransferOperation move({temp.filePath("incoming/folder")}, temp.filePath("destination"),
                                   true);
        QSignalSpy result(&move, &FileTransferOperation::result);
        connect(
            &move, &FileTransferOperation::overwriteRequested, &move,
            [&move] { move.submitOverwriteAnswer(NOverwriteAnswer::kCancel); },
            Qt::DirectConnection);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(move.resultKind(), FileTransferOperation::ResultKind::Canceled);
        QCOMPARE(result.first().at(1).toString(), QStringLiteral("Operation canceled."));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("new"));
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("old"));
        QCOMPARE(
            QDir(temp.filePath("destination")).entryList(QDir::NoDotAndDotDot | QDir::AllEntries),
            QStringList({QStringLiteral("folder")}));
    }

    void directoryMergeDeleteFailureRetainsPublishedBytes() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath("incoming/folder")));
        QVERIFY(QDir().mkpath(temp.filePath("destination/folder")));
        QFile source(temp.filePath("incoming/folder/new"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("new bytes"), qint64(9));
        source.close();
        QFile original(temp.filePath("destination/folder/old"));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("old bytes"), qint64(9));
        original.close();
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.removeSource = [](const QString &) { return false; };
        FileTransferOperation move({temp.filePath("incoming/folder")}, temp.filePath("destination"),
                                   true, nullptr, adapter);
        QSignalSpy result(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(QFileInfo::exists(source.fileName()));
        QVERIFY(QFileInfo::exists(temp.filePath("destination/folder/new")));
        QVERIFY(QFileInfo::exists(original.fileName()));
    }

    void noToAllAndCancelDuringPromptLeaveSourcesUntouched_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("queued");
        QTest::addColumn<bool>("move");
        QTest::newRow("skip-before-wait") << false << false << false;
        QTest::newRow("cancel-before-wait") << true << false << false;
        QTest::newRow("skip-from-gui") << false << true << false;
        QTest::newRow("cancel-from-gui") << true << true << false;
        QTest::newRow("skip-move-from-gui") << false << true << true;
    }

    void noToAllAndCancelDuringPromptLeaveSourcesUntouched() {
        QFETCH(bool, cancel);
        QFETCH(bool, queued);
        QFETCH(bool, move);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        QStringList sources;
        for (const QString &name : {QStringLiteral("first"), QStringLiteral("second")}) {
            QFile source(temp.filePath(name));
            QVERIFY(source.open(QIODevice::WriteOnly));
            source.write("new");
            source.close();
            QFile target(temp.filePath("destination/" + name));
            QVERIFY(target.open(QIODevice::WriteOnly));
            target.write("old");
            target.close();
            sources << source.fileName();
        }
        FileTransferOperation copy(sources, temp.filePath("destination"), move);
        QSignalSpy requested(&copy, &FileTransferOperation::overwriteRequested);
        QSignalSpy result(&copy, &FileTransferOperation::result);
        connect(
            &copy, &FileTransferOperation::overwriteRequested, &copy,
            [&copy, cancel]() {
                if (cancel)
                    copy.cancel();
                else
                    copy.submitOverwriteAnswer(NOverwriteAnswer::kNoToAll);
            },
            queued ? Qt::QueuedConnection : Qt::DirectConnection);
        copy.start();
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(copy.wait(30000));
        QCOMPARE(result.first().first().toBool(), !cancel);
        QCOMPARE(copy.resultKind(), cancel ? FileTransferOperation::ResultKind::Canceled
                                           : FileTransferOperation::ResultKind::PartialSuccess);
        if (!cancel)
            QCOMPARE(result.first().at(1).toString(), QString("All items were skipped."));
        QCOMPARE(requested.size(), 1);
        for (const QString &source : sources) {
            QVERIFY(QFile::exists(source));
            QFile target(temp.filePath("destination/" + QFileInfo(source).fileName()));
            QVERIFY(target.open(QIODevice::ReadOnly));
            QCOMPARE(target.readAll(), QByteArray("old"));
        }
        QCOMPARE(QDir(temp.filePath("destination"))
                     .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                     .size(),
                 2);
    }

    void upstreamFolderCopyCountsHiddenFilesAndEmptyDirectories() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("source/пустая папка")));
        const QByteArray bytes("copied bytes");
        for (const QString &name : {QStringLiteral(".hidden"), QStringLiteral("имя с пробелом")}) {
            QFile file(temp.filePath("source/" + name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        NFsFolder::CCopyState state;
        quint64 completed = 0;
        QStringList paths;
        state.Progress = [&completed](quint64 value) {
            if (value < completed)
                return false;
            completed = value;
            return true;
        };
        state.CurrentFile = [&paths](const QString &path) { paths << path; };
        QVERIFY(NFsFolder::CopyFolder(state, temp.filePath("source"), temp.filePath("target")));
        QCOMPARE(completed, quint64(bytes.size() * 2));
        QCOMPARE(paths.size(), 2);
        QVERIFY(QDir(temp.filePath(QStringLiteral("target/пустая папка"))).exists());
        for (const QString &source : paths) {
            QFile copy(temp.filePath("target/" + QFileInfo(source).fileName()));
            QVERIFY(copy.open(QIODevice::ReadOnly));
            QCOMPARE(copy.readAll(), bytes);
        }
        QVERIFY(!NFsFolder::CopyFolder(state, temp.filePath("source"), temp.filePath("target")));
        QFile existing(temp.filePath("target/.hidden"));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), bytes);
    }

    void canceledTreeCopyRollsBackEarlierSources() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath("tree/empty"));
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        for (const QString &name : {QString("first"), QString("tree/second")}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("bytes"), 5);
        }
        FileTransferOperation copy({temp.filePath("first"), temp.filePath("tree")},
                                   temp.filePath("destination"), false);
        QSignalSpy finished(&copy, &FileTransferOperation::result);
        connect(
            &copy, &FileTransferOperation::currentFile, &copy,
            [&copy](const QString &path) {
                if (path.endsWith("second"))
                    copy.cancel();
            },
            Qt::DirectConnection);
        copy.start();
        QVERIFY(copy.wait(30000));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(
            QDir(temp.filePath("destination")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
            QStringList());
        QVERIFY(QFile::exists(temp.filePath("first")));
        QVERIFY(QFile::exists(temp.filePath("tree/second")));
    }

    void copiesNestedUnicodeAndMovesFile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("source/папка")));
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath(QStringLiteral("source/папка/имя с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("contents"), 8);
        file.close();
        FileTransferOperation copy({temp.filePath("source")}, temp.filePath("destination"), false);
        QSignalSpy copied(&copy, &FileTransferOperation::result);
        copy.start();
        QVERIFY(copy.wait(30000));
        QTRY_COMPARE(copied.size(), 1);
        QVERIFY(copied.first().first().toBool());
        QFile duplicate(
            temp.filePath(QStringLiteral("destination/source/папка/имя с пробелом.txt")));
        QVERIFY(duplicate.open(QIODevice::ReadOnly));
        QCOMPARE(duplicate.readAll(), QByteArray("contents"));
        FileTransferOperation move({source}, temp.filePath("destination"), true);
        QSignalSpy moved(&move, &FileTransferOperation::result);
        move.start();
        QVERIFY(move.wait(30000));
        QTRY_COMPARE(moved.size(), 1);
        QVERIFY(moved.first().first().toBool());
        QVERIFY(!QFile::exists(source));
        QVERIFY(QFile::exists(temp.filePath(QStringLiteral("destination/имя с пробелом.txt"))));
    }

    void mergesOccupiedDirectoryAndRejectsDestinationInsideSource() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath("source/child"));
        const QString source = temp.filePath("source");
        FileTransferOperation recursive({source}, temp.filePath("source/child"), false);
        QSignalSpy rejected(&recursive, &FileTransferOperation::result);
        recursive.start();
        QVERIFY(recursive.wait(30000));
        QTRY_COMPARE(rejected.size(), 1);
        QVERIFY(!rejected.first().first().toBool());
        QVERIFY(!QFile::exists(temp.filePath("source/child/source")));
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        QVERIFY(QDir(temp.path()).mkdir("destination/source"));
        FileTransferOperation collision({source}, temp.filePath("destination"), true);
        QSignalSpy occupied(&collision, &FileTransferOperation::result);
        collision.start();
        QVERIFY(collision.wait(30000));
        QTRY_COMPARE(occupied.size(), 1);
        QVERIFY(occupied.first().first().toBool());
        QVERIFY(!QDir(source).exists());
        QVERIFY(QDir(temp.filePath("destination/source/child")).exists());
    }

    void cancellationLeavesNoPublishedOrStagedFile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath("large file.bin");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(64 * 1024 * 1024));
        file.close();
        FileTransferOperation copy({source}, temp.filePath("destination"), false);
        QSignalSpy finished(&copy, &FileTransferOperation::result);
        connect(
            &copy, &FileTransferOperation::advanced, &copy, [&copy](qint64) { copy.cancel(); },
            Qt::DirectConnection);
        copy.start();
        QVERIFY(copy.wait(30000));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(
            QDir(temp.filePath("destination")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
            QStringList());
        QVERIFY(QFile::exists(source));
    }

#ifndef Q_OS_WIN
    void preservesNestedSymbolicLinkForCopyAndMove() {
        for (bool move : {false, true}) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            QVERIFY(QDir(temp.path()).mkpath("source/child"));
            QVERIFY(QDir(temp.path()).mkdir("destination"));
            const QString source = temp.filePath("source");
            QVERIFY(QFile::link(temp.path(), temp.filePath("source/child/link")));
            FileTransferOperation operation({source}, temp.filePath("destination"), move);
            QSignalSpy finished(&operation, &FileTransferOperation::result);
            operation.start();
            QVERIFY(operation.wait(30000));
            QTRY_COMPARE(finished.size(), 1);
            QVERIFY2(finished.first().first().toBool(), qPrintable(finished.first()[1].toString()));
            QCOMPARE(QDir(source).exists(), !move);
            const QFileInfo copied(temp.filePath("destination/source/child/link"));
            QVERIFY(copied.isSymLink());
            QCOMPARE(copied.symLinkTarget(), temp.path());
        }
    }
#endif
};

QTEST_MAIN(FileTransferTests)
#include "file_transfer_tests.moc"
