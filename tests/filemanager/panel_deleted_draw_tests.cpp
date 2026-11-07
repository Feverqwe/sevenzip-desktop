#include "archive/support/deleted_folder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/color_contrast.h"
#include "ui/support/layout_inspection.h"

class PanelDeletedDrawTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void owningProviderColorsDeletedItems_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(russian).arg(icons)))
                        << pane << russian << icons;
    }
    void owningProviderColorsDeletedItems() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile control(fixture.filePath(QStringLiteral("control 文件.txt")));
        QVERIFY(control.open(QIODevice::WriteOnly));
        QCOMPARE(control.write("untouched"), qint64(9));
        control.close();
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.Save(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        auto *iconView = ui->findChild<QListView *>("iconList");
        auto *navigation = ui->findChild<QWidget *>(QString("panelNavigation%1").arg(pane));
        QVERIFY(navigation);
        view->setFocus();
        action(window, "openRootAction")->trigger();
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *spec = new DeletedFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        panel.OnReload();
        QCOMPARE(view->rowCount(), 2);
        panel.SelectAll(true);
        CRecordVector<UInt32> marks;
        panel.Get_ItemIndices_Selected(marks);
        QCOMPARE(marks.Size(), 2u);
        const int focus = view->currentRow();
        int deletedRow = -1;
        int ordinaryRow = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 0 ? deletedRow
                                                                               : ordinaryRow) = row;
        QVERIFY(deletedRow >= 0 && ordinaryRow >= 0);
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerView = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        CMyComPtr<IFolderFolder> peerProvider = peerView->sourcePanel->GetFolder();
        peerView->sourcePanel->Set_AutoRefresh_Mode(false);
        const int peerRows = peerView->rowCount();
        for (int column = 0; column < view->columnCount(); ++column) {
            const QColor foreground = view->model()
                                          ->index(deletedRow, column)
                                          .data(Qt::ForegroundRole)
                                          .value<QBrush>()
                                          .color();
            const QColor background = view->palette().color(QPalette::Base);
            QVERIFY(foreground.red() > foreground.green());
            QVERIFY(foreground.red() > foreground.blue());
            QVERIFY(textContrast(foreground, background) >= 4.5);
            QVERIFY(!view->model()->index(ordinaryRow, column).data(Qt::ForegroundRole).isValid());
        }
        QCOMPARE(
            qvariant_cast<QBrush>(iconView->model()->index(deletedRow, 0).data(Qt::ForegroundRole))
                .color(),
            view->model()->index(deletedRow, 0).data(Qt::ForegroundRole).value<QBrush>().color());
        const unsigned queries = spec->deletedQueries;
        {
            CPanel::CDisableNotify guard(panel);
            QVERIFY(!view->model()->index(deletedRow, 0).data(Qt::ForegroundRole).isValid());
            QCOMPARE(spec->deletedQueries, queries);
        }
        const auto savedTextFolder = view->textFolder;
        view->textFolder = peerProvider;
        QVERIFY(!view->model()->index(deletedRow, 0).data(Qt::ForegroundRole).isValid());
        QCOMPARE(spec->deletedQueries, queries);
        view->textFolder = savedTextFolder;
        spec->deletedResult = E_FAIL;
        QVERIFY(!view->model()->index(deletedRow, 0).data(Qt::ForegroundRole).isValid());
        spec->deletedResult = S_OK;
        QCOMPARE(view->currentRow(), focus);
        CRecordVector<UInt32> after;
        panel.Get_ItemIndices_Selected(after);
        QCOMPARE(after.Size(), marks.Size());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QWidget &visibleView =
                icons ? static_cast<QWidget &>(*iconView) : static_cast<QWidget &>(*view);
            QVERIFY(UiInspection::fullyVisible(visibleView, window));
            QVERIFY(UiInspection::fullyVisible(*navigation, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("deleted-draw-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
            const QImage rendered = visibleView.grab().toImage();
            bool redText = false;
            for (int y = 0; y < rendered.height() && !redText; ++y)
                for (int x = 0; x < rendered.width(); ++x) {
                    const QColor pixel = rendered.pixelColor(x, y);
                    if (pixel.red() > pixel.green() * 1.4 && pixel.red() > pixel.blue() * 1.4) {
                        redText = true;
                        break;
                    }
                }
            QVERIFY2(redText, "Deleted-item emphasis must survive selected-text paint");
        }
        spec->archiveHasDeleted = false;
        panel.OnReload();
        const unsigned before = spec->deletedQueries;
        for (int row = 0; row < view->rowCount(); ++row)
            QVERIFY(!view->model()->index(row, 0).data(Qt::ForegroundRole).isValid());
        QCOMPARE(spec->deletedQueries, before);
        spec->archiveHasDeleted = true;
        spec->firstDeleted = false;
        panel.OnReload();
        for (int row = 0; row < view->rowCount(); ++row)
            QVERIFY(!view->model()->index(row, 0).data(Qt::ForegroundRole).isValid());
        panel.SetNewFolder(new LoadFolder);
        panel.OnReload();
        for (int row = 0; row < view->rowCount(); ++row)
            QVERIFY(!view->model()->index(row, 0).data(Qt::ForegroundRole).isValid());
        QCOMPARE(peerView->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peerView->rowCount(), peerRows);
        QCOMPARE(backend.listCalls, 0);
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("untouched"));
    }
    void sourceVariantAndParentPolicy() {
        auto *spec = new DeletedFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        CPanel panel(provider);
        QCOMPARE(panel.LoadListItems(), HRESULT(S_OK));
        QVERIFY(panel.IsItem_Deleted(0));
        QCOMPARE(panel.OnCustomDraw(0), QColor(255, 0, 0));
        const unsigned queries = spec->deletedQueries;
        QVERIFY(!panel.IsItem_Deleted(UInt32(-1)));
        QVERIFY(!panel.OnCustomDraw(UInt32(-1)).isValid());
        QCOMPARE(spec->deletedQueries, queries);
        spec->firstDeleted = false;
        QVERIFY(!panel.IsItem_Deleted(0));
        spec->deletedType = VT_EMPTY;
        QVERIFY(!panel.IsItem_Deleted(0));
        for (const HRESULT failure : {E_FAIL, S_OK}) {
            spec->deletedResult = failure;
            spec->deletedType = VT_UI4;
            bool caught = false;
            try {
                panel.IsItem_Deleted(0);
            } catch (int) {
                caught = true;
            }
            QVERIFY(caught);
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PanelDeletedDrawTests)
#include "panel_deleted_draw_tests.moc"
