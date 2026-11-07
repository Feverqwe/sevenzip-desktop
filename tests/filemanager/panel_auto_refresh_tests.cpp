#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"

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
        QTest::addColumn<int>("count");
        for (int count : {160, 320})
            QTest::newRow(qPrintable(QString("items-%1").arg(count))) << count;
    }
    void iconViewsKeepTheirScrollAcrossChanges() {
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
        action(window, "viewMode0Action")->trigger();
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
        auto *scroll = view->verticalScrollBar();
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

        int anchorRow = 0;
        while (anchorRow < count && !view->viewport()->rect().intersects(
                                        view->visualRect(view->model()->index(anchorRow, 0))))
            ++anchorRow;
        QVERIFY(anchorRow < count);
        const QModelIndex anchor = view->model()->index(anchorRow, 0);
        const QString anchorName = anchor.data().toString();
        const int anchorTop = view->visualRect(anchor).top();
        const int firstTop = view->visualRect(view->model()->index(0, 0)).top();
        int columns = 1;
        while (columns < count &&
               view->visualRect(view->model()->index(columns, 0)).top() == firstTop)
            ++columns;
        QVERIFY(columns < count);
        for (int column = 0; column < columns; ++column)
            QVERIFY(writeFile(fixture.filePath(QString("000-new-%1.txt").arg(column))));
        QTRY_VERIFY_WITH_TIMEOUT(view->isEnabled() && files->rowCount() == count + columns, 5000);
        const QModelIndex restored = view->model()->index(filesystemRow(window, anchorName), 0);
        QVERIFY(restored.isValid());
        QCOMPARE(view->visualRect(restored).top(), anchorTop);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(files->currentRow(), {}));
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
            PanelKeys::focusAddress(address);
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
            QTRY_VERIFY(views[1]->hasFocus());
        }
    }

    void idleTicksKeepActiveTitleAndAddressDraft_data() {
        QTest::addColumn<unsigned>("owner");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void idleTicksKeepActiveTitleAndAddressDraft() {
        QFETCH(unsigned, owner);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        QLineEdit *addresses[2];
        FileTable *views[2];
        QString folders[2];
        for (unsigned pane = 0; pane < 2; ++pane) {
            folders[pane] = fixture.filePath(QString("panel %1 文件").arg(pane));
            QVERIFY(QDir().mkpath(folders[pane]));
            QVERIFY(writeFile(QDir(folders[pane]).filePath("control.txt"), "unchanged"));
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            addresses[pane] = ui->findChild<QLineEdit *>("archivePath");
            views[pane] = ui->findChild<FileTable *>("archiveTable");
            PanelKeys::focusAddress(addresses[pane]);
            window.openPath(folders[pane]);
            QTRY_VERIFY(views[pane]->isEnabled() && views[pane]->rowCount() == 1);
            views[pane]->selectRow(0);
        }
        PanelKeys::focusAddress(addresses[owner]);
        QTRY_VERIFY(addresses[owner]->hasFocus());
        const QString draft = QStringLiteral("черновик адреса δ");
        addresses[owner]->setText(draft);
        const QString title = window.windowTitle();
        QVERIFY(QDir::fromNativeSeparators(title).startsWith(folders[owner]));
        QSignalSpy titles(&window, &QWidget::windowTitleChanged);
        QTest::qWait(1250); // Cross the actual application timer interval while idle.
        for (const auto &event : titles)
            QCOMPARE(event.first().toString(), title);
        QCOMPARE(window.windowTitle(), title);
        QCOMPARE(addresses[owner]->text(), draft);
        QVERIFY(addresses[owner]->hasFocus());
        for (unsigned pane = 0; pane < 2; ++pane) {
            QCOMPARE(views[pane]->rowCount(), 1);
            QCOMPARE(views[pane]->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                         ToQString(views[pane]->sourcePanel->GetCurrentFolderPrefix()))),
                     folders[pane]);
        }
    }

    void refreshesIndependentWindowsAndNavigatedLocations() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString a = fixture.filePath("A folder");
        const QString b = fixture.filePath("B папка");
        const QString c = fixture.filePath("C folder");
        for (const auto &path : {a, b, c})
            QVERIFY(QDir().mkpath(path));
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(a);
        second.openPath(c);
        const auto files = [](MainWindow &window) -> FileTable * {
            for (auto *view : window.findChildren<FileTable *>())
                if (view->isVisible())
                    return view;
            return nullptr;
        };
        QTRY_VERIFY(files(first) && files(first)->isEnabled());
        QTRY_VERIFY(files(second) && files(second)->isEnabled());
        QtWindowWorkspace *owner = nullptr;
        for (auto *object : first.children())
            if (auto *workspace = dynamic_cast<QtWindowWorkspace *>(object))
                owner = workspace;
        QVERIFY(owner);
        first.openPath(b);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), b);
        QTRY_VERIFY(files(first)->isEnabled());
        QVERIFY(writeFile(QDir(b).filePath("new.txt")));
        QVERIFY(writeFile(QDir(c).filePath("new.txt")));
        QTRY_COMPARE_WITH_TIMEOUT(files(first)->rowCount(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(files(second)->rowCount(), 1, 5000);
        first.openPath(a);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), a);
        QVERIFY(writeFile(QDir(a).filePath("new.txt")));
        QTRY_COMPARE_WITH_TIMEOUT(files(first)->rowCount(), 1, 5000);
    }

    void refreshIgnoresLegacyDisabledPreference() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QVERIFY(writeFile(temp.filePath(QStringLiteral("start.txt"))));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("view/autoRefresh", false);
        settings.sync();

        MainWindow window;
        window.show();
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QVERIFY(writeFile(temp.filePath(QStringLiteral("later.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(table(window)->rowCount(), 2, 5000);
        QVERIFY(writeFile(temp.filePath(QStringLiteral("manual.txt"))));
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 3);
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
