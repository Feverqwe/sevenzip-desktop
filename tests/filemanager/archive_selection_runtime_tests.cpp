#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "support/progress_dialog.h"
#include <QLineEdit>
#include <QTreeWidget>

class PathHashBackend : public ControlledBackend {
  public:
    void extractSourceEntry(const QString &archive, const QString &entry, const QString &output,
                            const QString &, const QString &password, bool = false) override {
        streamEntries.append(entry);
        streamPaths.append(output);
        extract(archive, output, {entry}, password);
        QTimer::singleShot(0, this, [this, output] {
            QFile file(output);
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7)
                fail("Cannot prepare stream fixture");
            else {
                file.close();
                completeExtraction();
            }
        });
    }
    QStringList streamEntries;
    QStringList streamPaths;
};

class ArchiveSelectionRuntimeTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static QStringList portablePaths(QStringList paths) {
        for (QString &path : paths)
            path = QDir::fromNativeSeparators(path);
        return paths;
    }
    static void selectOnly(MainWindow &window, int row) {
        QVERIFY(row >= 0);
        auto *view = table(window);
        view->clearSelection();
        view->selectionModel()->select(view->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        view->selectionModel()->setCurrentIndex(view->model()->index(row, 0),
                                                QItemSelectionModel::NoUpdate);
    }
    static void corruptPresentationPath(MainWindow &window, int row) {
        const QPersistentModelIndex item = table(window)->model()->index(row, 0);
        QVERIFY(!SetPanelData(item, QString("unrelated/display-only.txt"), Qt::UserRole));
        QVERIFY(!SetPanelData(item, false, Qt::UserRole + 1));
        QVERIFY(!SetPanelData(item, 9999u, Qt::UserRole + 4));
        // The source item parameter (+6) and source folder remain untouched.
    }
    static void closeNextDialog(MainWindow &window, const QString &destination, bool accepted) {
        QTimer::singleShot(0, &window, [destination, accepted] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (auto *combo = dialog->findChild<QComboBox *>("extractDestination")) {
                dialog->findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
                combo->setCurrentText(destination);
            }
            if (auto *combo = dialog->findChild<QLineEdit *>("copyDestination"))
                combo->setText(destination);
            if (accepted)
                dialog->accept();
            else
                dialog->reject();
        });
    }

  private slots:
    void successfulHashUsesSourceLogicalSelection_data() {
        sourcePathsAcrossViewsAndConsumers_data();
    }
    void successfulHashUsesSourceLogicalSelection() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        PathHashBackend backend;
        MainWindow window(&backend);
        action(window, "flatViewAction")->setChecked(false);
        const QString directory = QStringLiteral("папка й с пробелом");
        openListedArchive(window, backend, "source-hash.zip",
                          {{"root.bin", 3, 1}, {directory + "/inside.txt", 7, 3}});
        action(window, "flatViewAction")->setChecked(flat);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        const int directoryRow = rowWithPath(window, directory);
        const int rootRow = rowWithPath(window, "root.bin");
        QVERIFY(directoryRow >= 0 && rootRow >= 0);
        selectOnly(window, directoryRow);
        table(window)->selectionModel()->select(table(window)->model()->index(rootRow, 0),
                                                QItemSelectionModel::Select |
                                                    QItemSelectionModel::Rows);
        corruptPresentationPath(window, directoryRow);
        corruptPresentationPath(window, rootRow);
        int resultRows = 0;
        QTimer closeResults;
        connect(&closeResults, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<CListViewDialog *>(QApplication::activeModalWidget())) {
                resultRows =
                    dialog->findChild<QTreeWidget *>("propertiesList")->topLevelItemCount();
                dialog->accept();
            }
        });
        closeResults.start(1);
        action(window, "hashSHA256Action")->trigger();
        closeResults.stop();
        QVERIFY(resultRows > 0);
        QStringList expected = {directory + "/inside.txt", "root.bin"};
        // Original Agent::Extract does not recurse selected folders in flat
        // mode. Descendant files must be selected as their own flat items.
        if (flat)
            expected = {"root.bin"};
        QStringList actual = portablePaths(backend.streamEntries);
        expected.sort();
        actual.sort();
        QCOMPARE(actual, expected);
        for (const QString &path : backend.streamPaths)
            QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
        QVERIFY(action(window, "hashSHA256Action")->isEnabled());
        action(window, "flatViewAction")->setChecked(false);
    }

    void renameUsesSourceNameHintAndIgnoresForgedPath() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QCoreApplication::processEvents();
        action(window, "flatViewAction")->setChecked(false);
        openListedArchive(window, backend, "source-rename.zip",
                          {{"a/same.txt", 3, 1}, {"b/same.txt", 7, 3}});
        action(window, "flatViewAction")->setChecked(true);
        const int row = rowWithPath(window, "b/same.txt");
        QVERIFY(row >= 0);
        selectOnly(window, row);
        corruptPresentationPath(window, row);
        action(window, "renameAction")->trigger();
        auto *editor = table(window)->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setFocus();
        QCOMPARE(editor->text(), QString("same.txt"));
        QTest::keyClick(editor, Qt::Key_Escape);

        action(window, "flatViewAction")->setChecked(false);
    }

    void sourcePathsAcrossViewsAndConsumers_data() {
        QTest::addColumn<bool>("flat");
        QTest::addColumn<int>("mode");
        for (bool flat : {false, true})
            for (int mode : {0, 3})
                QTest::newRow(qPrintable(QString("flat%1-mode%2").arg(flat).arg(mode)))
                    << flat << mode;
    }
    void sourcePathsAcrossViewsAndConsumers() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "flatViewAction")->setChecked(false);
        const QString directory = QStringLiteral("папка й с пробелом");
        const QString first = directory + "/same.txt";
        const QString nested = directory + "/deep/same.txt";
        const QStringList expected = {"root.bin", first, nested};
        openListedArchive(
            window, backend, "owned-selection.zip",
            {{"root.bin", 11, 5}, {first, 13, 6}, {nested, 17, 8}, {"other/same.txt", 19, 9}});
        action(window, "flatViewAction")->setChecked(flat);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        const int directoryRow = rowWithPath(window, directory);
        const int rootRow = rowWithPath(window, "root.bin");
        QVERIFY(directoryRow >= 0 && rootRow >= 0);
        const int childRow = flat ? rowWithPath(window, nested) : -1;
        const auto selectRequest = [&] {
            selectOnly(window, directoryRow);
            for (const int row : {rootRow, childRow})
                if (row >= 0)
                    table(window)->selectionModel()->select(table(window)->model()->index(row, 0),
                                                            QItemSelectionModel::Select |
                                                                QItemSelectionModel::Rows);
        };
        selectRequest();
        if (flat) {
            QVERIFY(childRow >= 0);
            corruptPresentationPath(window, childRow);
        }
        corruptPresentationPath(window, directoryRow);
        corruptPresentationPath(window, rootRow);
        window.extractTo(destination.path(), true);
        QCOMPARE(backend.extractCalls, 1);
        QCOMPARE(portablePaths(backend.lastSelected), expected); // Source order, deduplicated.
        backend.cancel();
        QVERIFY(action(window, "copyAction")->isEnabled());
        closeNextDialog(window, destination.path(), false);
        action(window, "copyAction")->trigger();
        QCOMPARE(backend.extractCalls, 1);
        closeNextDialog(window, destination.path(), true);
        action(window, "copyAction")->trigger();
        QCOMPARE(backend.extractCalls, 2);
        QCOMPARE(portablePaths(backend.lastSelected), expected);
        QTimer::singleShot(0, &window, [] { dismissWarnings(); });
        backend.fail("fixture extraction failure");
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        selectRequest();
        QVERIFY(action(window, "copyAction")->isEnabled());
        closeNextDialog(window, destination.path(), false);
        action(window, "moveAction")->trigger();
        QCOMPARE(backend.extractCalls, 2);
        closeNextDialog(window, destination.path(), true);
        action(window, "moveAction")->trigger();
        QCOMPARE(backend.extractCalls, 3);
        QCOMPARE(portablePaths(backend.lastSelected), expected);
        backend.cancel();

        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        selectRequest();
        QTimer closeResult;
        connect(&closeResult, &QTimer::timeout, &window, [] {
            TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget());
            dismissWarnings();
        });
        closeResult.start(5);
        action(window, "hashSHA256Action")->trigger();
        closeResult.stop();
        QCOMPARE(backend.extractCalls, 4);
        QCOMPARE(backend.lastSelected, QStringList{"root.bin"}); // First source stream cancels.
        QVERIFY(action(window, "hashSHA256Action")->isEnabled());
        action(window, "flatViewAction")->setChecked(false);
    }

    void flatDuplicateBasenamesUseTheSelectedSourceIndex() {
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, "flatViewAction")->setChecked(false);
        openListedArchive(window, backend, "duplicate-paths.zip",
                          {{"a/same.txt", 1, 1}, {"b/same.txt", 2, 2}});
        action(window, "flatViewAction")->setChecked(true);
        const int row = rowWithPath(window, "b/same.txt");
        QVERIFY(row >= 0);
        selectOnly(window, row);
        corruptPresentationPath(window, row);
        window.extractTo("/temporary/fixture destination", true);
        QCOMPARE(portablePaths(backend.lastSelected), QStringList{"b/same.txt"});
        backend.cancel();
        action(window, "flatViewAction")->setChecked(false);
    }
};

QTEST_MAIN(ArchiveSelectionRuntimeTests)
#include "archive_selection_runtime_tests.moc"
