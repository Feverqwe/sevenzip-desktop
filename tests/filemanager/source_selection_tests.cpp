#include "7zip/UI/FileManager/resource.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
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
            panel.SelectSpec(ToUString(marks[int(pane)]), true);
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

    void selection_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("alternative");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool alternative : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(alternative)))
                        << pane << language << alternative;
    }
    void selection() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(bool, alternative);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.AlternativeSelection = alternative;
        preferences.Save(settings);
        QTemporaryDir fixture, peerDirectory;
        QVERIFY(fixture.isValid());
        QVERIFY(peerDirectory.isValid());
        const QString first = QStringLiteral("first 空.txt");
        const QString second = QStringLiteral("second 空.txt");
        for (const QString &name : {first, second, QString("keep.bin")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected input"), qint64(15));
        }
        QVERIFY(QDir(fixture.path()).mkdir("folder.txt"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        QTRY_COMPARE(view->rowCount(), 4);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        view->setCurrentCell(row(*view, "keep.bin"), 0);
        view->selectRow(row(*view, "keep.bin"));
        panel.SelectSpec(L"keep.bin", true);
        QCOMPARE(selected(panel), QStringList{"keep.bin"});
        const int focus = view->currentRow();
        bool observed = false;
        const auto answer = [&](bool selectMode, bool accept, const QString &mask, bool reports) {
            observed = false;
            QTimer::singleShot(0, &window, [&, selectMode, accept, mask, reports] {
                auto *dialog = window.findChild<CComboDialog *>("comboDialog");
                if (!dialog)
                    return;
                const auto close = qScopeGuard([&] {
                    if (dialog->isVisible())
                        dialog->reject();
                });
                observed = true;
                QCOMPARE(dialog->windowTitle(),
                         ToQString(LangString(selectMode ? IDS_SELECT : IDS_DESELECT)));
                QCOMPARE(dialog->findChild<QLabel *>("comboPrompt")->text(),
                         ToQString(LangString(IDS_SELECT_MASK)));
                auto *combo = dialog->findChild<QComboBox *>("comboValue");
                QCOMPARE(combo->currentText(), QString("*"));
                if (reports)
                    for (int width : {360, 260}) {
                        dialog->resize(width, 150);
                        dialog->layout()->activate();
                        QVERIFY(UiInspection::fullyVisible(*combo, *dialog));
                        QVERIFY(
                            UiInspection::labelFits(*dialog->findChild<QLabel *>("comboPrompt")));
                        QVERIFY(UiInspection::writeReport(*dialog,
                                                          QString("source-selection-%1-%2-%3-%4-%5")
                                                              .arg(pane)
                                                              .arg(language)
                                                              .arg(alternative)
                                                              .arg(selectMode)
                                                              .arg(width)));
                    }
                combo->setEditText(mask);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
        };
        answer(true, false, "*", false);
        action(window, "selectMaskAction")->trigger();
        QVERIFY(observed);
        QCOMPARE(selected(panel), QStringList{"keep.bin"});
        QCOMPARE(view->currentRow(), focus);
        answer(true, true, QStringLiteral("*空.txt"), true);
        action(window, "selectMaskAction")->trigger();
        QVERIFY(observed);
        QCOMPARE(selected(panel), (QStringList{first, "keep.bin", second}));
        answer(false, true, "keep.*", true);
        action(window, "deselectMaskAction")->trigger();
        QVERIFY(observed);
        QCOMPARE(selected(panel), (QStringList{first, second}));
        action(window, "deselectAllAction")->trigger();
        QVERIFY(selected(panel).isEmpty());
        view->setCurrentCell(row(*view, first), 0);
        action(window, "selectTypeAction")->trigger();
        QCOMPARE(selected(panel), (QStringList{first, second}));
        action(window, "deselectTypeAction")->trigger();
        QVERIFY(selected(panel).isEmpty());
        action(window, "selectAllAction")->trigger();
        QCOMPARE(selected(panel).size(), 4);
        action(window, "invertSelectionAction")->trigger();
        QVERIFY(selected(panel).isEmpty());
        peer->setFocus();
        answer(true, true, first, false);
        panel.SelectSpec(true);
        QVERIFY(observed);
        QCOMPARE(selected(panel), QStringList{first});
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QVERIFY(selected(*peer->sourcePanel).isEmpty());
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(backend.extractCalls, 0);
        for (const QString &name : {first, second, QString("keep.bin")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("protected input"));
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceSelectionTests)
#include "source_selection_tests.moc"
