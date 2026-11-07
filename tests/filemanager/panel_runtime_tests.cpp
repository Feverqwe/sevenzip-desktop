#include "7zip/PropID.h"
#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "fileops/scanner/filesystemscanner.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QSet>
#include <QThread>

using NWindows::NCOM::CPropVariant;

class PanelRuntimeTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static void createFile(const QString &path, const QByteArray &bytes = "fixture") {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    static void navigate(MainWindow &window, const QString &path) {
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
    }
    static int diskRow(MainWindow &window, const QString &path) {
        auto *view = table(window);
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 1).data().toString() == path)
                return row;
        return -1;
    }
    static void selectOnly(FileTable *view, int row) {
        QVERIFY(row >= 0);
        view->clearSelection();
        view->selectionModel()->select(view->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }

  private slots:
    void init() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void archiveMoveSummaryUsesBoundSourceProperties_data() { filesystemArchiveTransitions_data(); }
    void archiveMoveSummaryUsesBoundSourceProperties() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "flatViewAction")->setChecked(false);
        const QString directory = QStringLiteral("папка й");
        const QString file = QStringLiteral("отдельный й.txt");
        openListedArchive(window, backend, "source-summary.zip",
                          {{directory + "/inside", 11, 3}, {file, 7, 2}, {"unknown", -1, -1}});
        action(window, "flatViewAction")->setChecked(flat);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *view = table(window);
        selectOnly(view, rowWithPath(window, directory));
        view->selectionModel()->select(view->model()->index(rowWithPath(window, file), 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QString summary;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("copyDialog");
            QVERIFY(dialog);
            auto *info = dialog->findChild<QWidget *>("copySourceSummary");
            QVERIFY(info);
            summary = ToQString(static_cast<CopyDialog *>(dialog)->Info)
                          .normalized(QString::NormalizationForm_C);
            const QString visibleSummary = info->accessibleDescription();
            QVERIFY(visibleSummary.contains("source-summary.zip"));
            QCOMPARE(visibleSummary.contains("bytes"), !flat);
            if (!flat)
                QVERIFY(visibleSummary.contains("18 bytes"));
            for (const QSize &size : {QSize(440, 250), QSize(340, 220)}) {
                dialog->resize(size);
                QCoreApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*info, *dialog));
                QVERIFY(UiInspection::writeReport(
                    *dialog,
                    QString("copy-source-flat%1-mode%2-%3").arg(flat).arg(mode).arg(size.width())));
            }
            dialog->reject();
        });
        action(window, "moveAction")->trigger();
        const QString expected = flat ? QStringLiteral("Folders: 1\nFiles: 1    ( 7 bytes )\n\n")
                                      : QStringLiteral("Folders: 1    ( 11 bytes )\n"
                                                       "Files: 1    ( 7 bytes )\n"
                                                       "Size: 18 bytes\n\n");
        QVERIFY2(summary.startsWith(expected), qPrintable(summary));
        QVERIFY(QDir::fromNativeSeparators(summary).contains("source-summary.zip/"));
        QVERIFY(QDir::fromNativeSeparators(summary).contains("  " + directory + '/'));
        QVERIFY(summary.contains("  " + file));
        QCOMPARE(backend.extractCalls, 0);
        selectOnly(view, rowWithPath(window, "unknown"));
        summary.clear();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("copyDialog");
            QVERIFY(dialog);
            summary = ToQString(static_cast<CopyDialog *>(dialog)->Info);
            dialog->reject();
        });
        action(window, "moveAction")->trigger();
        QVERIFY2(summary.startsWith("Files: 1\n\n"), qPrintable(summary));
        QCOMPARE(backend.extractCalls, 0);
        action(window, "flatViewAction")->setChecked(false);
    }
    void filesystemArchiveTransitions_data() {
        QTest::addColumn<bool>("flat");
        QTest::addColumn<int>("mode");
        for (bool flat : {false, true})
            for (int mode : {0, 3})
                QTest::newRow(qPrintable(QString("flat%1-mode%2").arg(flat).arg(mode)))
                    << flat << mode;
    }
    void filesystemArchiveTransitions() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QTemporaryDir archives;
        QVERIFY(archives.isValid());
        const QDir root(QFileInfo(temp.path()).canonicalFilePath());
        const QString directory = QStringLiteral("папка й с пробелом");
        const QString name = QStringLiteral("текущий й.txt");
        QVERIFY(root.mkdir(directory));
        createFile(root.filePath(name));
        createFile(root.filePath(directory + '/' + name));
        createFile(root.filePath("zero"), {});
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "flatViewAction")->setChecked(false);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        navigate(window, root.path());
        auto *scanner = window.findChild<FilesystemScanner *>();
        QVERIFY(scanner);
        CMyComPtr<IFolderFolder> sourceFolder;
        connect(scanner, &FilesystemScanner::folderReady, &window,
                [&](IFolderFolder *folder) { sourceFolder = folder; });
        action(window, "flatViewAction")->setChecked(flat);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        auto *view = table(window);
        QCOMPARE(view->rowCount(), flat ? 4 : 3);
        if (flat) {
            QVERIFY(sourceFolder);
            UInt32 count = 0;
            QCOMPARE(sourceFolder->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(view->rowCount()));
            QSet<quint32> indices;
            for (int row = 0; row < view->rowCount(); ++row) {
                const quint32 index = view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt();
                QVERIFY(index < count && !indices.contains(index));
                indices.insert(index);
                CPropVariant sourceName, prefix;
                QCOMPARE(sourceFolder->GetProperty(index, kpidName, &sourceName), S_OK);
                QCOMPARE(sourceFolder->GetProperty(index, kpidPrefix, &prefix), S_OK);
                const QString relPath = (QString::fromWCharArray(prefix.bstrVal) +
                                         QString::fromWCharArray(sourceName.bstrVal))
                                            .normalized(QString::NormalizationForm_C);
                QCOMPARE(QDir::fromNativeSeparators(view->model()->index(row, 0).data().toString()),
                         QDir::fromNativeSeparators(relPath));
            }
        }
        selectOnly(view, diskRow(window, root.filePath(name)));
        QList<ArchiveEntry> entries = {
            {name, 11, 3}, {directory + '/' + name, 13, 5}, {"zero", 0, 0}, {"unknown", -1, -1}};
        openListedArchive(window, backend, archives.filePath("runtime.zip"), entries);
        QVERIFY(view->selectionModel()->selectedRows().isEmpty());
        entries.clear();
        action(window, "flatViewAction")->setChecked(flat);
        QCOMPARE(view->rowCount(), flat ? 5 : 4);
        const int archiveRow = rowWithPath(window, name);
        QVERIFY(archiveRow >= 0);
        QCOMPARE(view->model()->index(archiveRow, 2).data(Qt::UserRole + 2).toULongLong(), 11ull);
        const int unknownRow = rowWithPath(window, "unknown");
        QVERIFY(unknownRow >= 0);
        QVERIFY(!view->model()->index(unknownRow, 2).data(Qt::UserRole + 2).isValid());
        selectOnly(view, archiveRow);
        view->setCurrentCell(unknownRow, 0, QItemSelectionModel::NoUpdate);
        navigate(window, root.path());
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(view->rowCount(), flat ? 4 : 3);
        QVERIFY(view->selectionModel()->selectedRows().isEmpty());
        const int diskFile = diskRow(window, root.filePath(name));
        QVERIFY(diskFile >= 0);
        QCOMPARE(view->model()->index(diskFile, 2).data(Qt::UserRole + 2).toULongLong(), 7ull);
        selectOnly(view, diskFile);
        const int zeroRow = diskRow(window, root.filePath("zero"));
        QVERIFY(zeroRow >= 0);
        view->setCurrentCell(zeroRow, 0, QItemSelectionModel::NoUpdate);
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(view->model()->index(view->currentRow(), 1).data().toString(),
                 root.filePath("zero"));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->model()
                     ->index(view->selectionModel()->selectedRows().first().row(), 1)
                     .data()
                     .toString(),
                 root.filePath(name));
        action(window, "flatViewAction")->setChecked(false);
    }

    void filesystemPresentationRetainsLoadedMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = QFileInfo(temp.path()).canonicalFilePath();
        const QString filePath = QDir(path).filePath(QStringLiteral("размер й.txt"));
        createFile(filePath);
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, "flatViewAction")->setChecked(false);
        navigate(window, path);
        auto *scanner = window.findChild<FilesystemScanner *>();
        QVERIFY(scanner);
        bool changed = false;
        connect(scanner, &FilesystemScanner::folderReady, &window, [&](IFolderFolder *) {
            createFile(filePath, "much larger replacement");
            changed = true;
        });
        action(window, "flatViewAction")->setChecked(true);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(changed);
        const int row = diskRow(window, filePath);
        QVERIFY(row >= 0);
        QCOMPARE(table(window)->model()->index(row, 2).data(Qt::UserRole + 2).toULongLong(), 7ull);
        QCOMPARE(QFileInfo(filePath).size(), qint64(23));
        action(window, "flatViewAction")->setChecked(false);
        QCOMPARE(table(window)->model()->index(0, 2).data(Qt::UserRole + 2).toULongLong(), 23ull);
    }

    void archiveNavigationRejectsUnfinishedFilesystemOwner() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        createFile(temp.filePath("source"));
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, "flatViewAction")->setChecked(false);
        navigate(window, temp.path());
        auto *scanner = window.findChild<FilesystemScanner *>();
        QVERIFY(scanner);
        QSignalSpy finished(scanner, &FilesystemScanner::completed);
        int readyCount = 0;
        connect(scanner, &FilesystemScanner::folderReady, &window,
                [&](IFolderFolder *) { ++readyCount; });
        action(window, "flatViewAction")->setChecked(true);
        QVERIFY(!action(window, "refreshAction")->isEnabled());
        openListedArchive(window, backend, temp.filePath("replacement.zip"), {{"source", 29, 17}});
        auto *view = table(window);
        QCOMPARE(view->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 2).data(Qt::UserRole + 2).toULongLong(), 29ull);
        // Retired native workers are deleted only after their queued handoff.
        QTRY_VERIFY(window.findChildren<QThread *>().isEmpty());
        QCoreApplication::processEvents();
        QCOMPARE(readyCount, 0);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(view->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 2).data(Qt::UserRole + 2).toULongLong(), 29ull);
        action(window, "flatViewAction")->setChecked(false);
    }
};

QTEST_MAIN(PanelRuntimeTests)
#include "panel_runtime_tests.moc"
