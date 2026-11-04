#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "localization/qtlanguageservices.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QSysInfo>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

class ItemLayoutStyle final : public QProxyStyle {
  public:
    struct Item {
        QRect bounds, icon, text;
        int textHeight;
        QString label;
        Qt::Alignment alignment;
    };
    mutable QList<Item> items;

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override {
        if (element == CE_ItemViewItem) {
            if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option)) {
                const auto *view = qobject_cast<const QAbstractItemView *>(widget);
                const QRect cell =
                    view && item->index.isValid() ? view->visualRect(item->index) : item->rect;
                items.append({cell, subElementRect(SE_ItemViewItemDecoration, item, widget),
                              subElementRect(SE_ItemViewItemText, item, widget),
                              item->fontMetrics.height(), item->text, item->displayAlignment});
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

class MainWindowLayoutTests : public QObject {
    Q_OBJECT

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void splitAndCombineCommandsFollowSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        for (const QString &name : {"file.bin", "file.bin.001", "file.bin.002"}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("part");
        }
        const QString previous = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temp.path()));
        MainWindow window;
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *split = window.findChild<QAction *>("splitFileAction");
        auto *combine = window.findChild<QAction *>("combineFilesAction");
        QVERIFY(table && split && combine);
        QVERIFY(!split->isEnabled());
        QVERIFY(!combine->isEnabled());
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->model()->index(row, 0).data().toString() == "file.bin.001") {
                table->selectRow(row);
                break;
            }
        }
        QVERIFY(split->isEnabled());
        QVERIFY(combine->isEnabled());
        table->clearSelection();
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->model()->index(row, 0).data().toString() == "file.bin") {
                table->selectRow(row);
                break;
            }
        }
        QVERIFY(split->isEnabled());
        QVERIFY(!combine->isEnabled());
        QVERIFY(QDir::setCurrent(previous));
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void extractDialogMatches7zfmControls() {
        ExtractDialog dialog("/initial", {"/previous", "/older"});
        QCOMPARE(dialog.windowTitle(), QString("Extract"));
        auto *destination = dialog.findChild<QComboBox *>("extractDestination");
        QVERIFY(destination);
        QVERIFY(destination->isEditable());
        QCOMPARE(destination->currentText(), QString("/"));
        QCOMPARE(dialog.findChild<QLineEdit *>("extractPathName")->text(), QString("initial"));
        QCOMPARE(destination->itemText(0), QString("/previous"));
        QVERIFY(dialog.findChild<QPushButton *>("extractBrowseButton"));
        auto *pathMode = dialog.findChild<QComboBox *>("extractPathMode");
        auto *overwriteMode = dialog.findChild<QComboBox *>("extractOverwriteMode");
        QVERIFY(pathMode && overwriteMode);
        QCOMPARE(pathMode->currentText(), QString("Full pathnames"));
        QVERIFY(pathMode->isEnabled());
        QVERIFY(overwriteMode->isEnabled());
        auto *password = dialog.findChild<QLineEdit *>("extractPassword");
        auto *showPassword = dialog.findChild<QCheckBox *>("extractShowPassword");
        QVERIFY(password && showPassword);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        password->setText("secret");
        QCOMPARE(dialog.password(), QString("secret"));
        showPassword->setChecked(true);
        QCOMPARE(password->echoMode(), QLineEdit::Normal);
        dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
        destination->setCurrentText("/changed");
        dialog.accept();
        QCOMPARE(QDir::cleanPath(dialog.destination()), QString("/changed"));
    }

    void layoutAtSizes_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("default") << QSize(980, 620);
        QTest::newRow("compact") << QSize(720, 480);
        QTest::newRow("small") << QSize(640, 400);
    }

    void layoutAtSizes() {
        QFETCH(QSize, windowSize);

        MainWindow window;
        auto *path = window.findChild<QLineEdit *>("archivePath");
        auto *pathCombo = window.findChild<QComboBox *>("pathCombo");
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *toolbar = window.findChild<QToolBar *>("panelFileActions0");
        QVERIFY(path);
        QVERIFY(pathCombo);
        QVERIFY(pathCombo->isEditable());
        QCOMPARE(pathCombo->lineEdit(), path);
        QVERIFY(table);
        QVERIFY(toolbar);
        QVERIFY(!toolbar->actions().first()->icon().isNull());
        auto *parent = window.findChild<QPushButton *>("panelParentButton0");
        QVERIFY(parent);

        path->setText(QString(180, QLatin1Char('a')) +
                      QStringLiteral("/имя с пробелом/archive.7z"));
        window.resize(windowSize);
        window.show();
        QCoreApplication::processEvents();
        window.findChild<QAction *>("focusPathAction")->trigger();
        QCoreApplication::processEvents();

        QCOMPARE(window.size(), windowSize);
        QVERIFY(path->isVisible());
        QVERIFY(table->isVisible());
        QVERIFY(toolbar->isVisible());
        QVERIFY(UiInspection::fullyVisible(*parent, window));
        QCOMPARE(window.statusBar()->isVisible(), !window.statusBar()->currentMessage().isEmpty());
        QVERIFY(
            UiInspection::fullyVisible(*window.findChild<QStatusBar *>("panelStatusBar"), window));

        const QRect content = window.centralWidget()->contentsRect();
        QVERIFY2(content.contains(UiInspection::bounds(*pathCombo, *window.centralWidget())),
                 "Archive path escaped the content area");
        const QRect tableInContent(table->mapTo(window.centralWidget(), QPoint(0, 0)),
                                   table->size());
        QVERIFY2(content.contains(tableInContent), "File table escaped the content area");
        QVERIFY2(UiInspection::bounds(*pathCombo, *window.centralWidget()).bottom() <
                     tableInContent.top(),
                 "Archive path overlaps the file table");
        QVERIFY2(table->height() >= 120, "File table is too short to inspect archives");
        QVERIFY2(table->horizontalHeader()->sectionSize(0) > 0, "Name column is hidden");
        QVERIFY(table->isColumnHidden(1));
        QVERIFY(table->verticalHeader()->isHidden());

        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(UiInspection::writeReport(window, QSysInfo::productType() + "-" + tag));
    }

    void sidebarToggleKeepsLocationAndExpandsFileList() {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *toggle = window.findChild<QAction *>("sidebarAction");
        QVERIFY(table && toggle);
        const QString path = window.findChild<QLineEdit *>("archivePath")->text();
        const int original = table->width();
        toggle->trigger();
        QTRY_VERIFY(table->width() > original);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), path);
        const int expanded = table->width();
        toggle->trigger();
        QTRY_VERIFY(table->width() < expanded);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), path);
    }

    void columnCustomizationPersists() {
        MainWindow window;
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *header = table->horizontalHeader();
        QVERIFY(!table->isColumnHidden(2));
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (QAction *option : menu->actions()) {
                if (option->text() == "Size") {
                    option->trigger();
                    break;
                }
            }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(header, "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(30, 5))));
        QVERIFY(table->isColumnHidden(2));
        MainWindow reopened;
        QVERIFY(reopened.findChild<FileTable *>("archiveTable")->isColumnHidden(2));
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void viewModesShareSelectionAndPersist() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString original = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temporary.path()));
        QFile file(temporary.filePath(QStringLiteral("имя с пробелом.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        MainWindow window;
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *list = window.findChild<QListView *>("iconList");
        auto *stack = window.findChild<QStackedWidget *>("fileViewStack");
        QVERIFY(table && list && stack);
        QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(table));
        QCOMPARE(list->model(), table->model());
        QCOMPARE(list->selectionModel(), table->selectionModel());

        for (int mode = 0; mode < 3; ++mode) {
            auto *action =
                window.findChild<QAction *>(QStringLiteral("viewMode%1Action").arg(mode));
            QVERIFY(action);
            action->trigger();
            QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(list));
            QVERIFY(action->isChecked());
        }
        QVERIFY(table->rowCount() > 0);
        list->selectionModel()->select(list->model()->index(0, 0),
                                       QItemSelectionModel::ClearAndSelect |
                                           QItemSelectionModel::Rows);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        list->setFocus();
        const QKeyCombination selectAll = QKeySequence(QKeySequence::SelectAll)[0];
        QTest::keyClick(list, selectAll.key(), selectAll.keyboardModifiers());
        QCOMPARE(table->selectionModel()->selectedRows().size(), table->rowCount());
        window.findChild<QAction *>("viewMode3Action")->trigger();
        QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(table));
        QCOMPARE(table->selectionModel()->selectedRows().size(), table->rowCount());

        window.findChild<QAction *>("viewMode1Action")->trigger();
        MainWindow reopened;
        QCOMPARE(reopened.findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QVERIFY(reopened.findChild<QAction *>("viewMode1Action")->isChecked());
        QVERIFY(QDir::setCurrent(original));
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void iconModesKeepLabelsInsideCells_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::addColumn<bool>("largeFont");
        QTest::addColumn<int>("mode");
        for (int mode : {0, 1}) {
            const QString prefix = mode == 0 ? "large-" : "small-";
            QTest::newRow(qPrintable(prefix + "normal")) << QSize(980, 620) << false << mode;
            QTest::newRow(qPrintable(prefix + "compact")) << QSize(640, 400) << false << mode;
            QTest::newRow(qPrintable(prefix + "compact-large-font"))
                << QSize(640, 400) << true << mode;
        }
    }

    void iconModesKeepLabelsInsideCells() {
        QFETCH(QSize, windowSize);
        QFETCH(bool, largeFont);
        QFETCH(int, mode);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString original = QDir::currentPath();
        const auto restoreDirectory = qScopeGuard([&] { QDir::setCurrent(original); });
        QVERIFY(QDir::setCurrent(temporary.path()));
        for (int i = 0; i < 12; ++i) {
            QFile file(temporary.filePath(QStringLiteral("Длинное имя файла %1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QDir(temporary.path()).mkdir(QStringLiteral("Folder")));
        QFile shortName(temporary.filePath(QStringLiteral("Short.txt")));
        QVERIFY(shortName.open(QIODevice::WriteOnly));
        shortName.close();

        ItemLayoutStyle style;
        QtFileManagerStartup startup;
        startup.FullPath = UString(temporary.path().toStdWString().c_str());
        MainWindow window(nullptr, {}, nullptr, &startup);
        auto *list = window.findChild<QListView *>("iconList");
        QVERIFY(list);
        list->setStyle(&style);
        window.resize(windowSize);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(list->model()->rowCount() >= 12);
        QTRY_VERIFY([&] {
            for (int row = 0; row < list->model()->rowCount(); ++row)
                if (list->model()->index(row, 0).data().toString().startsWith(
                        QStringLiteral("Длинное")))
                    return true;
            return false;
        }());
        // Exercise both transitions: vertical labels and column-flow lists
        // must not leave their layout behind when returning to Small Icons.
        for (int precedingMode : {mode == 0 ? 1 : 0, 2}) {
            auto *preceding =
                window.findChild<QAction *>(QStringLiteral("viewMode%1Action").arg(precedingMode));
            auto *icons = window.findChild<QAction *>(QStringLiteral("viewMode%1Action").arg(mode));
            QTRY_VERIFY_WITH_TIMEOUT(preceding->isEnabled(), 15000);
            preceding->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(icons->isEnabled(), 15000);
            icons->trigger();
            if (largeFont) {
                QFont font = list->font();
                font.setPixelSize(28);
                list->setFont(font);
            }
            list->doItemsLayout();
            QCoreApplication::processEvents();
            style.items.clear();
            const QPixmap rendered = list->viewport()->grab();
            QVERIFY(!rendered.isNull());
            QVERIFY(!style.items.isEmpty());
            for (const auto &item : style.items) {
                QString geometry;
                QDebug(&geometry) << "cell" << item.bounds << "icon" << item.icon << "text"
                                  << item.text << "label" << item.label;
                QVERIFY2(item.bounds.contains(item.icon), "Icon is clipped by its cell");
                QVERIFY2(item.bounds.contains(item.text), qPrintable(geometry));
                QVERIFY2(item.text.height() >= item.textHeight,
                         qPrintable(QString("Filename height %1, font height %2, cell height %3")
                                        .arg(item.text.height())
                                        .arg(item.textHeight)
                                        .arg(item.bounds.height())));
                if (mode == 0) {
                    QVERIFY2(item.alignment.testFlag(Qt::AlignHCenter),
                             "Large Icons filenames must be centered below their icons");
                    QVERIFY2(item.icon.bottom() < item.text.top(),
                             "Filename must be below the icon");
                    if (item.label.startsWith(QStringLiteral("Длинное")))
                        QVERIFY2(item.text.height() >= 2 * item.textHeight,
                                 "Large Icons must wrap long filenames");
                } else
                    QVERIFY2(item.icon.right() < item.text.left(), "Filename must follow the icon");
            }
            const QRect first = list->visualRect(list->model()->index(0, 0));
            const QRect second = list->visualRect(list->model()->index(1, 0));
            if (first.right() + 1 + second.width() + list->spacing() <= list->viewport()->width()) {
                QCOMPARE(first.top(), second.top());
                QVERIFY(first.right() < second.left());
            } else {
                QCOMPARE(first.left(), second.left());
                QVERIFY(first.bottom() < second.top());
            }
            QVERIFY2(!first.intersects(second),
                     qPrintable(QString("Overlapping cells: %1,%2 %3x%4 and %5,%6 %7x%8")
                                    .arg(first.x())
                                    .arg(first.y())
                                    .arg(first.width())
                                    .arg(first.height())
                                    .arg(second.x())
                                    .arg(second.y())
                                    .arg(second.width())
                                    .arg(second.height())));
        }
        QVERIFY(UiInspection::writeReport(
            window, "icon-layout-" + QString::fromLatin1(QTest::currentDataTag())));
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(MainWindowLayoutTests)
#include "ui_layout_tests.moc"
