#include "archive/callbacks/IFileExtractCallback.h"
#include "fileops/transfer/filetransferoperation.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FileTransferDirectTests : public QObject {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

  private slots:
    void copiesOneUnicodeFileToDirectName() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath(QStringLiteral("исходное имя.txt"));
        const QString target = temp.filePath(QStringLiteral("destination/новое имя.txt"));
        QVERIFY(write(source, "source bytes"));
        FileTransferOperation operation({source}, target, false, nullptr, {},
                                        FileTransferOperation::DestinationKind::DirectPath);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(read(target), QByteArray("source bytes"));
        QCOMPARE(read(source), QByteArray("source bytes"));
    }

    void movesOneDirectoryToDirectName() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath("source/sub")));
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath("source");
        const QString target = temp.filePath(QStringLiteral("destination/переименованная папка"));
        QVERIFY(write(temp.filePath("source/sub/file"), "tree bytes"));
        FileTransferOperation operation({source}, target, true, nullptr, {},
                                        FileTransferOperation::DestinationKind::DirectPath);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QVERIFY(!QFileInfo::exists(source));
        QCOMPARE(read(QDir(target).filePath("sub/file")), QByteArray("tree bytes"));
    }

    void directConflictCanSkipOrReplace() {
        for (const int answer : {NOverwriteAnswer::kNo, NOverwriteAnswer::kYes}) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            const QString source = temp.filePath("source.txt");
            const QString target = temp.filePath("renamed.txt");
            QVERIFY(write(source, "new bytes"));
            QVERIFY(write(target, "old bytes"));
            FileTransferOperation operation({source}, target, false, nullptr, {},
                                            FileTransferOperation::DestinationKind::DirectPath);
            QSignalSpy prompts(&operation, &FileTransferOperation::overwriteRequested);
            QSignalSpy result(&operation, &FileTransferOperation::result);
            connect(
                &operation, &FileTransferOperation::overwriteRequested, &operation,
                [&operation, answer]() { operation.submitOverwriteAnswer(answer); },
                Qt::DirectConnection);
            operation.start();
            QVERIFY(operation.wait(30000));
            QTRY_COMPARE(result.size(), 1);
            QVERIFY(result.first().first().toBool());
            QCOMPARE(prompts.size(), 1);
            QCOMPARE(read(target), answer == NOverwriteAnswer::kYes ? QByteArray("new bytes")
                                                                    : QByteArray("old bytes"));
            QCOMPARE(read(source), QByteArray("new bytes"));
        }
    }

    void selfCopyAndChildTargetAreRejected() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("same.txt");
        QVERIFY(write(source, "keep"));
        FileTransferOperation self({source}, source, false, nullptr, {},
                                   FileTransferOperation::DestinationKind::DirectPath);
        QSignalSpy selfResult(&self, &FileTransferOperation::result);
        self.start();
        QVERIFY(self.wait(30000));
        QTRY_COMPARE(selfResult.size(), 1);
        QVERIFY(!selfResult.first().first().toBool());
        QCOMPARE(selfResult.first().at(1).toString(),
                 QString("Source and destination are the same: %1").arg(source));
        QCOMPARE(read(source), QByteArray("keep"));

        QVERIFY(QDir().mkpath(temp.filePath("folder/sub")));
        const QString folder = temp.filePath("folder");
        const QString child = temp.filePath("folder/sub/copied");
        FileTransferOperation recursive({folder}, child, false, nullptr, {},
                                        FileTransferOperation::DestinationKind::DirectPath);
        QSignalSpy recursiveResult(&recursive, &FileTransferOperation::result);
        recursive.start();
        QVERIFY(recursive.wait(30000));
        QTRY_COMPARE(recursiveResult.size(), 1);
        QVERIFY(!recursiveResult.first().first().toBool());
        QCOMPARE(recursiveResult.first().at(1).toString(),
                 QString("Destination is inside the source folder: %1").arg(child));
        QVERIFY(!QFileInfo::exists(child));
    }

#ifndef Q_OS_WIN
    void destinationLinkIdentifiesTheBlockedPath() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath("source.txt");
        const QString existing = temp.filePath("existing.txt");
        const QString link = temp.filePath("link.txt");
        QVERIFY(write(source, "incoming"));
        QVERIFY(write(existing, "existing"));
        QVERIFY(QFile::link(existing, link));
        FileTransferOperation operation({source}, link, true, nullptr, {},
                                        FileTransferOperation::DestinationKind::DirectPath);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QCOMPARE(result.first().at(1).toString(),
                 QString("Destination is a symbolic link: %1").arg(link));
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(read(existing), QByteArray("existing"));
        QCOMPARE(read(source), QByteArray("incoming"));
    }
#endif
};

QTEST_GUILESS_MAIN(FileTransferDirectTests)
#include "file_transfer_direct_tests.moc"
