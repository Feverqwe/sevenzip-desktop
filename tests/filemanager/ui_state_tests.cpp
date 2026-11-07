#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include <QScopeGuard>
#include <QTreeWidget>

extern bool g_CaseSensitive;

class MainWindowStateTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.clear();
    }

    void maskAndTypeSelectionFollowFileManager() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        for (const QString &name : {QStringLiteral("Alpha.txt"), QStringLiteral("beta.TXT"),
                                    QStringLiteral("image.png")}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto applyMask = [&](const char *actionName, const QString &mask) {
            QTimer::singleShot(0, [&] {
                auto *dialog = window.findChild<QDialog *>("comboDialog");
                QVERIFY(dialog);
                dialog->findChild<QComboBox *>("comboValue")->setEditText(mask);
                dialog->accept();
            });
            action(window, actionName)->trigger();
        };
        applyMask("selectMaskAction", "*.txt");
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), g_CaseSensitive ? 1 : 2);
        applyMask("deselectMaskAction", "alpha.*");
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        const int focusedBeforeCancel = table(window)->currentRow();
        QTimer::singleShot(0, [&] {
            auto *dialog = window.findChild<QDialog *>("comboDialog");
            QVERIFY(dialog);
            dialog->findChild<QComboBox *>("comboValue")->setEditText("*");
            dialog->reject();
        });
        action(window, "selectMaskAction")->trigger();
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)->currentRow(), focusedBeforeCancel);
        table(window)->selectionModel()->setCurrentIndex(
            table(window)->model()->index(filesystemRow(window, "image.png"), 0),
            QItemSelectionModel::NoUpdate);
        action(window, "selectTypeAction")->trigger();
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 2);
        action(window, "deselectTypeAction")->trigger();
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);

        openListedArchive(window, backend, temp.filePath("test.7z"),
                          {ArchiveEntry{"first.txt", 1, 1}, ArchiveEntry{"second.TXT", 1, 1},
                           ArchiveEntry{"other.bin", 1, 1}});
        applyMask("selectMaskAction", "*.txt");
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), g_CaseSensitive ? 1 : 2);
    }

    void foldersHistoryDialogPersistsAndDeletesEntries() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath("first folder");
        const QString second = temp.filePath(QStringLiteral("вторая папка"));
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(first);
        QTest::keyClick(address, Qt::Key_Return);
        address->setText(second);
        QTest::keyClick(address, Qt::Key_Return);
        const QString canonicalFirst = QFileInfo(first).canonicalFilePath();
        const QString canonicalSecond = QFileInfo(second).canonicalFilePath();
        QTimer::singleShot(0, [&] {
            auto *dialog = window.findChild<QDialog *>("foldersHistoryDialog");
            QVERIFY(dialog);
            auto *list = dialog->findChild<QTreeWidget *>("foldersHistoryList");
            QCOMPARE(list->topLevelItem(0)->text(0), canonicalSecond);
            QCOMPARE(list->topLevelItem(1)->text(0), canonicalFirst);
            list->setCurrentItem(list->topLevelItem(0));
            QTest::keyClick(list, Qt::Key_Delete);
            list->setCurrentItem(list->topLevelItem(0));
            dialog->accept();
        });
        action(window, "foldersHistoryAction")->trigger();
        QCOMPARE(address->text(), canonicalFirst);
        MainWindow reopened(&backend);
        QTimer::singleShot(0, &reopened, [&] {
            auto *dialog = reopened.findChild<QDialog *>("foldersHistoryDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QTreeWidget *>("foldersHistoryList");
            QVERIFY(list);
            QCOMPARE(list->findItems(canonicalFirst, Qt::MatchExactly).size(), 1);
            QCOMPARE(list->findItems(canonicalSecond, Qt::MatchExactly).size(), 0);
        });
        QVERIFY(action(reopened, "foldersHistoryAction")->isEnabled());
        action(reopened, "foldersHistoryAction")->trigger();
    }

    void favoritesFollowFoldersAndArchivePaths() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("папка с пробелом"));
        const QString second = temp.filePath("other");
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        const QString archive = temp.filePath(QStringLiteral("архив с пробелом.7z"));
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        address->setText(first);
        QTest::keyClick(address, Qt::Key_Return);
        QTest::keyClick(view, Qt::Key_1, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(action(window, "openBookmark1Action")->isEnabled(), true);
        address->setText(second);
        QTest::keyClick(address, Qt::Key_Return);
        QTest::keyClick(view, Qt::Key_1, Qt::AltModifier);
        QCOMPARE(address->text(), QFileInfo(first).canonicalFilePath());

        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] {
                backend.completeList({ArchiveEntry{QStringLiteral("внутри/файл.txt"), 1, 1}});
            });
            QVERIFY(!action(window, "openBookmark1Action")->isEnabled());
        });
        window.openArchive(archive);
        openRow(window, rowWithPath(window, QStringLiteral("внутри")));
        action(window, "saveBookmark2Action")->trigger();
        action(window, "openBookmark1Action")->trigger();
        QCOMPARE(address->text(), QFileInfo(first).canonicalFilePath());
        backend.queueList({ArchiveEntry{QStringLiteral("внутри/файл.txt"), 1, 1}});
        action(window, "openBookmark2Action")->trigger();
        QCOMPARE(backend.listCalls, 2);
        QVERIFY(address->text().endsWith(QStringLiteral(" / внутри")));
        QCOMPARE(view->rowCount(), 1);

        MainWindow reopened;
        QVERIFY(action(reopened, "openBookmark1Action")->isEnabled());
        QVERIFY(action(reopened, "openBookmark2Action")->isEnabled());
        QFile::remove(archive);
        action(reopened, "openBookmark2Action")->trigger();
        QCOMPARE(reopened.findChild<QLineEdit *>("archivePath")->text(),
                 QFileInfo(temp.path()).canonicalFilePath());
    }

    void mainWindowFolderAndSelectionCommands() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("файл с пробелом.txt"));
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(table(window)->rowCount(), 1);
        auto *createFolder = action(window, "createFolderAction");
        auto *refresh = action(window, "refreshAction");
        QVERIFY(createFolder->isEnabled());
        QVERIFY(refresh->isEnabled());

        QTimer::singleShot(0, &window, [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<CComboDialog *>(widget))
                    dialog->reject();
            }
        });
        createFolder->trigger();
        QCOMPARE(table(window)->rowCount(), 1);
        QTimer::singleShot(0, &window, [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<CComboDialog *>(widget)) {
                    dialog->findChild<QComboBox *>("comboValue")
                        ->setCurrentText(QStringLiteral("папка с пробелом"));
                    dialog->accept();
                }
            }
        });
        createFolder->trigger();
        const QString folder = temp.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QFileInfo(folder).isDir());
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)
                     ->model()
                     ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QStringLiteral("папка с пробелом"));

        action(window, "invertSelectionAction")->trigger();
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)
                     ->model()
                     ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QStringLiteral("файл с пробелом.txt"));
        action(window, "deselectAllAction")->trigger();
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        table(window)->selectRow(filesystemRow(window, QStringLiteral("файл с пробелом.txt")));
        const QString later = temp.filePath("later.txt");
        QFile newFile(later);
        QVERIFY(newFile.open(QIODevice::WriteOnly));
        newFile.close();
        refresh->trigger();
        QCOMPARE(table(window)->rowCount(), 3);
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)
                     ->model()
                     ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QStringLiteral("файл с пробелом.txt"));
        QCOMPARE(backend.listCalls, 0);
    }

    void refreshArchiveKeepsFolderAndSelection() {
        ControlledBackend backend;
        MainWindow window(&backend);
        ArchiveEntry first;
        first.path = QStringLiteral("папка с пробелом/первый.txt");
        openListedArchive(window, backend, "example.7z", {first});
        openRow(window, rowWithPath(window, QStringLiteral("папка с пробелом")));
        QCOMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        auto *list = static_cast<FileTable *>(table(window));
        CMyComPtr<IFolderFolder> provider = list->sourcePanel->GetFolder();
        const QString address = window.findChild<QLineEdit *>("archivePath")->text();
        const int listings = backend.listCalls;
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        for (int count = 1; count <= 3; ++count) {
            action(window, "refreshAction")->trigger();
            QCOMPARE(backend.listCalls, listings);
            QCOMPARE(list->sourcePanel->GetFolder(), (IFolderFolder *)provider);
            QCOMPARE(publication.count(), count);
            QCOMPARE(list->rowCount(), 1);
            QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(list->model()
                         ->index(list->selectionModel()->selectedRows().first().row(), 0)
                         .data()
                         .toString(),
                     QStringLiteral("первый.txt"));
            QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), address);
            QVERIFY(action(window, "refreshAction")->isEnabled());
        }
    }
};

QTEST_MAIN(MainWindowStateTests)
#include "ui_state_tests.moc"
