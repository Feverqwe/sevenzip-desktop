#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class SettingsTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void cleanup() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/SingleClick", false);
    }
    void singleClickSelection_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("singleClick");
        for (int mode : {0, 3})
            for (bool singleClick : {false, true}) {
                QTest::newRow(qPrintable(QStringLiteral("disk-%1-%2").arg(mode).arg(singleClick)))
                    << mode << false << singleClick;
                QTest::newRow(
                    qPrintable(QStringLiteral("archive-%1-%2").arg(mode).arg(singleClick)))
                    << mode << true << singleClick;
            }
    }
    void singleClickSelection() {
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QFETCH(bool, singleClick);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/SingleClick", singleClick);
        settings.sync();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QStringList names = {"A папка", "B папка", "C папка"};
        for (const QString &name : names)
            QVERIFY(QDir(dir.path()).mkdir(name));
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(dir.path());
        QTest::keyClick(address, Qt::Key_Return);
        if (archive) {
            openListedArchive(window, backend, dir.filePath("click.zip"),
                              {ArchiveEntry{names[0] + "/child.txt", 1, 1},
                               ArchiveEntry{names[1] + "/child.txt", 1, 1},
                               ArchiveEntry{names[2] + "/child.txt", 1, 1}});
        }
        window.findChild<QAction *>(qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))
            ->trigger();
        window.show();
        window.activateWindow();
        QApplication::processEvents();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *icons = window.findChild<QListView *>("iconList");
        QAbstractItemView *view = mode == 3 ? static_cast<QAbstractItemView *>(table)
                                            : static_cast<QAbstractItemView *>(icons);
        const QString before = address->text();
        const auto click = [&](int row, Qt::KeyboardModifiers modifiers) {
            const QModelIndex index = table->model()->index(row, 0);
            view->scrollTo(index);
            const QRect cell = view->visualRect(index);
            const QPoint point =
                mode == 3 ? QPoint(cell.left() + 20, cell.center().y()) : cell.center();
            QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, point);
        };
        click(0, Qt::ControlModifier);
        QCOMPARE(address->text(), before);
        click(1, Qt::ControlModifier);
        QCOMPARE(address->text(), before);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 2);
        QVERIFY(window.findChild<QAction *>("copyAction")->isEnabled());
        click(2, Qt::ShiftModifier);
        QCOMPARE(address->text(), before);
        QVERIFY(table->selectionModel()->selectedRows().size() >= 2);
        click(0, Qt::MetaModifier);
        QCOMPARE(address->text(), before);
        if (singleClick && (mode == 0 || mode == 3) &&
            qEnvironmentVariableIntValue("SEVENZIP_CAPTURE_SCREENSHOTS") == 1) {
            const QString screenshots = QDir::currentPath() + "/ui-screenshots";
            QVERIFY(QDir().mkpath(screenshots));
            for (int width : {980, 640}) {
                window.resize(width, width == 980 ? 620 : 400);
                QApplication::processEvents();
                QVERIFY(
                    window.grab().save(screenshots + QStringLiteral("/single-click-%1-%2-%3.png")
                                                         .arg(archive ? "archive" : "disk")
                                                         .arg(mode)
                                                         .arg(width)));
            }
        }
        click(0, Qt::NoModifier);
        if (!singleClick) {
            QCOMPARE(address->text(), before);
            const QModelIndex index = table->model()->index(0, 0);
            QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                               view->visualRect(index).center());
        }
        QVERIFY(address->text() != before);
        QCOMPARE(backend.extractCalls, 0);
        settings.setValue("FM/SingleClick", false);
        settings.sync();
    }

    void optionsApplyCancelAndPersist() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString original = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temporary.path()));
        QVERIFY(QDir().mkdir(QStringLiteral("папка с пробелом")));
        MainWindow window;
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *action = window.findChild<QAction *>("settingsAction");
        QVERIFY(table && action);
        QVERIFY(!table->showGrid());

        QTimer::singleShot(0, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(dialog);
            dialog->findChild<QCheckBox *>("gridOption")->setChecked(true);
            dialog->findChild<QCheckBox *>("singleClickOption")->setChecked(true);
            dialog->reject();
        });
        action->trigger();
        QVERIFY(!table->showGrid());

        QTimer::singleShot(0, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(dialog);
            dialog->findChild<QCheckBox *>("gridOption")->setChecked(true);
            dialog->findChild<QCheckBox *>("singleClickOption")->setChecked(true);
            dialog->accept();
        });
        action->trigger();
        QVERIFY(table->showGrid());

        int folderRow = -1;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() ==
                QStringLiteral("папка с пробелом"))
                folderRow = row;
        QVERIFY(folderRow >= 0);
        QVERIFY(QMetaObject::invokeMethod(table, "clicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, table->model()->index(folderRow, 0))));
        QCOMPARE(
            window.findChild<QLineEdit *>("archivePath")->text(),
            QFileInfo(temporary.filePath(QStringLiteral("папка с пробелом"))).canonicalFilePath());

        MainWindow reopened;
        QVERIFY(reopened.findChild<FileTable *>("archiveTable")->showGrid());
        QVERIFY(QDir::setCurrent(original));
    }
};

QTEST_MAIN(SettingsTests)
#include "settings_tests.moc"
