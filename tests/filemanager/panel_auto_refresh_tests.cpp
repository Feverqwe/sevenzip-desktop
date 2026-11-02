#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"

#include <QScrollBar>
#include <QStackedWidget>

namespace {
struct CurrentDirectoryGuard {
    const QString previous = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(previous); }
};

bool writeFile(const QString &path, const QByteArray &data = "data") {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
} // namespace

class PanelAutoRefreshTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.remove(QStringLiteral("view/autoRefresh"));
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void iconViewsKeepTheirScrollAcrossChanges_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("count");
        for (int mode : {0, 1, 2})
            for (int count : {160, 320})
                QTest::newRow(qPrintable(QString("mode-%1-items-%2").arg(mode).arg(count)))
                    << mode << count;
    }
    void iconViewsKeepTheirScrollAcrossChanges() {
        QFETCH(int, mode);
        QFETCH(int, count);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(fixture.path()));
        for (int i = 0; i < count; ++i)
            QVERIFY(writeFile(fixture.filePath(QString("file-%1.txt").arg(i, 3, 10, QChar('0')))));
        MainWindow window;
        window.resize(640, 400);
        window.show();
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() == count);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *view = qobject_cast<QAbstractItemView *>(
            window.findChild<QStackedWidget *>("fileViewStack")->currentWidget());
        QVERIFY(view && view != files);
        const QString selected = QString("file-%1.txt").arg(count / 2, 3, 10, QChar('0'));
        const int row = filesystemRow(window, selected);
        QVERIFY(row >= 0);
        files->setCurrentCell(row, 0);
        files->selectRow(row);
        view->setFocus();
        QApplication::processEvents();
        auto *scroll = mode == 2 ? view->horizontalScrollBar() : view->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue((scroll->maximum() + 1) / 2);
        const int horizontal = view->horizontalScrollBar()->value();
        const int vertical = view->verticalScrollBar()->value();
        QVERIFY(scroll->value() > 0);
        const QByteArray changed("changed contents");
        QVERIFY(writeFile(fixture.filePath(selected), changed));
        QTRY_VERIFY_WITH_TIMEOUT(view->isEnabled() && files->rowCount() == count, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            files->model()->index(files->currentRow(), 2).data(Qt::UserRole + 2).toLongLong(),
            qint64(changed.size()), 5000);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(files->currentRow(), {}));
        QCOMPARE(view->horizontalScrollBar()->value(), horizontal);
        QCOMPARE(view->verticalScrollBar()->value(), vertical);
    }

    void detectsChangesAndPreservesSelectionAndScroll() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        for (int i = 0; i < 80; ++i)
            QVERIFY(
                writeFile(temp.filePath(QStringLiteral("file-%1.txt").arg(i, 3, 10, QChar('0')))));

        MainWindow window;
        window.resize(640, 400);
        window.show();
        auto *files = table(window);
        QCOMPARE(files->rowCount(), 80);
        const int selectedRow = filesystemRow(window, QStringLiteral("file-050.txt"));
        QVERIFY(selectedRow >= 0);
        files->setCurrentCell(selectedRow, 0);
        files->selectRow(selectedRow);
        const int topRow = filesystemRow(window, QStringLiteral("file-035.txt"));
        files->scrollTo(files->model()->index(topRow, 0), QAbstractItemView::PositionAtTop);
        const QString topPath = files->model()->index(files->rowAt(0), 1).data().toString();
        QVERIFY(files->verticalScrollBar()->value() > 0);

        QVERIFY(writeFile(temp.filePath(QStringLiteral("новый файл.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(files->rowCount(), 81, 3500);
        QVERIFY(files->model()->index(files->currentRow(), 0).isValid());
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(),
                 QStringLiteral("file-050.txt"));
        QVERIFY(files->selectionModel()->isRowSelected(files->currentRow(), QModelIndex()));
        QVERIFY(files->model()->index(files->rowAt(0), 1).isValid());
        QCOMPARE(files->model()->index(files->rowAt(0), 1).data().toString(), topPath);

        // Changing an existing file leaves the directory entry set unchanged.
        // Windows must observe this through the original native folder watcher.
        const QByteArray changed("changed contents");
        QVERIFY(writeFile(temp.filePath(QStringLiteral("file-050.txt")), changed));
        QTRY_COMPARE_WITH_TIMEOUT(
            files->model()->index(files->currentRow(), 2).data(Qt::UserRole + 2).toLongLong(),
            qint64(changed.size()), 3500);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(),
                 QStringLiteral("file-050.txt"));
        QVERIFY(files->selectionModel()->isRowSelected(files->currentRow(), QModelIndex()));

        QVERIFY(QFile::remove(temp.filePath(QStringLiteral("новый файл.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(files->rowCount(), 80, 3500);
    }

    void bothPanelsKeepTheirViewportAcrossChanges() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList folders{fixture.filePath(QStringLiteral("левая папка 空")),
                                  fixture.filePath(QStringLiteral("правая папка 空"))};
        for (const auto &folder : folders) {
            QVERIFY(QDir().mkpath(folder));
            for (int i = 0; i < 270; ++i)
                QVERIFY(writeFile(QDir(folder).filePath(
                    QStringLiteral("file-%1.txt").arg(i, 3, 10, QChar('0')))));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(1100, 400);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        FileTable *views[2];
        const QStringList selectedNames{"file-050.txt", "file-150.txt"};
        QStringList topPaths;
        for (unsigned pane = 0; pane < 2; ++pane) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            views[pane] = ui->findChild<FileTable *>("archiveTable");
            auto *view = views[pane];
            address->setFocus();
            address->setText(folders[int(pane)]);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(view->isEnabled() && view->rowCount() == 270);
            action(window, "viewMode3Action")->trigger();
            const auto matches =
                view->model()->match(view->model()->index(0, 0), Qt::DisplayRole,
                                     selectedNames[int(pane)], 1, Qt::MatchExactly);
            QVERIFY(!matches.isEmpty());
            const int selectedRow = matches.first().row();
            view->setCurrentCell(selectedRow, 0);
            view->selectRow(selectedRow);
            view->scrollTo(view->model()->index(selectedRow - 15, 0),
                           QAbstractItemView::PositionAtTop);
            QVERIFY(view->verticalScrollBar()->value() > 0);
        }
        views[1]->setFocus();
        QTRY_VERIFY(views[1]->hasFocus());
        for (auto *view : views)
            topPaths << view->model()->index(view->rowAt(0), 1).data().toString();
        for (bool addEntry : {false, true}) {
            for (unsigned pane = 0; pane < 2; ++pane) {
                const QDir folder(folders[int(pane)]);
                if (addEntry)
                    QVERIFY(writeFile(folder.filePath(QStringLiteral("новый файл.txt"))));
                else
                    QVERIFY(writeFile(folder.filePath(selectedNames[int(pane)]),
                                      QByteArray(31 + int(pane), 'x')));
            }
            for (unsigned pane = 0; pane < 2; ++pane) {
                auto *view = views[pane];
                QTRY_VERIFY_WITH_TIMEOUT(
                    view->isEnabled() && view->rowCount() == (addEntry ? 271 : 270), 5000);
                QTRY_COMPARE_WITH_TIMEOUT(
                    view->model()->index(view->currentRow(), 2).data(Qt::UserRole + 2).toLongLong(),
                    qint64(31 + pane), 5000);
                QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                         selectedNames[int(pane)]);
                QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), QModelIndex()));
                QCOMPARE(view->model()->index(view->rowAt(0), 1).data().toString(),
                         topPaths[int(pane)]);
            }
            auto *peer = window.findChild<QWidget *>("filePanel1");
            QVERIFY(peer->isAncestorOf(window.focusWidget()));
        }
    }

    void togglePersistsAndGatesTimer() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QVERIFY(writeFile(temp.filePath(QStringLiteral("start.txt"))));
        {
            MainWindow window;
            window.show();
            auto *toggle = action(window, "autoRefreshAction");
            QVERIFY(toggle->isChecked());
            toggle->trigger();
            QVERIFY(!toggle->isChecked());
            QVERIFY(writeFile(temp.filePath(QStringLiteral("later.txt"))));
            QTest::qWait(1250);
            QCOMPARE(table(window)->rowCount(), 1);
            action(window, "refreshAction")->trigger();
            QCOMPARE(table(window)->rowCount(), 2);
        }
        MainWindow reopened;
        QVERIFY(!action(reopened, "autoRefreshAction")->isChecked());
        action(reopened, "autoRefreshAction")->trigger();
        QVERIFY(action(reopened, "autoRefreshAction")->isChecked());
    }

    void missingFolderClearsStaleRowsAndRecovers() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString folder = temp.filePath(QStringLiteral("browse"));
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(writeFile(folder + QStringLiteral("/old.txt")));
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(folder));
        MainWindow window;
        window.show();
        QCOMPARE(table(window)->rowCount(), 1);
        QVERIFY(QDir::setCurrent(temp.path()));
        QVERIFY(QFile::remove(folder + QStringLiteral("/old.txt")));
        bool removed = false;
        QTRY_VERIFY_WITH_TIMEOUT(removed || (removed = QDir().rmdir(folder)), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(table(window)->rowCount(), 0, 3500);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Folder not found")));

        QVERIFY(QDir().mkpath(folder));
        QVERIFY(writeFile(folder + QStringLiteral("/new.txt")));
        QTRY_COMPARE_WITH_TIMEOUT(table(window)->rowCount(), 1, 3500);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral("new.txt"));
    }
};

QTEST_MAIN(PanelAutoRefreshTests)
#include "panel_auto_refresh_tests.moc"
