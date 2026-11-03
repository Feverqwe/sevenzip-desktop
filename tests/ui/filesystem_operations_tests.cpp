#include "Common/StringConvert.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/support/filedeleteoperation.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FilesystemOperationsTests : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes = "contents") {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

  private slots:
    void createAndRenameWithoutReplacingExistingData() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        NFsFolder::CFSFolder folder(temp.path());
        const QString nested = QStringLiteral("новая папка/вложенная");
        QCOMPARE(folder.CreateFolder(nested.toStdWString().c_str(), nullptr), S_OK);
        QVERIFY(folder.CreateFolder(nested.toStdWString().c_str(), nullptr) != S_OK);
        const QString name = nested + QStringLiteral("/файл с пробелом.txt");
        QCOMPARE(folder.CreateFile(name.toStdWString().c_str(), nullptr), S_OK);
        QVERIFY(write(temp.filePath(name)));
        QVERIFY(folder.CreateFile(name.toStdWString().c_str(), nullptr) != S_OK);
        QCOMPARE(read(temp.filePath(name)), QByteArray("contents"));
        const QString occupied = nested + "/occupied";
        QVERIFY(write(temp.filePath(occupied), "keep"));
        folder.AddPath(temp.filePath(name));
        QCOMPARE(QDir::fromNativeSeparators(
                     QString::fromWCharArray(fs2us(folder.GetRelPath(folder.Files[0])))),
                 name);
        QVERIFY(!folder.Rename(0, "occupied"));
        QCOMPARE(read(temp.filePath(occupied)), QByteArray("keep"));
        QVERIFY(folder.Rename(0, QStringLiteral("другое имя.txt")));
        QVERIFY(!QFile::exists(temp.filePath(name)));
        QCOMPARE(read(temp.filePath(nested + QStringLiteral("/другое имя.txt"))),
                 QByteArray("contents"));
        QVERIFY(!folder.Rename(1, "invalid index"));
        // GetAbsPath also supports upstream's absolute input.
        QCOMPARE(folder.CreateFile(temp.filePath("absolute").toStdWString().c_str(), nullptr),
                 S_OK);
        QVERIFY(QFile::exists(temp.filePath("absolute")));
    }

    void deletionRemovesHiddenFilesAndEmptyUnicodeDirectories() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("дерево с пробелом/пустая")));
        QVERIFY(write(temp.filePath(QStringLiteral("дерево с пробелом/.hidden"))));
        QVERIFY(write(temp.filePath("standalone")));
        FileDeleteOperation operation(
            temp.path(),
            {temp.filePath(QStringLiteral("дерево с пробелом")), temp.filePath("standalone")});
        QSignalSpy result(&operation, &FileDeleteOperation::result);
        QSignalSpy progress(&operation, &FileDeleteOperation::advanced);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result[0][0].toBool());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress[0][0].toInt(), 0);
        QCOMPARE(progress.last()[0].toInt(), 2);
        QCOMPARE(progress.last()[1].toInt(), 2);
        QCOMPARE(QDir(temp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList());
    }

    void missingLaterItemReportsFailureAfterPartialDeletion() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(write(temp.filePath("first")));
        QVERIFY(write(temp.filePath("last"), "keep"));
        FileDeleteOperation operation(
            temp.path(), {temp.filePath("first"), temp.filePath("missing"), temp.filePath("last")});
        QSignalSpy result(&operation, &FileDeleteOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result[0][0].toBool());
        QVERIFY(result[0][1].toString().contains("missing"));
        QVERIFY(!QFile::exists(temp.filePath("first")));
        QCOMPARE(read(temp.filePath("last")), QByteArray("keep"));
    }

    void cancelBeforeStartAndBetweenItems() {
        for (bool beforeStart : {true, false}) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            QVERIFY(write(temp.filePath("first")));
            QVERIFY(write(temp.filePath("second"), "keep"));
            FileDeleteOperation operation(temp.path(),
                                          {temp.filePath("first"), temp.filePath("second")});
            QSignalSpy result(&operation, &FileDeleteOperation::result);
            if (beforeStart)
                operation.cancel();
            else
                connect(
                    &operation, &FileDeleteOperation::advanced, &operation,
                    [&operation](int completed, int) {
                        if (completed == 1)
                            operation.cancel();
                    },
                    Qt::DirectConnection);
            operation.start();
            QVERIFY(operation.wait(30000));
            QTRY_COMPARE(result.size(), 1);
            QVERIFY(!result[0][0].toBool());
            QCOMPARE(result[0][1].toString(), QStringLiteral("Операция отменена."));
            QCOMPARE(QFile::exists(temp.filePath("first")), beforeStart);
            QCOMPARE(read(temp.filePath("second")), QByteArray("keep"));
        }
    }

    void cancelDuringRecursiveTraversalPreservesRemainingTree() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("дерево/вложенная")));
        const QString file = temp.filePath(QStringLiteral("дерево/вложенная/файл.txt"));
        QVERIFY(write(file, "keep"));
        FileDeleteOperation operation(temp.path(), {temp.filePath(QStringLiteral("дерево"))});
        QSignalSpy result(&operation, &FileDeleteOperation::result);
        connect(
            &operation, &FileDeleteOperation::currentFile, &operation,
            [&operation, file](const QString &path) {
                if (path == file)
                    operation.cancel();
            },
            Qt::DirectConnection);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result[0][0].toBool());
        QCOMPARE(result[0][1].toString(), QStringLiteral("Операция отменена."));
        QCOMPARE(read(file), QByteArray("keep"));
    }

#ifndef Q_OS_WIN
    void nestedLinksAreUnlinkedWithoutTouchingTheirTargets() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("outside"));
        QVERIFY(QDir(temp.path()).mkdir("tree"));
        QVERIFY(write(temp.filePath("outside/keep"), "keep"));
        QVERIFY(QFile::link(temp.filePath("outside"), temp.filePath("tree/directory link")));
        QVERIFY(QFile::link(temp.filePath("outside/keep"), temp.filePath("tree/file link")));
        QVERIFY(QFile::link(temp.filePath("missing"), temp.filePath("tree/dangling link")));
        NFsFolder::CFSFolder folder(temp.path());
        folder.AddPath(temp.filePath("tree/directory link"));
        UInt32 index = 0;
        QVERIFY(folder.Delete(&index, 1));
        QVERIFY(!QFileInfo(temp.filePath("tree/directory link")).isSymLink());
        QCOMPARE(read(temp.filePath("outside/keep")), QByteArray("keep"));
        folder.AddPath(temp.filePath("tree"));
        index = 1;
        QVERIFY(folder.Delete(&index, 1));
        QVERIFY(!QFile::exists(temp.filePath("tree")));
        QCOMPARE(read(temp.filePath("outside/keep")), QByteArray("keep"));
    }
#endif
};

QTEST_GUILESS_MAIN(FilesystemOperationsTests)
#include "filesystem_operations_tests.moc"
