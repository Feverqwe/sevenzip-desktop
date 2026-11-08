#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"

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
        QTest::addColumn<bool>("manual");
        for (int count : {160, 320})
            for (bool manual : {false, true})
                QTest::newRow(qPrintable(
                    QString("items-%1-%2").arg(count).arg(manual ? "manual" : "automatic")))
                    << count << manual;
    }
    void iconViewsKeepTheirScrollAcrossChanges() {
        QFETCH(int, count);
        QFETCH(bool, manual);
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
        if (manual)
            files->sourcePanel->Set_AutoRefresh_Mode(false);
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
        if (manual)
            action(window, "refreshAction")->trigger();
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
        if (manual)
            action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(view->isEnabled() && files->rowCount() == count + columns, 5000);
        const QModelIndex restored = view->model()->index(filesystemRow(window, anchorName), 0);
        QVERIFY(restored.isValid());
        QCOMPARE(view->visualRect(restored).top(), anchorTop);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(files->currentRow(), {}));
    }

    void tableKeepsPartiallyVisibleRowAcrossRefresh_data() {
        QTest::addColumn<bool>("manual");
        QTest::addColumn<bool>("throughLink");
        QTest::newRow("automatic") << false << false;
        QTest::newRow("manual") << true << false;
#ifdef Q_OS_UNIX
        QTest::newRow("linked-folder") << true << true;
#endif
    }

    void tableKeepsPartiallyVisibleRowAcrossRefresh() {
        QFETCH(bool, manual);
        QFETCH(bool, throughLink);
        QTemporaryDir fixture;
        QTemporaryDir aliases;
        QVERIFY(fixture.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(fixture.path()));
        for (int i = 0; i < 80; ++i)
            QVERIFY(writeFile(fixture.filePath(QString("file-%1.txt").arg(i, 3, 10, QChar('0')))));

        MainWindow window;
        window.resize(640, 400);
        window.show();
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() == 80);
        if (throughLink) {
            QVERIFY(aliases.isValid());
            const QString alias = aliases.filePath("folder link");
            QVERIFY(QFile::link(fixture.path(), alias));
            COpenResult opened;
            QCOMPARE(files->sourcePanel->BindToPath(ToUString(alias + '/'), UString(), opened),
                     S_OK);
            QCOMPARE(files->sourcePanel->RefreshListCtrl(), S_OK);
            QCOMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())),
                     alias);
            QTRY_COMPARE(files->rowCount(), 80);
        }
        if (manual)
            files->sourcePanel->Set_AutoRefresh_Mode(false);
        files->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        const QString anchorName = QStringLiteral("file-035.txt");
        const int row = filesystemRow(window, anchorName);
        QVERIFY(row >= 0);
        files->scrollTo(files->model()->index(row, 0), QAbstractItemView::PositionAtTop);
        auto *scroll = files->verticalScrollBar();
        scroll->setValue(scroll->value() + files->rowHeight(row) / 2);
        QCOMPARE(files->rowAt(0), row);
        const int topOffset = files->rowViewportPosition(row);
        QVERIFY(topOffset < 0);
        files->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        files->setColumnWidth(0, files->viewport()->width() + 200);
        auto *horizontal = files->horizontalScrollBar();
        QTRY_VERIFY(horizontal->maximum() > 0);
        horizontal->setValue(horizontal->maximum() / 2);
        const int horizontalOffset = horizontal->value();
        QVERIFY(horizontalOffset > 0);

        QVERIFY(writeFile(fixture.filePath("000-new.txt")));
        if (manual)
            action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(files->isEnabled() && files->rowCount() == 81, 5000);
        const int restoredRow = filesystemRow(window, anchorName);
        QCOMPARE(files->rowAt(0), restoredRow);
        QCOMPARE(files->rowViewportPosition(restoredRow), topOffset);
        QCOMPARE(horizontal->value(), horizontalOffset);
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
        action(window, "sort3Action")->trigger();
        QVERIFY(files->currentRow() > 0);
        const QByteArray changed("changed contents");
        QVERIFY(writeFile(temp.filePath(QStringLiteral("file-050.txt")), changed));
        QTRY_COMPARE_WITH_TIMEOUT(
            files->model()->index(files->currentRow(), 2).data(Qt::UserRole + 2).toLongLong(),
            qint64(changed.size()), 3500);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(),
                 QStringLiteral("file-050.txt"));
        QCOMPARE(files->currentRow(), 0);
        QVERIFY(files->selectionModel()->isRowSelected(files->currentRow(), QModelIndex()));

        QVERIFY(QFile::remove(temp.filePath(QStringLiteral("новый файл.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(files->rowCount(), 80, 3500);
    }

    void renameDraftSurvivesAutoRefresh_data() {
        QTest::addColumn<bool>("commit");
        QTest::addColumn<bool>("published");
        QTest::addColumn<bool>("manual");
        QTest::newRow("enter") << true << false << false;
        QTest::newRow("escape") << false << false << false;
        QTest::newRow("published-enter") << true << true << false;
        QTest::newRow("published-escape") << false << true << false;
        QTest::newRow("manual-enter") << true << false << true;
        QTest::newRow("manual-escape") << false << false << true;
    }

    void renameDraftSurvivesAutoRefresh() {
        QFETCH(bool, commit);
        QFETCH(bool, published);
        QFETCH(bool, manual);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = fixture.filePath("original.txt");
        const QString renamed = fixture.filePath(QStringLiteral("renamed δ.txt"));
        QVERIFY(writeFile(original, "original bytes"));
        QtWorkspaceApplication application(*qApp);
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        FileTable *views[2];
        for (unsigned pane = 0; pane < 2; ++pane) {
            auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            auto *address = panel->findChild<QLineEdit *>("archivePath");
            views[pane] = panel->findChild<FileTable *>("archiveTable");
            PanelKeys::focusAddress(address);
            address->setText(fixture.path());
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(views[pane]->rowCount(), 1);
            if (published || manual)
                views[pane]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        auto *view = views[0];
        view->setFocus();
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(window, "renameAction")->trigger();
        QPointer<QLineEdit> editor;
        for (auto *candidate : view->findChildren<QLineEdit *>())
            if (candidate->isVisible())
                editor = candidate;
        QVERIFY(editor);
        const QString draft = QFileInfo(renamed).fileName();
        editor->setText(draft);
        QVERIFY(writeFile(fixture.filePath("new file.txt")));
        if (published)
            application.FilesystemPublished({fixture.path()});
        if (manual) {
            auto *refresh = action(window, "refreshAction");
            QVERIFY(refresh->isEnabled());
            refresh->trigger();
            QCOMPARE(view->rowCount(), 1);
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(views[1]->rowCount(), 2, 5000);
        }
        QVERIFY(editor && editor->isVisible());
        QVERIFY(editor->hasFocus());
        QCOMPARE(editor->text(), draft);
        QTest::keyClick(editor, commit ? Qt::Key_Return : Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(view->rowCount(), 2, 5000);
        QCOMPARE(QFileInfo::exists(renamed), commit);
        QCOMPARE(QFileInfo::exists(original), !commit);
        QFile payload(commit ? renamed : original);
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("original bytes"));
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

        // A directory receiving continuous writes must not monopolize the
        // refresh timer while the other pane has an update waiting.
        const QString changing = QDir(folders[0]).filePath(selectedNames[0]);
        int size = 50;
        QVERIFY(writeFile(changing, QByteArray(size, 'x')));
        QTimer writer;
        connect(&writer, &QTimer::timeout, this,
                [&] { QVERIFY(writeFile(changing, QByteArray(++size, 'x'))); });
        writer.start(100);
        const QString newName = QStringLiteral("новый справа.txt");
        QVERIFY(writeFile(QDir(folders[1]).filePath(newName)));
        auto *right = views[1];
        QTRY_VERIFY_WITH_TIMEOUT(right->isEnabled() && right->rowCount() == 272, 5000);
        QVERIFY(
            !right->model()
                 ->match(right->model()->index(0, 0), Qt::DisplayRole, newName, 1, Qt::MatchExactly)
                 .isEmpty());
        QTRY_VERIFY(right->hasFocus());
    }

    void idleTicksKeepActiveTitleAndAddressDraft_data() {
        QTest::addColumn<unsigned>("owner");
        QTest::addColumn<bool>("published");
        QTest::addColumn<bool>("peerUpdate");
        QTest::newRow("left") << 0u << false << false;
        QTest::newRow("right") << 1u << false << false;
        QTest::newRow("published-left") << 0u << true << false;
        QTest::newRow("published-right") << 1u << true << false;
        QTest::newRow("published-peer") << 1u << true << true;
        QTest::newRow("automatic-peer") << 1u << false << true;
    }
    void idleTicksKeepActiveTitleAndAddressDraft() {
        QFETCH(unsigned, owner);
        QFETCH(bool, published);
        QFETCH(bool, peerUpdate);
        const unsigned updated = peerUpdate ? 1 - owner : owner;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QtWorkspaceApplication application(*qApp);
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
        addresses[owner]->setSelection(8, -8);
        const QString title = window.windowTitle();
        QVERIFY(QDir::fromNativeSeparators(title).startsWith(folders[owner]));
        QSignalSpy titles(&window, &QWidget::windowTitleChanged);
        auto *rename = action(window, "renameAction");
        auto *remove = action(window, "deleteAction");
        auto *copy = action(window, "copyFilesAction");
        QVERIFY(rename->isEnabled());
        QVERIFY(remove->isEnabled());
        QVERIFY(copy->isEnabled());
        QSignalSpy renameAvailability(rename, &QAction::enabledChanged);
        QSignalSpy removeAvailability(remove, &QAction::enabledChanged);
        QSignalSpy copyAvailability(copy, &QAction::enabledChanged);
        if (published || peerUpdate) {
            views[updated]->sourcePanel->Set_AutoRefresh_Mode(!published);
            QVERIFY(writeFile(QDir(folders[updated]).filePath("published.txt")));
            if (published)
                application.FilesystemPublished({folders[updated]});
            QTRY_COMPARE(views[updated]->rowCount(), 2);
        } else
            QTest::qWait(1250); // Cross the actual application timer interval while idle.
        for (const auto &event : titles)
            QCOMPARE(event.first().toString(), title);
        QCOMPARE(window.windowTitle(), title);
        QCOMPARE(addresses[owner]->text(), draft);
        QCOMPARE(addresses[owner]->selectedText(), draft.left(8));
        QCOMPARE(addresses[owner]->cursorPosition(), 0);
        QVERIFY(addresses[owner]->hasFocus());
        for (unsigned pane = 0; pane < 2; ++pane) {
            QCOMPARE(views[pane]->rowCount(), (published || peerUpdate) && pane == updated ? 2 : 1);
            QCOMPARE(views[pane]->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                         ToQString(views[pane]->sourcePanel->GetCurrentFolderPrefix()))),
                     folders[pane]);
        }
        if (peerUpdate) {
            QVERIFY(renameAvailability.isEmpty());
            QVERIFY(removeAvailability.isEmpty());
            QVERIFY(copyAvailability.isEmpty());
            QTest::keyClicks(QApplication::focusWidget(), "next");
            QCOMPARE(addresses[owner]->text(), QString("next") + draft.mid(8));
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
        table(window)->sourcePanel->Set_AutoRefresh_Mode(false);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->selectAll();
        QTest::keyClicks(address, "unfinished path");
        address->setSelection(2, 5);
        QVERIFY(writeFile(temp.filePath(QStringLiteral("manual.txt"))));
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 3);
        QCOMPARE(address->text(), QStringLiteral("unfinished path"));
        QCOMPARE(address->selectedText(), QStringLiteral("finis"));
    }

    void missingFolderClearsStaleRowsAndRecovers_data() {
        QTest::addColumn<bool>("automatic");
        QTest::addColumn<bool>("publishedRename");
        QTest::newRow("automatic") << true << false;
        QTest::newRow("manual") << false << false;
        QTest::newRow("published-rename") << false << true;
    }
    void missingFolderClearsStaleRowsAndRecovers() {
        QFETCH(bool, automatic);
        QFETCH(bool, publishedRename);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString folder = temp.filePath(QStringLiteral("browse"));
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(writeFile(folder + QStringLiteral("/old.txt"), "control"));
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(folder));
        QtWorkspaceApplication application(*qApp);
        MainWindow window;
        window.show();
        QCOMPARE(table(window)->rowCount(), 1);
        table(window)->sourcePanel->Set_AutoRefresh_Mode(automatic);
        const QList<QAction *> fileCommands{action(window, "toolbarAddAction"),
                                            action(window, "copyAction"),
                                            action(window, "createFolderAction")};
        for (const auto *command : fileCommands)
            QVERIFY(command->isEnabled());
        QVERIFY(QDir::setCurrent(temp.path()));
        const QString renamed = temp.filePath("renamed");
        if (publishedRename) {
            QVERIFY(QDir().rename(folder, renamed));
            application.FilesystemPublished({temp.path()});
        } else {
            QVERIFY(QFile::remove(folder + QStringLiteral("/old.txt")));
            bool removed = false;
            QTRY_VERIFY_WITH_TIMEOUT(removed || (removed = QDir().rmdir(folder)), 2000);
            if (!automatic)
                action(window, "refreshAction")->trigger();
        }
        QTRY_COMPARE_WITH_TIMEOUT(table(window)->rowCount(), 0, 3500);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Folder not found")));
        for (const auto *command : fileCommands)
            QVERIFY(!command->isEnabled());
        QVERIFY(!action(window, "testArchiveAction")->isEnabled());
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        if (publishedRename) {
            QFile retained(renamed + "/old.txt");
            QVERIFY(retained.open(QIODevice::ReadOnly));
            QCOMPARE(retained.readAll(), QByteArray("control"));
        }

        QVERIFY(QDir().mkpath(folder));
        QVERIFY(writeFile(folder + QStringLiteral("/new.txt")));
        if (!automatic)
            action(window, "refreshAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(table(window)->rowCount(), 1, 3500);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral("new.txt"));
        for (const auto *command : fileCommands)
            QVERIFY(command->isEnabled());
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
        const QByteArray changed("changed after the folder came back");
        QVERIFY(writeFile(folder + "/new.txt", changed));
        if (!automatic)
            action(window, "refreshAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(
            table(window)->model()->index(0, 2).data(Qt::UserRole + 2).toLongLong(),
            qint64(changed.size()), 3500);
    }
};

QTEST_MAIN(PanelAutoRefreshTests)
#include "panel_auto_refresh_tests.moc"
