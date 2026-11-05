#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QImage>
#include <QTabBar>

class WorkspaceFileStatesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int rowFor(FileTable &table, const QString &name) {
        for (int row = 0; row < table.rowCount(); ++row)
            if (table.model()->index(row, 0).data().toString() == name)
                return row;
        return -1;
    }
    static bool cut(FileTable &table, const QString &name) {
        return table.model()->index(rowFor(table, name), 0).data(QtPanelList::CutRole).toBool();
    }
    static void write(const QString &path) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("file state fixture"), 18);
    }
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *child : window.children())
            if (auto *result = dynamic_cast<QtWindowWorkspace *>(child))
                return result;
        return nullptr;
    }

  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QApplication::clipboard()->clear();
    }
    void cleanup() { QApplication::clipboard()->clear(); }

    void cutFollowsFilesystemEntryAcrossViewsTabsAndWindows() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath("Source");
        const QString peer = fixture.filePath("Other folder");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        write(source + "/Alpha.txt");
        write(source + "/Beta.txt");
        write(peer + "/Alpha.txt");
        ControlledBackend backend, otherBackend;
        MainWindow window(&backend), other(&otherBackend);
        window.show();
        other.show();
        window.openPath(source);
        other.openPath(source);
        auto *files = table(window);
        auto *otherFiles = table(other);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "Alpha.txt") >= 0);
        QTRY_VERIFY(otherFiles->isEnabled() && rowFor(*otherFiles, "Alpha.txt") >= 0);
        files->selectRow(rowFor(*files, "Alpha.txt"));
        files->setCurrentCell(rowFor(*files, "Beta.txt"), files->columnForProperty(kpidSize),
                              QItemSelectionModel::NoUpdate);
        action(window, "cutFilesAction")->trigger();
        QTRY_VERIFY(cut(*files, "Alpha.txt"));
        QVERIFY(!cut(*files, "Beta.txt"));
        QTRY_VERIFY(cut(*otherFiles, "Alpha.txt"));
        files->sortItems(0, Qt::DescendingOrder);
        QVERIFY(cut(*files, "Alpha.txt"));
        QCOMPARE(files->currentIndex().siblingAtColumn(0).data().toString(), QString("Beta.txt"));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(files->isEnabled() && cut(*files, "Alpha.txt"));
        QCOMPARE(files->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(files->selectionModel()->selectedRows().first().data().toString(),
                 QString("Alpha.txt"));
        auto *icons = window.findChild<QListView *>("iconList");
        action(window, "viewMode0Action")->trigger();
        QTRY_VERIFY(icons->isVisible());
        QVERIFY(icons->model()
                    ->index(rowFor(*files, "Alpha.txt"), 0)
                    .data(QtPanelList::CutRole)
                    .toBool());
        QCOMPARE(icons->selectionModel()->selectedRows(), files->selectionModel()->selectedRows());
        auto *owner = workspace(window);
        QVERIFY(owner);
        owner->NewTab(0, peer);
        auto *peerFiles = window.findChild<QSplitter *>("filePanelSplitter")
                              ->widget(0)
                              ->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(peerFiles->isEnabled() && rowFor(*peerFiles, "Alpha.txt") >= 0);
        QVERIFY(!cut(*peerFiles, "Alpha.txt"));
        QVERIFY(cut(*files, "Alpha.txt"));
        // Replacing Cut with Copy updates hidden tab models and other windows.
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(source + "/Alpha.txt")});
        mime->setData("x-special/gnome-copied-files", "copy\n");
        QApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY(!cut(*files, "Alpha.txt"));
        QTRY_VERIFY(!cut(*otherFiles, "Alpha.txt"));
        QVERIFY(QFileInfo::exists(source + "/Alpha.txt"));
    }

    void externalCutHints_data() {
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<QByteArray>("hint");
        QTest::newRow("gnome") << QByteArray("x-special/gnome-copied-files") << QByteArray("cut\n");
        QTest::newRow("kde") << QByteArray("application/x-kde-cutselection") << QByteArray("1");
#ifdef Q_OS_WIN
        QTest::newRow("windows")
            << QByteArray("application/x-qt-windows-mime;value=\"Preferred DropEffect\"")
            << QByteArray("\x02\x00\x00\x00", 4);
#endif
    }
    void externalCutHints() {
        QFETCH(QByteArray, format);
        QFETCH(QByteArray, hint);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("External cut.txt");
        write(path);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "External cut.txt") >= 0);
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        mime->setData(format, hint);
        QApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY(cut(*files, "External cut.txt"));
        auto *mixed = new QMimeData;
        mixed->setUrls({QUrl::fromLocalFile(path), QUrl("https://example.invalid/file")});
        mixed->setData(format, hint);
        QApplication::clipboard()->setMimeData(mixed);
        QTRY_VERIFY(!cut(*files, "External cut.txt"));
        QApplication::clipboard()->clear();
        QTRY_VERIFY(!cut(*files, "External cut.txt"));
    }

    void cutLinkKeepsTargetAndOtherNamesUnmarked() {
#ifndef Q_OS_UNIX
        QSKIP("QFile::link creates shortcuts rather than symlinks on this platform");
#else
        QTemporaryDir fixture;
        write(fixture.filePath("target.txt"));
        QVERIFY(QFile::link(fixture.filePath("target.txt"), fixture.filePath("link.txt")));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "link.txt") >= 0);
        files->selectRow(rowFor(*files, "link.txt"));
        action(window, "cutFilesAction")->trigger();
        QTRY_VERIFY(cut(*files, "link.txt"));
        QVERIFY(!cut(*files, "target.txt"));
#endif
    }

    void nativeSymlinkDirectoryUsesItsOperationPath() {
#ifndef Q_OS_UNIX
        QSKIP("Requires a directory symlink");
#else
        QTemporaryDir fixture;
        const QString directory = fixture.filePath("Real directory");
        const QString alias = fixture.filePath("Directory link");
        QVERIFY(QDir().mkpath(directory));
        write(directory + "/Alpha.txt");
        QVERIFY(QFile::link(directory, alias));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled());
        COpenResult opened;
        // The native directory route retains its separator, including for a link.
        QCOMPARE(files->sourcePanel->BindToPath(ToUString(alias + '/'), UString(), opened), S_OK);
        QCOMPARE(files->sourcePanel->RefreshListCtrl(), S_OK);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "Alpha.txt") >= 0);
        files->selectRow(rowFor(*files, "Alpha.txt"));
        action(window, "cutFilesAction")->trigger();
        const auto urls = QApplication::clipboard()->mimeData()->urls();
        QCOMPARE(urls.size(), 1);
        QCOMPARE(urls.first().toLocalFile(), alias + "/Alpha.txt");
        QTRY_VERIFY(cut(*files, "Alpha.txt"));
#endif
    }

    void fileStateGeometry_data() {
        QTest::addColumn<bool>("icons");
        QTest::newRow("table") << false;
        QTest::newRow("icons") << true;
    }
    void fileStateGeometry() {
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        const QString selectedName = QStringLiteral("Рабочие материалы и описание проекта.txt");
        for (const auto &name : {selectedName, QString("Beta.txt"), QString("Gamma.txt")})
            write(fixture.filePath(name));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "Gamma.txt") >= 0);
        if (icons)
            action(window, "viewMode0Action")->trigger();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(window.findChild<QListView *>("iconList"))
                  : files;
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        files->selectRow(rowFor(*files, selectedName));
        files->setCurrentCell(rowFor(*files, "Beta.txt"), files->columnForProperty(kpidSize),
                              QItemSelectionModel::NoUpdate);
        // Current names the record, even when the table left a metadata column active.
        const QImage metadataCurrent = view->viewport()->grab().toImage();
        files->setCurrentCell(rowFor(*files, "Beta.txt"), 0, QItemSelectionModel::NoUpdate);
        QCOMPARE(view->viewport()->grab().toImage(), metadataCurrent);
        const QModelIndex selected = files->model()->index(rowFor(*files, selectedName), 0);
        QVERIFY(selected.data(Qt::ToolTipRole).toString().contains(selectedName));
        for (int width : {980, 640}) {
            window.resize(width, 620);
            QApplication::processEvents();
            const QImage before = view->viewport()->grab().toImage();
            action(window, "cutFilesAction")->trigger();
            QTRY_VERIFY(selected.data(QtPanelList::CutRole).toBool());
            const QImage after = view->viewport()->grab().toImage();
            const QRect area = view->visualRect(selected);
            const qreal ratio = after.devicePixelRatio();
            const QRect pixels(qRound(area.x() * ratio), qRound(area.y() * ratio),
                               qRound(area.width() * ratio), qRound(area.height() * ratio));
            QVERIFY(before.copy(pixels) != after.copy(pixels));
            const QPoint background(qRound((area.left() + 4) * ratio),
                                    qRound((area.center().y()) * ratio));
            QCOMPARE(before.pixelColor(background), after.pixelColor(background));
            QVERIFY(view->selectionModel()->isSelected(selected));
            QCOMPARE(view->currentIndex().siblingAtColumn(0).data().toString(),
                     QString("Beta.txt"));
            QVERIFY(selected.flags().testFlag(Qt::ItemIsEnabled));
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(
                window, QString("file-state-%1-%2").arg(icons ? "icons" : "table").arg(width)));
            QApplication::clipboard()->clear();
            QTRY_VERIFY(!selected.data(QtPanelList::CutRole).toBool());
        }
    }
};

QTEST_MAIN(WorkspaceFileStatesTests)
#include "workspace_file_states_tests.moc"
