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
        QTest::addColumn<bool>("dots");
        QTest::addColumn<int>("mode");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool dots : {false, true})
                    for (int mode : {0, 1})
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(index).arg(language).arg(dots).arg(mode)))
                            << index << language << dots << mode;
    }
    void reloadUsesSourceParentSelection() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, dots);
        QFETCH(int, mode);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/ShowDots", dots);
        settings.sync();
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
        QList<QUrl> opened;
        MainWindow window(&backend, [&](const QUrl &url) {
            opened.append(url);
            return true;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peerView = panelView(window, 1 - sourceIndex);
        navigate(*panelAddress(window, 1 - sourceIndex), peer);
        QTRY_COMPARE(peerView->rowCount(), 1 + int(dots));
        peerView->selectRow(int(dots));
        peerView->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerFolder = peerView->sourcePanel->GetFolder();
        auto *view = panelView(window, sourceIndex);
        auto *address = panelAddress(window, sourceIndex);
        navigate(*address, folder);
        QTRY_COMPARE(view->rowCount(), count + int(dots));
        QTRY_VERIFY(view->sourcePanel->ShouldProcessNotifications());
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        for (bool timer : {false, true}) {
            view->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
            view->clearSelection();
            panel.OnReload(timer);
            QTRY_COMPARE(view->rowCount(), count + int(dots));
            QTRY_VERIFY(panel.ShouldProcessNotifications());
            QCOMPARE(view->currentRow(), 0);
            QCOMPARE(view->selectionModel()->isRowSelected(0, QModelIndex()), dots);
            CSelectedState restored;
            panel.SaveSelectedState(restored);
            QCOMPARE(restored.SelectFocused, dots);
            if (dots) {
                QCOMPARE(view->model()->index(0, 0).data().toString(), QString(".."));
                QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 6).toUInt(), UInt32(-1));
                QVERIFY(
                    !view->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
                QCOMPARE(view->model()->index(0, 0).data(Qt::ToolTipRole).toString(),
                         language == "ru" ? QStringLiteral("Родительская папка")
                                          : QStringLiteral("Parent folder"));
            }
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
        if (dots) {
            view->setFocus();
            QVERIFY(action(window, "openOutsideAction")->isEnabled());
            action(window, "openOutsideAction")->trigger();
            QCOMPARE(opened.size(), 1);
            QCOMPARE(QDir::cleanPath(opened.front().toLocalFile()), fixture.path());
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), folder);
            view->setFocus();
            QVERIFY(action(window, "openItemAction")->isEnabled());
            PanelKeys::openSelection(view);
            QTRY_COMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())),
                         fixture.path());
        }
        QCOMPARE(opened.size(), int(dots));
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
