#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "settings/RegistryUtils.h"
#include <QScopeGuard>

class SourceSelectionTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static QStringList selected(CPanel &panel) {
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_Selected(indices);
        QStringList names;
        FOR_VECTOR(i, indices)
        names.append(ToQString(panel.GetItemName(indices[i])));
        names.sort();
        return names;
    }
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data(Qt::UserRole + 3).toString() == name)
                return i;
        return -1;
    }

  private slots:
    void refreshKeepsEachPanelsSelection_data() {
        QTest::addColumn<bool>("alternative");
        QTest::newRow("standard") << false;
        QTest::newRow("alternative") << true;
    }

    void refreshKeepsEachPanelsSelection() {
        QFETCH(bool, alternative);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.AlternativeSelection = alternative;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList folders{fixture.filePath(QStringLiteral("левая папка 空")),
                                  fixture.filePath(QStringLiteral("правая папка 空"))};
        for (const QString &folder : folders) {
            QVERIFY(QDir().mkpath(folder));
            for (int i = 0; i < 270; ++i) {
                QFile file(QDir(folder).filePath(QString("item-%1.txt").arg(i, 3, 10, QChar('0'))));
                QVERIFY(file.open(QIODevice::WriteOnly));
            }
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        FileTable *views[2];
        const QStringList marks{"item-010.txt", "item-020.txt"};
        const QStringList cursors{"item-011.txt", "item-021.txt"};
        for (unsigned pane = 0; pane < 2; ++pane) {
            views[pane] = openPanel(window, pane, folders[int(pane)]);
            QTRY_VERIFY(views[pane]->isEnabled() && views[pane]->rowCount() == 270);
            action(window, "viewMode3Action")->trigger();
            auto &panel = *views[pane]->sourcePanel;
            panel.Set_AutoRefresh_Mode(false);
            const int cursor = row(*views[pane], cursors[int(pane)]);
            QVERIFY(cursor >= 0);
            views[pane]->setCurrentCell(cursor, 0);
            panel.KillSelection();
            MarkSelectionForTest(panel, ToUString(marks[int(pane)]));
            QCOMPARE(selected(panel), QStringList{marks[int(pane)]});
        }
        for (auto *view : views) {
            view->setFocus();
            QTRY_VERIFY(view->hasFocus());
            QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
            action(window, "refreshAction")->trigger();
        }
        for (unsigned pane = 0; pane < 2; ++pane) {
            auto *view = views[pane];
            QTRY_VERIFY(view->isEnabled() && view->rowCount() == 270);
            QTRY_COMPARE(selected(*view->sourcePanel), QStringList{marks[int(pane)]});
            QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                     cursors[int(pane)]);
        }
        auto *peer = window.findChild<QWidget *>("filePanel1");
        QVERIFY(peer->isAncestorOf(window.focusWidget()));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceSelectionTests)
#include "source_selection_tests.moc"
