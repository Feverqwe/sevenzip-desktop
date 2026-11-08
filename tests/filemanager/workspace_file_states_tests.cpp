#include "filemanager/support/window_state_support.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QImage>
#include <QScopeGuard>
#include <QStatusBar>

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

  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QApplication::clipboard()->clear();
    }
    void cleanup() { QApplication::clipboard()->clear(); }

    void keyboardClipboardWorksWhileThePeerRefreshes_data() {
        QTest::addColumn<bool>("move");
        QTest::newRow("copy") << false;
        QTest::newRow("cut") << true;
    }
    void keyboardClipboardWorksWhileThePeerRefreshes() {
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString target = fixture.filePath("target");
        const QString peerPath = fixture.filePath("peer");
        for (const auto &path : {source, target, peerPath})
            QVERIFY(QDir().mkpath(path));
        const QString input = QDir(source).filePath("item.txt");
        write(input);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(source);
        auto *files = table(window);
        QTRY_COMPARE(files->rowCount(), 1);
        files->selectRow(0);
        action(window, "twoPanelsAction")->trigger();
        auto *peerPane = window.findChild<QWidget *>("filePanel1");
        QVERIFY(peerPane && peerPane->isVisible());
        auto *peer = peerPane->findChild<FileTable *>("archiveTable");
        auto *address = peerPane->findChild<QLineEdit *>("archivePath");
        QVERIFY(peer && address);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        window.openPath(peerPath);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 0);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        for (int i = 0; i < 301; ++i)
            write(QDir(peerPath).filePath(QString("peer-%1.txt").arg(i)));

        const auto startPeerRefresh = [&] {
            peer->setFocus();
            QVERIFY(peer->hasFocus());
            QTest::keySequence(peer, action(window, "refreshAction")->shortcut());
            QVERIFY(!peer->isEnabled());
            files->setFocus();
            QVERIFY(files->hasFocus());
        };
        startPeerRefresh();
        QTest::keySequence(files, move ? QKeySequence::Cut : QKeySequence::Copy);
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), QList{QUrl::fromLocalFile(input)});
        QCOMPARE(cut(*files, "item.txt"), move);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 301);

        QString report;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            if (auto *result =
                    TestProgressDialog::ResultWindow(QApplication::activeModalWidget())) {
                report = TestProgressDialog::ReportText(*result);
                TestProgressDialog::CloseResult(*result);
            }
        });
        dismiss.start(5);
        QTest::keySequence(files, QKeySequence::Paste);
        QTRY_VERIFY((!report.isEmpty() || !window.statusBar()->currentMessage().isEmpty()) &&
                    files->isEnabled());
        dismiss.stop();
        QVERIFY(report.isEmpty());
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Source and destination are the same:")));
        QCOMPARE(files->rowCount(), 1);
        QVERIFY(QFileInfo::exists(input));
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), QList{QUrl::fromLocalFile(input)});
        QCOMPARE(cut(*files, "item.txt"), move);
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(files->hasFocus());

        window.openPath(target);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() == 0);
        write(QDir(peerPath).filePath("new.txt"));
        startPeerRefresh();
        QTest::keySequence(files, QKeySequence::Paste);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "item.txt") >= 0);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 302);
        QFile output(QDir(target).filePath("item.txt"));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("file state fixture"));
        QCOMPARE(QFileInfo::exists(input), !move);
        if (move) {
            const auto *mime = QApplication::clipboard()->mimeData();
            QVERIFY(!mime || !mime->hasUrls());
        }
        QVERIFY(WindowActivation::waitForActive(window));
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(!peerPane->isVisible());
    }

    void cutFollowsFilesystemEntryAcrossViewsLocationsAndWindows() {
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
        window.openPath(peer);
        QTRY_COMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())),
                     peer);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "Alpha.txt") >= 0);
        QVERIFY(!cut(*files, "Alpha.txt"));
        window.openPath(source);
        QTRY_VERIFY(files->isEnabled() && cut(*files, "Alpha.txt"));
        const QString original = source + "/Alpha.txt";
        const QString renamed = source + "/Renamed.txt";
        QVERIFY(QFile::rename(original, renamed));
        for (auto *owner : {&window, &other})
            action(*owner, "refreshAction")->trigger();
        for (auto *view : {files, otherFiles}) {
            QTRY_VERIFY(rowFor(*view, "Renamed.txt") >= 0);
            QCOMPARE(rowFor(*view, "Alpha.txt"), -1);
            QVERIFY(!cut(*view, "Renamed.txt"));
            QVERIFY(!cut(*view, "Beta.txt"));
        }
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(),
                 QList{QUrl::fromLocalFile(original)});
        QVERIFY(QFile::rename(renamed, original));
        for (auto *owner : {&window, &other})
            action(*owner, "refreshAction")->trigger();
        QTRY_VERIFY(cut(*files, "Alpha.txt"));
        QTRY_VERIFY(cut(*otherFiles, "Alpha.txt"));
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(source + "/Alpha.txt")});
        mime->setData("x-special/gnome-copied-files", "copy\n");
        QApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY(!cut(*files, "Alpha.txt"));
        QTRY_VERIFY(!cut(*otherFiles, "Alpha.txt"));
        QVERIFY(QFileInfo::exists(source + "/Alpha.txt"));
    }

    void fileMutationsRequireWritableFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString writable = fixture.filePath("writable");
        const QString blocked = fixture.filePath("blocked");
        const QString source = fixture.filePath("incoming.txt");
        QVERIFY(QDir().mkpath(writable));
        QVERIFY(QDir().mkpath(blocked));
        write(source);
        write(QDir(blocked).filePath("existing.txt"));
        write(QDir(writable).filePath("existing.txt"));
        const auto restore = qScopeGuard([&] {
            QFile::setPermissions(blocked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        });
        if (!QFile::setPermissions(blocked, QFile::ReadOwner | QFile::ExeOwner) ||
            QFileInfo(blocked).isWritable())
            QSKIP("Cannot make the destination folder read-only");

        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(writable);
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(source)});
        QApplication::clipboard()->setMimeData(mime);
        auto *paste = action(window, "pasteFilesAction");
        auto *cut = action(window, "cutFilesAction");
        auto *copy = action(window, "copyFilesAction");
        const QList<QAction *> mutations{
            action(window, "moveAction"), action(window, "deleteAction"),
            action(window, "renameAction"), action(window, "createFolderAction")};
        QTRY_VERIFY(paste->isEnabled());

        window.openPath(blocked);
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "existing.txt") >= 0);
        QVERIFY(WindowActivation::waitForActive(window));
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        QTest::keySequence(files, QKeySequence::SelectAll);
        QVERIFY(copy->isEnabled());
        QVERIFY(!cut->isEnabled());
        QVERIFY(!paste->isEnabled());
        for (auto *mutation : mutations)
            QVERIFY2(!mutation->isEnabled(), qPrintable(mutation->objectName()));
        window.openPath(writable);
        QTRY_VERIFY(paste->isEnabled());
        QTRY_VERIFY(files->hasFocus());
        QTest::keySequence(files, QKeySequence::SelectAll);
        files->setCurrentCell(rowFor(*files, "existing.txt"), 0, QItemSelectionModel::NoUpdate);
        QVERIFY(copy->isEnabled());
        QVERIFY(cut->isEnabled());
        for (auto *mutation : mutations)
            QVERIFY2(mutation->isEnabled(), qPrintable(mutation->objectName()));
    }

    void pasteWaitsForTheDestinationListing() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("incoming.txt");
        const QString destination = fixture.filePath("large destination");
        write(source);
        QVERIFY(QDir().mkpath(destination));
        for (int i = 0; i < 257; ++i) {
            QFile entry(QDir(destination).filePath(QString::number(i)));
            QVERIFY(entry.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        auto *paste = action(window, "pasteFilesAction");
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(source)});
        QApplication::clipboard()->setMimeData(mime);
        QTRY_VERIFY(paste->isEnabled());
        openListedArchive(window, backend, fixture.filePath("view.zip"),
                          {ArchiveEntry{"inside.txt", 1, 1}});
        QTRY_VERIFY(files->isEnabled() && files->sourcePanel->IsArcFolder());
        QVERIFY(WindowActivation::waitForActive(window));
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        const int entry = rowFor(*files, "inside.txt");
        QVERIFY(entry >= 0);
        files->selectRow(entry);
        QVERIFY(action(window, "copyFilesAction")->isEnabled());
        QVERIFY(!action(window, "cutFilesAction")->isEnabled());
        QVERIFY(!paste->isEnabled());
        QTest::keySequence(files, QKeySequence::Cut);
        QTest::keySequence(files, QKeySequence::Paste);
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(),
                 QList<QUrl>{QUrl::fromLocalFile(source)});
        QCOMPARE(rowFor(*files, "incoming.txt"), -1);
        window.openPath(destination);
        QVERIFY(!files->isEnabled());
        QVERIFY(!paste->isEnabled());
        QTRY_VERIFY(files->isEnabled() && paste->isEnabled());
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(),
                 QList<QUrl>{QUrl::fromLocalFile(source)});
        QTRY_VERIFY(files->hasFocus());
        QTest::keySequence(files, QKeySequence::Paste);
        QFile output(QDir(destination).filePath("incoming.txt"));
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, "incoming.txt") >= 0);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("file state fixture"));
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

    void gnomeFileListKeepsCopyAndCutDistinct() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("External 空 cut.txt"));
        write(path);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        const QString name = QFileInfo(path).fileName();
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, name) >= 0);
        auto *paste = action(window, "pasteFilesAction");
        const auto putFiles = [](const QByteArray &data) {
            auto *mime = new QMimeData;
            mime->setData("x-special/gnome-copied-files", data);
            QApplication::clipboard()->setMimeData(mime);
        };
        const QByteArray url = QUrl::fromLocalFile(path).toEncoded();
        putFiles("copy\n" + url);
        QTRY_VERIFY(paste->isEnabled());
        putFiles("copy\n" + url + '\n');
        QTRY_VERIFY(paste->isEnabled());
        QVERIFY(!cut(*files, name));
        putFiles("cut\n" + url + '\n');
        QTRY_VERIFY(paste->isEnabled());
        QVERIFY(cut(*files, name));
        putFiles("cut\n" + url + "\n\n" + url);
        QTRY_VERIFY(!paste->isEnabled());
        QVERIFY(!cut(*files, name));
        putFiles("cut\n" + url + "\nhttps://example.invalid/file");
        QTRY_VERIFY(!paste->isEnabled());
        QVERIFY(!cut(*files, name));
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
        const QUrl local = QUrl::fromLocalFile(path);
        const auto putFilesOnClipboard = [&](const QList<QUrl> &urls) {
            auto *mime = new QMimeData;
            mime->setUrls(urls);
            mime->setData(
                format, format == "x-special/gnome-copied-files" ? hint + local.toEncoded() : hint);
            QApplication::clipboard()->setMimeData(mime);
        };
        auto *paste = action(window, "pasteFilesAction");
        putFilesOnClipboard({local});
        QTRY_VERIFY(cut(*files, "External cut.txt"));
        QTRY_VERIFY(paste->isEnabled());
        for (const QUrl &invalid :
             {QUrl("https://example.invalid/file"), QUrl::fromLocalFile("relative.txt")}) {
            putFilesOnClipboard({local, invalid});
            QTRY_VERIFY(!cut(*files, "External cut.txt"));
            QTRY_VERIFY(!paste->isEnabled());
            putFilesOnClipboard({local});
            QTRY_VERIFY(cut(*files, "External cut.txt"));
            QTRY_VERIFY(paste->isEnabled());
        }
        QApplication::clipboard()->clear();
        QTRY_VERIFY(!cut(*files, "External cut.txt"));
        QTRY_VERIFY(!paste->isEnabled());
    }

    void copyingAMissingFileKeepsThePreviousClipboard() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("selected file.txt");
        const QString path = fixture.filePath(name);
        const QString other = fixture.filePath("another file.txt");
        write(path);
        write(other);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && rowFor(*files, name) >= 0);
        files->sourcePanel->Set_AutoRefresh_Mode(false);
        const int otherRow = rowFor(*files, "another file.txt");
        QVERIFY(otherRow >= 0);
        files->selectRow(otherRow);
        files->selectionModel()->select(files->model()->index(rowFor(*files, name), 0),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
        auto *clipboard = QApplication::clipboard();
        clipboard->setText("previous clipboard text");
        auto *copy = action(window, "copyFilesAction");
        QTRY_VERIFY(copy->isEnabled());
        QVERIFY(QFile::remove(path));
        copy->trigger();
        QCOMPARE(clipboard->text(), QString("previous clipboard text"));
        QVERIFY(window.statusBar()->currentMessage().contains(name));
        write(path);
        copy->trigger();
        const auto urls = clipboard->mimeData()->urls();
        QCOMPARE(urls.size(), 2);
        QVERIFY(urls.contains(QUrl::fromLocalFile(path)));
        QVERIFY(urls.contains(QUrl::fromLocalFile(other)));
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
    }

    void pasteRejectsAnOpenedFolderReplacedByALink() {
#ifndef Q_OS_UNIX
        QSKIP("Requires filesystem symbolic links");
#endif
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = fixture.filePath("original");
        const QString parked = fixture.filePath("parked");
        const QString target = fixture.filePath("target");
        QVERIFY(QDir().mkdir(original) && QDir().mkdir(target));
        const QString source = fixture.filePath("payload.txt");
        write(source);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(original);
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled());
        files->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *paste = action(window, "pasteFilesAction");
        const auto publish = [&] {
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(source)});
            QApplication::clipboard()->setMimeData(mime);
        };
        publish();
        QTRY_VERIFY(paste->isEnabled());
        QVERIFY(QDir().rename(original, parked));
        QVERIFY(QFile::link(target, original));
        QVERIFY(QFileInfo(original).isSymLink());
        publish();
        QTRY_VERIFY(!paste->isEnabled());
        QVERIFY(QFile::remove(original));
        QVERIFY(QDir().rename(parked, original));
        publish();
        QTRY_VERIFY(paste->isEnabled());
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
