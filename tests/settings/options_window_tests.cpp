#include "archive/support/native_archive_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QIcon>

class OptionsWindowTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void applyRefreshesBothPanelsAndRetainsSelection_data() {
        QTest::addColumn<unsigned>("focused");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void applyRefreshesBothPanelsAndRetainsSelection() {
        QFETCH(unsigned, focused);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const QString left = fixture.filePath("левая папка");
        const QString right = fixture.filePath("правая папка");
        QVERIFY(QDir().mkpath(left) && QDir().mkpath(right));
        auto write = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write("data") == 4;
        };
        QVERIFY(write(left + "/left.txt") && write(right + "/right.txt"));
        MainWindow window;
        window.show();
        auto navigate = [&](QWidget *panel, const QString &path) {
            auto *address = panel->findChild<QLineEdit *>("archivePath");
            address->setText(path);
            QTest::keyClick(address, Qt::Key_Return);
        };
        auto *leftPanel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(leftPanel);
        navigate(leftPanel, left);
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(leftTable->rowCount(), 1);
        leftTable->selectRow(0);
        leftTable->setCurrentCell(0, 0);
        window.findChild<QAction *>("twoPanelsAction")->trigger();
        auto *rightPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(rightPanel);
        navigate(rightPanel, right);
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(rightTable->rowCount(), 1);
        rightTable->selectRow(0);
        rightTable->setCurrentCell(0, 0);
        (focused == 0 ? leftTable : rightTable)->setFocus();
        QApplication::processEvents();
        bool applied = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(dialog);
            QVERIFY(write(left + "/new-left.txt") && write(right + "/new-right.txt"));
            dialog->findChild<QCheckBox *>("showDotsOption")->setChecked(true);
            dialog->findChild<QCheckBox *>("gridOption")->setChecked(true);
            dialog->findChild<QDialogButtonBox *>("settingsButtons")
                ->button(QDialogButtonBox::Apply)
                ->click();
            applied = true;
            dialog->findChild<QCheckBox *>("gridOption")->setChecked(false);
            dialog->reject();
        });
        window.findChild<QAction *>("settingsAction")->trigger();
        QVERIFY(applied);
        for (auto *table : {leftTable, rightTable}) {
            QTRY_COMPARE(table->rowCount(), 3);
            QCOMPARE(table->model()->index(0, 0).data().toString(), QString(".."));
            QVERIFY(table->showGrid());
            QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
            for (int row = 1; row < table->rowCount(); ++row) {
                const auto index = table->model()->index(row, 0);
                QIcon expected;
                QCOMPARE(table->sourcePanel->GetItemIcon(index.data(Qt::UserRole + 6).toUInt(),
                                                         expected),
                         S_OK);
                QCOMPARE(
                    qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).pixmap(16, 16).toImage(),
                    expected.pixmap(16, 16).toImage());
            }
        }
        const auto parentIcon = [](FileTable *table) {
            return qvariant_cast<QIcon>(table->model()->index(0, 0).data(Qt::DecorationRole))
                .pixmap(16, 16)
                .toImage();
        };
        QCOMPARE(parentIcon(leftTable), parentIcon(rightTable));
        QCOMPARE(leftTable->model()->index(leftTable->currentRow(), 0).data().toString(),
                 QString("left.txt"));
        QCOMPARE(rightTable->model()->index(rightTable->currentRow(), 0).data().toString(),
                 QString("right.txt"));
    }
    void applyRetainsParentInBoundFolder_data() {
        QTest::addColumn<bool>("archive");
        QTest::newRow("volumes") << false;
        QTest::newRow("archive") << true;
    }
    void applyRetainsParentInBoundFolder() {
        QFETCH(bool, archive);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        MainWindow window;
        window.show();
        if (archive) {
            const QString path = fixture.filePath(QStringLiteral("архив 空.zip"));
            QVERIFY(NativeArchiveFixture::Write(path, {{QStringLiteral("файл.txt"), "payload"}},
                                                "zip"));
            window.openArchive(path);
        } else
            action(window, "openRootAction")->trigger();
        auto *view = static_cast<FileTable *>(table(window));
        QTRY_VERIFY(view->isEnabled());
        CMyComPtr<IFolderFolder> folder = view->sourcePanel->GetFolder();
        const QString path = window.findChild<QLineEdit *>("archivePath")->text();
        bool applied = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(dialog);
            dialog->findChild<QCheckBox *>("showDotsOption")->setChecked(true);
            dialog->findChild<QDialogButtonBox *>("settingsButtons")
                ->button(QDialogButtonBox::Apply)
                ->click();
            applied = true;
            dialog->reject();
        });
        action(window, "settingsAction")->trigger();
        QVERIFY(applied);
        QTRY_VERIFY(view->isEnabled());
        QCOMPARE(view->sourcePanel->GetFolder(), (IFolderFolder *)folder);
        QCOMPARE(view->sourcePanel->IsArcFolder(), archive);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), path);
        QCOMPARE(view->model()->index(0, 0).data().toString(), QString(".."));
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 6).toUInt(), UInt32(-1));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(
                view->viewport()->rect().contains(view->visualRect(view->model()->index(0, 0))));
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(
                window, QString("options-bound-parent-%1-%2").arg(archive).arg(size.width())));
        }
    }
};
QTEST_MAIN(OptionsWindowTests)
#include "options_window_tests.moc"
