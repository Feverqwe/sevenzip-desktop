#include "filemanager/panels/qtpaneldragmodel.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/drag_event_position.h"
#include "ui/support/layout_inspection.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QMimeData>
#include <QTableWidget>
#include <new>

namespace {
class DropPresentation final : public IQtDropTargetPresentation {
  public:
    unsigned pane = 0;
    QList<bool> highlights;
    int PanelAtPoint(int, int) const override { return int(pane); }
    bool PanelEnabled(unsigned) const override { return true; }
    int FolderAtPoint(unsigned, int, int, UString &name) const override {
        name = L"destination 文件";
        return 0;
    }
    void Highlight(unsigned, int, bool enabled) override { highlights.append(enabled); }
    bool IsSameDrive(const UString &, const UStringVector &) const override { return true; }
};
} // namespace

class PanelRefreshDropTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()
                    ->index(i, 0)
                    .data(Qt::UserRole + 3)
                    .toString()
                    .normalized(QString::NormalizationForm_C) ==
                name.normalized(QString::NormalizationForm_C))
                return i;
        return -1;
    }
  private slots:
    void liveRefreshReleasesDropHighlight_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("restoreOnly");
        for (unsigned pane : {0u, 1u})
            for (bool icons : {false, true})
                for (bool russian : {false, true})
                    for (bool restoreOnly : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(pane)
                                                     .arg(icons)
                                                     .arg(russian)
                                                     .arg(restoreOnly)))
                            << pane << icons << russian << restoreOnly;
    }
    void liveRefreshReleasesDropHighlight() {
        QFETCH(unsigned, pane);
        QFETCH(bool, icons);
        QFETCH(bool, russian);
        QFETCH(bool, restoreOnly);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString(russian ? L"ru" : L"-"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString destination = QStringLiteral("destination 文件");
        const QString control = QStringLiteral("control δ.txt");
        const QString peerPath = root.filePath(QStringLiteral("peer δ"));
        QVERIFY(QDir().mkpath(root.filePath(destination)));
        QVERIFY(QDir().mkpath(peerPath));
        QFile bytes(root.filePath(control));
        QVERIFY(bytes.open(QIODevice::WriteOnly));
        QCOMPARE(bytes.write("unchanged"), qint64(9));
        bytes.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        QVERIFY(owner && peer);
        for (QWidget *widget : {peer, owner}) {
            auto *address = widget->findChild<QLineEdit *>("archivePath");
            address->setFocus();
            address->setText(widget == owner ? root.path() : peerPath);
            QTest::keyClick(address, Qt::Key_Return);
        }
        auto *table = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto *peerTable = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        CMyComPtr<IFolderFolder> peerProvider = peerTable->sourcePanel->GetFolder();
        const UString peerPrefix = peerTable->sourcePanel->GetCurrentFolderPrefix();
        QTRY_VERIFY(table->isEnabled() && row(*table, control) >= 0);
        table->selectRow(row(*table, control));
        table->setCurrentCell(row(*table, control), 0);
        if (icons)
            action(window, "viewMode0Action")->trigger();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : table;
        QVERIFY(view && view->isVisible());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(bytes.fileName())});
        auto enter = [&] {
            const QPoint point =
                view->visualRect(table->model()->index(row(*table, destination), 0)).center();
            QDragEnterEvent event(DragEventPosition(point), Qt::CopyAction, &mime, Qt::LeftButton,
                                  Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &event);
            return event.isAccepted();
        };
        QVERIFY(enter());
        QVERIFY(
            table->model()->index(row(*table, destination), 0).data(Qt::UserRole + 19).toBool());
        if (restoreOnly) {
            CSelectedState state;
            table->sourcePanel->SaveSelectedState(state);
            QCOMPARE(table->sourcePanel->RefreshListCtrl(state), S_OK);
        } else
            action(window, "refreshAction")->trigger();
        QTRY_VERIFY(table->isEnabled() && row(*table, destination) >= 0);
        QVERIFY(
            !table->model()->index(row(*table, destination), 0).data(Qt::UserRole + 19).toBool());
        QVERIFY(table->updatesEnabled() && view->updatesEnabled());
        QCOMPARE(table->model()
                     ->index(table->currentRow(), 0)
                     .data(Qt::UserRole + 3)
                     .toString()
                     .normalized(QString::NormalizationForm_C),
                 control.normalized(QString::NormalizationForm_C));
        QCOMPARE(peerTable->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerTable->sourcePanel->GetCurrentFolderPrefix(), peerPrefix);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(view->viewport(), &leave);
        QVERIFY(enter());
        QVERIFY(
            table->model()->index(row(*table, destination), 0).data(Qt::UserRole + 19).toBool());
        QCoreApplication::sendEvent(view->viewport(), &leave);
        QVERIFY(
            !table->model()->index(row(*table, destination), 0).data(Qt::UserRole + 19).toBool());
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        QCOMPARE(bytes.readAll(), QByteArray("unchanged"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("source-refresh-drop-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
    void dragHoverResumesAfterRefresh() {
        QTemporaryDir root, source;
        QVERIFY(root.isValid() && source.isValid());
        QVERIFY(QDir().mkdir(root.filePath("destination")));
        QFile input(source.filePath("input.txt"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        input.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(root.path());
        auto *files = table(window);
        QTRY_VERIFY(row(*files, "destination") >= 0);
        files->sourcePanel->Set_AutoRefresh_Mode(false);
        for (int i = 0; i < 257; ++i) {
            QFile file(root.filePath(QString("entry-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto targetPoint = [&] {
            return files->visualRect(files->model()->index(row(*files, "destination"), 0)).center();
        };
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(input.fileName())});
        const QPoint point = targetPoint();
        QDragEnterEvent enter(DragEventPosition(point), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(files->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        auto *refresh = action(window, "refreshAction");
        refresh->trigger();
        QVERIFY(!refresh->isEnabled());
        QDragMoveEvent blocked(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(files->viewport(), &blocked);
        QVERIFY(!blocked.isAccepted());
        QTRY_VERIFY(refresh->isEnabled() && row(*files, "destination") >= 0);
        QDragMoveEvent resumed(targetPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                               Qt::NoModifier);
        QCoreApplication::sendEvent(files->viewport(), &resumed);
        QVERIFY(resumed.isAccepted());
        QVERIFY(files->model()
                    ->index(row(*files, "destination"), 0)
                    .data(QtPanelList::DropHighlightedRole)
                    .toBool());
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(files->viewport(), &leave);
    }
    void sourceRefreshClearsTarget_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<int>("stage");
        QTest::addColumn<int>("result");
        for (unsigned pane : {0u, 1u})
            for (int stage : {0, 1, 2})
                for (int result : {0, 1, 2})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(stage).arg(result)))
                        << pane << stage << result;
    }
    void sourceRefreshClearsTarget() {
        QFETCH(unsigned, pane);
        QFETCH(int, stage);
        QFETCH(int, result);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("destination 文件"))));
        DropPresentation presentation;
        presentation.pane = pane;
        CApp app;
        app.NumPanels = 2;
        auto &panel = app.Panels[pane];
        auto *provider = new NFsFolder::CFSFolder(root.path());
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        QTableWidget list(1, 1);
        auto *item = new QTableWidgetItem(QStringLiteral("destination 文件"));
        item->setData(Qt::UserRole + 6, 0u);
        list.setItem(0, 0, item);
        panel.AttachSelectionView(&list);
        const HRESULT expected = result == 0   ? S_OK
                                 : result == 1 ? (stage == 2 ? E_OUTOFMEMORY : E_FAIL)
                                               : E_ABORT;
        // Seed a completed transport result before testing the restoration stage.
        if (stage == 0)
            QCOMPARE(panel.LoadListItems(true, expected), expected);
        app.DropTarget().AttachPresentation(&presentation);
        UStringVector paths;
        paths.Add(ToUString(root.filePath(QStringLiteral("source δ.txt"))));
        app.DropTarget().LoadNames_From_DataObject(paths, false);
        app.DropTarget().PositionCursor(0, 0);
        const UString base = panel.GetFsPath();
        QVERIFY(!base.IsEmpty());
        UString destination = base;
        destination += L"destination 文件";
        destination.Add_PathSepar();
        QCOMPARE(app.DropTarget().GetTargetPath(), destination);
        QCOMPARE(presentation.highlights, QList<bool>({true}));
        if (stage == 2) {
            provider->CheckBreak = [result](const QString &) {
                if (result == 1)
                    throw std::bad_alloc();
                return result != 2;
            };
            QCOMPARE(panel.LoadListItems(), expected);
        } else if (stage == 1)
            QCOMPARE(panel.LoadListItems(true, expected), expected);
        else
            QCOMPARE(panel.RefreshListCtrl(), expected);
        QCOMPARE(app.DropTarget().GetTargetPath(), base);
        // A later leave must not clear an unrelated row in the refreshed list.
        QCOMPARE(app.DropTarget().DragLeave(), S_OK);
        QCOMPARE(presentation.highlights, QList<bool>({true}));
    }
};

QTEST_MAIN(PanelRefreshDropTests)
#include "panel_refresh_drop_tests.moc"
