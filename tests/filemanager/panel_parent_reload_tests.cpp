#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"

class PanelParentReloadTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static bool writeFile(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("control") == 7;
    }
    static FileTable *panelView(MainWindow &window, unsigned index) {
        return static_cast<FileTable *>(
            window.findChild<QWidget *>(QString("filePanel%1").arg(index))
                ->findChild<FileTable *>("archiveTable"));
    }
    static QLineEdit *panelAddress(MainWindow &window, unsigned index) {
        return window.findChild<QWidget *>(QString("filePanel%1").arg(index))
            ->findChild<QLineEdit *>("archivePath");
    }
    static void navigate(QLineEdit &address, const QString &path) {
        PanelKeys::focusAddress(&address);
        address.setText(path);
        QTest::keyClick(&address, Qt::Key_Return);
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void reloadUsesSourceParentSelection_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("mode");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (int mode : {0, 1})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(index).arg(language).arg(mode)))
                        << index << language << mode;
    }
    void reloadUsesSourceParentSelection() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(int, mode);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString folder = fixture.filePath(QStringLiteral("browse space 文件"));
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(folder) && QDir().mkpath(peer));
        QVERIFY(writeFile(QDir(peer).filePath("peer.txt")));
        const int count = mode == 1 ? 301 : 1;
        for (int i = 0; i < count; ++i)
            QVERIFY(writeFile(QDir(folder).filePath(QString("control-%1.txt").arg(i))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peerView = panelView(window, 1 - sourceIndex);
        navigate(*panelAddress(window, 1 - sourceIndex), peer);
        QTRY_COMPARE(peerView->rowCount(), 1);
        peerView->selectRow(0);
        peerView->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerFolder = peerView->sourcePanel->GetFolder();
        auto *view = panelView(window, sourceIndex);
        auto *address = panelAddress(window, sourceIndex);
        navigate(*address, folder);
        QTRY_COMPARE(view->rowCount(), count);
        QTRY_VERIFY(view->sourcePanel->ShouldProcessNotifications());
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        for (bool timer : {false, true}) {
            view->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
            view->clearSelection();
            panel.OnReload(timer);
            QTRY_COMPARE(view->rowCount(), count);
            QTRY_VERIFY(panel.ShouldProcessNotifications());
            QCOMPARE(view->currentRow(), 0);
            QVERIFY(!view->selectionModel()->isRowSelected(0, QModelIndex()));
            CSelectedState restored;
            panel.SaveSelectedState(restored);
            QVERIFY(!restored.SelectFocused);
            CRecordVector<UInt32> indices;
            panel.Get_ItemIndices_Selected(indices);
            QVERIFY(indices.IsEmpty());
            panel.Get_ItemIndices_Operated(indices);
            QVERIFY(indices.IsEmpty());
            QCOMPARE(peerView->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
            QCOMPARE(peerView->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(panelAddress(window, 1 - sourceIndex)->text(), peer);
            QCOMPARE(QDir::cleanPath(
                         QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix()))),
                     folder);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(window.windowTitle())), folder);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::writeReport(window, QString("parent-reload-%1-%2-%3")
                                                              .arg(QTest::currentDataTag())
                                                              .arg(timer)
                                                              .arg(size.width())));
            }
        }
        PanelKeys::focusAddress(address);
        action(window, "parentFolderAction")->trigger();
        QTRY_COMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), fixture.path());
        QTRY_COMPARE(window.focusWidget(), view);
        PanelKeys::openSelection(window.focusWidget());
        QTRY_COMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), folder);
        QTRY_COMPARE(view->rowCount(), count);
        for (const QString path :
             {QDir(folder).filePath("control-0.txt"), QDir(peer).filePath("peer.txt")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("control"));
        }
        QCOMPARE(backend.listCalls, 0);
    }
};

QTEST_MAIN(PanelParentReloadTests)
#include "panel_parent_reload_tests.moc"
