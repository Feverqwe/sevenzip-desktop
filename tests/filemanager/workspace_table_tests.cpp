#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QHeaderView>
#include <QMenu>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QWindow>

class WorkspaceTableTests final : public QObject {
    Q_OBJECT
    static int rowFor(const FileTable &table, const QString &name) {
        for (int row = 0; row < table.rowCount(); ++row)
            if (table.model()->index(row, 0).data().toString() == name)
                return row;
        return -1;
    }
    static void fixture(const QString &folder) {
        QVERIFY(QDir().mkpath(folder + "/Documents"));
        for (const QString &name : {QStringLiteral("Annual report.txt"), QStringLiteral("Notes.md"),
                                    QStringLiteral("Проект.7z")}) {
            QFile file(folder + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("local presentation fixture\n"), 27);
        }
    }
    static FileTable *left(MainWindow &window) {
        return window.findChild<QSplitter *>("filePanelSplitter")
            ->widget(0)
            ->findChild<FileTable *>("archiveTable");
    }
    static void setColumnVisible(MainWindow &window, FileTable &table, int column, bool visible) {
        QTimer::singleShot(0, &window, [&table, column, visible] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto close = qScopeGuard([menu] { menu->close(); });
            const auto label = table.model()->headerData(column, Qt::Horizontal).toString();
            for (auto *action : menu->actions())
                if (action->isCheckable() && action->text() == label &&
                    action->isChecked() != visible)
                    action->trigger();
        });
        QVERIFY(QMetaObject::invokeMethod(table.horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
    }
    static QStringList visibleHeaders(const FileTable &table) {
        QStringList labels;
        const auto *header = table.horizontalHeader();
        for (int visual = 0; visual < header->count(); ++visual) {
            const int column = header->logicalIndex(visual);
            if (!table.isColumnHidden(column))
                labels << table.model()->headerData(column, Qt::Horizontal).toString();
        }
        return labels;
    }
    static QColor rowBackground(FileTable &table, int row) {
        QApplication::processEvents();
        const QImage image = table.viewport()->grab().toImage();
        const int x = table.viewport()->width() - 6;
        const int y = table.rowViewportPosition(row) + table.rowHeight(row) / 2;
        return image.pixelColor(qRound(x * image.devicePixelRatio()),
                                qRound(y * image.devicePixelRatio()));
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void typeSortAndNativeCustomLayoutSurviveReopening() {
        QTemporaryDir data;
        fixture(data.path());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QStringList headers;
        {
            ControlledBackend backend;
            MainWindow window(&backend);
            window.show();
            window.openPath(data.path());
            auto *table = left(window);
            QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
            const int type = table->columnForProperty(kpidExtension);
            QVERIFY(!table->isColumnHidden(type));
            const int directory = rowFor(*table, "Documents");
            const int report = rowFor(*table, "Annual report.txt");
            const QString folderType = table->model()->index(directory, type).data().toString();
            const QString fileType = table->model()->index(report, type).data().toString();
            QVERIFY(!folderType.isEmpty() && !fileType.isEmpty() && folderType != fileType);
            const int created = table->columnForProperty(kpidCTime);
            setColumnVisible(window, *table, created, true);
            QVERIFY(!table->isColumnHidden(created));
            table->horizontalHeader()->resizeSection(table->columnForProperty(kpidName), 237);
            table->horizontalHeader()->resizeSection(type, 151);
            table->sortItems(type, Qt::AscendingOrder);
            QVERIFY(rowFor(*table, "Проект.7z") < rowFor(*table, "Notes.md"));
            QVERIFY(rowFor(*table, "Notes.md") < rowFor(*table, "Annual report.txt"));
            CIntArr order;
            QVERIFY(table->sourcePanel->GetColumnOrder(order));
            setColumnVisible(window, *table, table->columnForProperty(kpidSize), false);
            headers = visibleHeaders(*table);
        }
        {
            ControlledBackend backend;
            MainWindow reopened(&backend);
            reopened.show();
            reopened.openPath(data.path());
            auto *table = left(reopened);
            QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
            const int type = table->columnForProperty(kpidExtension);
            QVERIFY(!table->isColumnHidden(type));
            QVERIFY(!table->isColumnHidden(table->columnForProperty(kpidCTime)));
            QCOMPARE(table->columnWidth(table->columnForProperty(kpidName)), 237);
            QCOMPARE(table->columnWidth(type), 151);
            QCOMPARE(table->horizontalHeader()->sortIndicatorSection(), type);
            QCOMPARE(visibleHeaders(*table), headers);
            reopened.resize(640, 400);
            QApplication::processEvents();
            QCOMPARE(table->columnWidth(table->columnForProperty(kpidName)), 237);
        }
        // A native layout from before the presentation extension remains authoritative.
        QSettings(settings, QSettings::IniFormat).remove("workspace/columns");
        ControlledBackend backend;
        MainWindow legacy(&backend);
        legacy.show();
        legacy.openPath(data.path());
        auto *table = left(legacy);
        QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
        QVERIFY(!table->isColumnHidden(table->columnForProperty(kpidCTime)));
        QCOMPARE(table->columnWidth(table->columnForProperty(kpidName)), 237);
        QCOMPARE(table->horizontalHeader()->sortIndicatorSection(),
                 table->columnForProperty(kpidExtension));
    }

    void explicitFullRowChoiceChangesPaintWithoutChangingSelection_data() {
        QTest::addColumn<bool>("fullRow");
        QTest::newRow("full-row") << true;
        QTest::newRow("name-only") << false;
    }
    void explicitFullRowChoiceChangesPaintWithoutChangingSelection() {
        QFETCH(bool, fullRow);
        CFmSettings preferences;
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        preferences.Load(settings);
        preferences.FullRow = fullRow;
        preferences.Save(settings);
        QTemporaryDir data;
        fixture(data.path());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(data.path());
        auto *table = left(window);
        QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        const int row = rowFor(*table, "Annual report.txt");
        table->selectRow(row);
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::mouseMove(table->viewport(), QPoint(1, table->viewport()->height() - 1));
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(rowBackground(*table, row).rgba(),
                 table->palette().color(fullRow ? QPalette::Highlight : QPalette::Base).rgba());
    }

    void selectionGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void currentOutlineSurvivesHorizontalScroll() {
        QTemporaryDir data;
        fixture(data.path());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(data.path());
        auto *table = left(window);
        QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        table->setCurrentCell(rowFor(*table, "Annual report.txt"), 0);
        table->clearSelection();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        const auto outline = [&] {
            QApplication::processEvents();
            const auto image = table->viewport()->grab().toImage();
            return image.pixelColor(qRound(image.devicePixelRatio()),
                                    qRound((table->rowViewportPosition(table->currentRow()) + 1) *
                                           image.devicePixelRatio()));
        };
        const QColor expected = outline();
        QVERIFY(expected != table->palette().color(QPalette::Base));
        table->horizontalHeader()->resizeSection(table->columnForProperty(kpidExtension),
                                                 table->viewport()->width() * 2);
        QTRY_VERIFY(table->horizontalScrollBar()->maximum() > 0);
        table->horizontalScrollBar()->setValue(table->horizontalScrollBar()->maximum());
        QVERIFY(table->visualRect(table->currentIndex()).right() < 0);
        QCOMPARE(outline(), expected);
    }
    void selectionGeometry() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const UString previous = g_LangID;
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(previous, settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
        });
        QTemporaryDir data;
        fixture(data.path());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(data.path());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            auto *toggle = window.findChild<QAction *>("twoPanelsAction");
            if (toggle->isChecked() != (size.width() == 640))
                toggle->trigger();
            auto *table = left(window);
            QTRY_VERIFY(rowFor(*table, "Annual report.txt") >= 0 && table->isEnabled());
            const bool active = WindowActivation::waitForActive(window);
            QVERIFY2(active, qPrintable(QString("visible=%1 exposed=%2 active=%3 app=%4 pos=%5,%6")
                                            .arg(window.isVisible())
                                            .arg(window.windowHandle()->isExposed())
                                            .arg(window.isActiveWindow())
                                            .arg(int(QGuiApplication::applicationState()))
                                            .arg(window.x())
                                            .arg(window.y())));
            const int first = rowFor(*table, "Annual report.txt");
            const int second = rowFor(*table, "Notes.md");
            table->selectRow(first);
            table->selectionModel()->select(table->model()->index(second, 0),
                                            QItemSelectionModel::Select |
                                                QItemSelectionModel::Rows);
            table->setCurrentCell(first, 0, QItemSelectionModel::NoUpdate);
            table->setFocus();
            QTRY_VERIFY(table->hasFocus());
            QCOMPARE(table->selectionModel()->selectedRows().size(), 2);
            QCOMPARE(rowBackground(*table, second).rgba(),
                     table->palette().color(QPalette::Highlight).rgba());
            const QString tag = QString("workspace-table-%1-%2-").arg(language).arg(size.width());
            QVERIFY(UiInspection::writeReport(window, tag + "selected-current"));
            if (toggle->isChecked())
                window.findChild<QSplitter *>("filePanelSplitter")
                    ->widget(1)
                    ->findChild<FileTable *>("archiveTable")
                    ->setFocus();
            else
                window.findChild<QToolBar *>("panelFileActions0")->setFocus();
            QApplication::processEvents();
            QVERIFY(!table->hasFocus());
            QCOMPARE(table->selectionModel()->selectedRows().size(), 2);
            QVERIFY(UiInspection::writeReport(window, tag + "inactive-selection"));
            table->clearSelection();
            table->setFocus();
            QTRY_VERIFY(table->hasFocus());
            QCOMPARE(table->currentRow(), first);
            QVERIFY(UiInspection::writeReport(window, tag + "current-only"));
        }
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspaceTableTests)
#include "workspace_table_tests.moc"
