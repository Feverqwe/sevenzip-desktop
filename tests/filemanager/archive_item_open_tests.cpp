#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"

#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class ItemOpenBackend final : public ControlledBackend {
  public:
    void list(const QString &, const QString & = {}) override {
        QTimer::singleShot(0, this, [this] {
            ArchiveInfo info;
            info.format = QStringLiteral("7z");
            emit infoReady(info);
            emit entriesReady({ArchiveEntry{"folder/имя с пробелом.txt", 6, 6, {}, false, false}});
            emit finished(true, "Архив открыт.");
        });
    }
    void extractSourceEntry(const QString &, const QString &entry, const QString &output,
                            const QString &, const QString &, bool = false) override {
        extracted = {entry};
        temporary = QFileInfo(output).absolutePath();
        if (cancelExtraction) {
            QTimer::singleShot(0, this, [this] { cancel(); });
            return;
        }
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            QTimer::singleShot(0, this, [this] { fail("Fixture stream creation failed."); });
            return;
        }
        file.write("hello\n");
        file.close();
        QTimer::singleShot(0, this, [this] {
            if (failExtraction)
                fail("Controlled extraction failure.");
            else
                completeExtraction();
        });
    }
    QStringList extracted;
    QString temporary;
    bool cancelExtraction = false;
    bool failExtraction = false;
};

class ArchiveItemOpenTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

    void dismissOperationResult(QTimer &timer) {
        dialogs_.connectResponder(timer, this, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (auto *box = qobject_cast<QMessageBox *>(dialog))
                box->accept();
            else
                TestProgressDialog::DismissVisibleResult(dialog);
        });
        timer.start(1);
    }

  private slots:
    void openFileFromFilesystemAndArchive() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString diskFile = dir.filePath(QStringLiteral("обычный файл.txt"));
        QFile disk(diskFile);
        QVERIFY(disk.open(QIODevice::WriteOnly));
        disk.write("disk");
        disk.close();
        QUrl opened;
        QString extractedDirectory;
        const auto cleanup = qScopeGuard([&] {
            if (!extractedDirectory.isEmpty())
                QDir(extractedDirectory).removeRecursively();
        });
        {
            ItemOpenBackend backend;
            MainWindow window(&backend, [&](const QUrl &url) {
                opened = url;
                return true;
            });
            window.show();
            auto *address = window.findChild<QLineEdit *>("archivePath");
            auto *table = window.findChild<FileTable *>("archiveTable");
            auto *open = window.findChild<QAction *>("openItemAction");
            auto *openOutside = window.findChild<QAction *>("openOutsideAction");
            QVERIFY(address && table && open && openOutside);
            address->setText(dir.path());
            emit address->returnPressed();
            QTRY_COMPARE(table->rowCount(), 1);
            table->selectRow(0);
            QTRY_VERIFY(open->isEnabled());
            open->trigger();
            QCOMPARE(QFileInfo(opened.toLocalFile()).canonicalFilePath(),
                     QFileInfo(diskFile).canonicalFilePath());
            opened = QUrl();
            QVERIFY(backend.prepareArchiveFixture(dir.filePath("test.7z")));
            window.openArchive(dir.filePath("test.7z"));
            QTRY_COMPARE(table->rowCount(), 1);
            emit table->doubleClicked(table->model()->index(0, 0));
            QTRY_COMPARE(table->model()->index(0, 0).data().toString(),
                         QStringLiteral("имя с пробелом.txt"));
            table->selectRow(0);
            QTRY_VERIFY(openOutside->isEnabled());
            openOutside->trigger();
            QTRY_VERIFY(!opened.isEmpty());
            QCOMPARE(backend.extracted.size(), 1);
            QCOMPARE(QDir::fromNativeSeparators(backend.extracted.front()),
                     QStringLiteral("folder/имя с пробелом.txt"));
            // Native CopyTo owns the final file separately from transport scratch storage.
            extractedDirectory = QFileInfo(opened.toLocalFile()).absolutePath();
            QVERIFY(QFileInfo::exists(opened.toLocalFile()));
            QVERIFY(QFileInfo::exists(extractedDirectory));
        }
        // The injected association returns no process handle. Closing the
        // window leaves the copy available to the external application.
        QVERIFY(QFileInfo::exists(opened.toLocalFile()));
    }
    void cancelledArchiveOpenDoesNotLaunchFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ItemOpenBackend backend;
        backend.cancelExtraction = true;
        int launches = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++launches;
            return true;
        });
        QTimer responder;
        dismissOperationResult(responder);
        QVERIFY(backend.prepareArchiveFixture(dir.filePath("test.7z")));
        window.openArchive(dir.filePath("test.7z"));
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *open = window.findChild<QAction *>("openOutsideAction");
        QVERIFY(table && open);
        QTRY_COMPARE(table->rowCount(), 1);
        emit table->doubleClicked(table->model()->index(0, 0));
        QTRY_COMPARE(table->model()->index(0, 0).data().toString(),
                     QStringLiteral("имя с пробелом.txt"));
        table->selectRow(0);
        QTRY_VERIFY(open->isEnabled());
        open->trigger();
        QTRY_VERIFY(!backend.temporary.isEmpty());
        QTRY_VERIFY(!QFileInfo::exists(backend.temporary));
        QCOMPARE(launches, 0);
    }
    void failedArchiveOpenRemovesPartialOutputWithoutLaunching() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ItemOpenBackend backend;
        backend.failExtraction = true;
        int launches = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++launches;
            return true;
        });
        QTimer responder;
        dismissOperationResult(responder);
        const QString archive = dir.filePath("failure.7z");
        QVERIFY(backend.prepareArchiveFixture(archive));
        window.openArchive(archive);
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *open = window.findChild<QAction *>("openOutsideAction");
        QVERIFY(table && open);
        QTRY_COMPARE(table->rowCount(), 1);
        emit table->doubleClicked(table->model()->index(0, 0));
        QTRY_COMPARE(table->model()->index(0, 0).data().toString(),
                     QStringLiteral("имя с пробелом.txt"));
        table->selectRow(0);
        QTRY_VERIFY(open->isEnabled());
        open->trigger();
        QTRY_VERIFY(!backend.temporary.isEmpty());
        QTRY_VERIFY(!QFileInfo::exists(backend.temporary));
        QCOMPARE(launches, 0);
        QVERIFY(QFileInfo::exists(archive));
        QCOMPARE(QFileInfo(archive).size(), qint64(0));
    }
};

QTEST_MAIN(ArchiveItemOpenTests)
#include "archive_item_open_tests.moc"
