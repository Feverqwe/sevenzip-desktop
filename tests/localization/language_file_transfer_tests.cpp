#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/transfer/filetransferoperation.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class LanguageFileTransferTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    void language(bool russian) {
        const QString path = settings.filePath("language.ini");
        SaveRegLang(UString(russian ? "ru" : "-"), path);
        ReloadLang(path);
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

  private slots:
    void cleanup() { language(false); }
    void transfer_data() {
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("move");
        for (bool russian : {false, true})
            for (bool move : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(russian).arg(move)))
                    << russian << move;
    }
    void transfer() {
        QFETCH(bool, russian);
        QFETCH(bool, move);
        language(russian);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("source 密碼 file.txt"));
        const QString destination = directory.filePath(QStringLiteral("destination папка"));
        QVERIFY(QDir().mkdir(destination));
        const QString target = QDir(destination).filePath(QFileInfo(source).fileName());
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original bytes"), qint64(14));
        file.close();
        FileTransferOperation operation({source}, destination, move);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(result.first().at(1).toString(),
                 russian ? (move ? QStringLiteral("Объекты перемещены.")
                                 : QStringLiteral("Объекты скопированы."))
                         : (move ? QString("Items moved.") : QString("Items copied.")));
        QVERIFY(!operation.isCanceled());
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Success);
        QCOMPARE(read(target), QByteArray("original bytes"));
        QCOMPARE(QFile::exists(source), !move);
    }
    void partialMoveCancellationRemainsFailure_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("en") << false;
        QTest::newRow("ru") << true;
    }
    void partialMoveCancellationRemainsFailure() {
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString destination = directory.filePath("destination");
        QVERIFY(QDir().mkdir(destination));
        const QString first = directory.filePath(QStringLiteral("first 密碼 file.txt"));
        const QString second = directory.filePath(QStringLiteral("second папка file.txt"));
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("keep bytes"), qint64(10));
        }
        FileTransferOperation *worker = nullptr;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        adapter.removeSource = [&worker, &first](const QString &path) {
            if (QFileInfo(path).fileName() == QFileInfo(first).fileName())
                return QFile::remove(path);
            worker->cancel();
            return false;
        };
        FileTransferOperation operation({first, second}, destination, true, nullptr, adapter);
        worker = &operation;
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(operation.isCanceled());
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Failure);
        QCOMPARE(result.first().at(1).toString(),
                 (russian ? QStringLiteral("Не удалось удалить источник после копирования: %1")
                          : QString("Could not delete source after copying: %1"))
                         .arg(second) +
                     (russian ? QStringLiteral(" Часть объектов могла быть перемещена.")
                              : QString(" Some items may have been moved.")));
        QVERIFY(!QFile::exists(first));
        QCOMPARE(read(second), QByteArray("keep bytes"));
        for (const QString &path : {first, second})
            QCOMPARE(read(QDir(destination).filePath(QFileInfo(path).fileName())),
                     QByteArray("keep bytes"));
        QCOMPARE(QDir(destination).entryList(QDir::Files | QDir::Hidden).size(), 2);
    }
    void overwriteCancellationAndRealFailure_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("en") << false;
        QTest::newRow("ru") << true;
    }
    void overwriteCancellationAndRealFailure() {
        QFETCH(bool, russian);
        language(russian);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("cancel 密碼 file.txt"));
        const QString destination = directory.filePath("destination");
        QVERIFY(QDir().mkdir(destination));
        const QString target = QDir(destination).filePath(QFileInfo(source).fileName());
        for (const auto &pair :
             {qMakePair(source, QByteArray("new")), qMakePair(target, QByteArray("old"))}) {
            QFile file(pair.first);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(pair.second), qint64(3));
        }
        FileTransferOperation operation({source}, destination, false);
        connect(
            &operation, &FileTransferOperation::overwriteRequested, &operation,
            [&operation]() { operation.submitOverwriteAnswer(NOverwriteAnswer::kCancel); },
            Qt::DirectConnection);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first().first().toBool());
        QVERIFY(operation.isCanceled());
        QCOMPARE(operation.resultKind(), FileTransferOperation::ResultKind::Canceled);
        QCOMPARE(result.first().at(1).toString(),
                 russian ? QStringLiteral("Операция отменена.") : QString("Operation canceled."));
        QCOMPARE(read(source), QByteArray("new"));
        QCOMPARE(read(target), QByteArray("old"));
        QCOMPARE(QDir(destination).entryList(QDir::Files | QDir::Hidden).size(), 1);

        FileTransferOperation failure({source}, source, false);
        QSignalSpy failed(&failure, &FileTransferOperation::result);
        failure.start();
        QVERIFY(failure.wait(30000));
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!failed.first().first().toBool());
        QVERIFY(!failure.isCanceled());
        QCOMPARE(failure.resultKind(), FileTransferOperation::ResultKind::Failure);
        QCOMPARE(failed.first().at(1).toString(),
                 russian ? QStringLiteral("Папка назначения недоступна.")
                         : QString("The destination folder is unavailable."));
        QCOMPARE(read(source), QByteArray("new"));

        const QString missing = directory.filePath(QStringLiteral("missing 密碼 file.txt"));
        NFsFolder::CCopyState state;
        QVERIFY(!NFsFolder::CopyFile_Ask(state, missing, QFileInfo(missing), target));
        QCOMPARE(state.ErrorMessage, (russian ? QStringLiteral("Источник недоступен: %1")
                                              : QString("Source unavailable: %1"))
                                         .arg(missing));
        QCOMPARE(read(target), QByteArray("old"));
    }
};
QTEST_GUILESS_MAIN(LanguageFileTransferTests)
#include "language_file_transfer_tests.moc"
