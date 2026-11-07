#include "support/delete_reply.h"
#include "window_operations.h"
#include <QLineEdit>

namespace {
class FakeTrashService final : public IQtTrashService {
  public:
    bool MoveToTrash(const QString &path) override {
        moved << path;
        if (failAfter >= 0 && moved.size() > failAfter)
            return false;
        return QFile::remove(path);
    }
    QStringList moved;
    int failAfter = -1;
};
} // namespace

void MainWindowOperationsTests::inlineRenameCollisionRestoresSelection() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    for (const QString &name : {QStringLiteral("исходный.txt"), QStringLiteral("занято.txt")}) {
        QFile file(temp.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(name.toUtf8()), qint64(name.toUtf8().size()));
    }
    ControlledBackend backend;
    MainWindow window(&backend);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    table(window)->selectRow(filesystemRow(window, QStringLiteral("исходный.txt")));
    action(window, "renameAction")->trigger();
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QVERIFY(editor);
    editor->setText(QStringLiteral("занято.txt"));
    QTimer warningTimer;
    bool answeredWarning = false;
    connect(&warningTimer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (TestProgressDialog::DismissVisibleResult(widget)) {
                answeredWarning = true;
            } else if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                answeredWarning = true;
                box->accept();
            }
    });
    warningTimer.start(1);
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_VERIFY(answeredWarning);
    warningTimer.stop();
    QFile occupied(temp.filePath(QStringLiteral("занято.txt")));
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QStringLiteral("занято.txt").toUtf8());
    QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("исходный.txt"))));
    QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table(window)
                 ->model()
                 ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                 .data()
                 .toString(),
             QStringLiteral("исходный.txt"));
}

void MainWindowOperationsTests::deleteRoutesTrashAndShiftPermanent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    for (const QString &name : {QStringLiteral("в корзину.txt"), QStringLiteral("навсегда.txt"),
                                QStringLiteral("первый.txt"), QStringLiteral("второй.txt")}) {
        QFile file(temp.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep"), qint64(4));
    }
    ControlledBackend backend;
    FakeTrashService trash;
    MainWindow window(&backend, {}, &trash);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));

    QString confirmation;
    bool approve = true;
    QTimer dialogTimer;
    connect(&dialogTimer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *confirmationDialog = dynamic_cast<QtDeleteConfirmation *>(widget);
                confirmationDialog && confirmationDialog->isVisible()) {
                confirmation = confirmationDialog->findChild<QWidget *>("deleteSourceSummary")
                                   ->accessibleName();
                ReplyDelete(*confirmationDialog, approve);
                continue;
            }
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box || !box->isVisible())
                continue;
            box->accept();
        }
    });

    table(window)->selectRow(filesystemRow(window, QStringLiteral("в корзину.txt")));
    dialogTimer.start(1);
    action(window, "deleteAction")->trigger();
    dialogTimer.stop();
    QVERIFY(confirmation.contains(QStringLiteral("в корзину.txt")));
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("в корзину.txt"))));
    QCOMPARE(trash.moved.size(), 1);

    QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("папка для отказа")));
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    table(window)->selectRow(filesystemRow(window, QStringLiteral("папка для отказа")));
    approve = false;
    confirmation.clear();
    dialogTimer.start(1);
    action(window, "deleteAction")->trigger();
    dialogTimer.stop();
    QVERIFY(confirmation.contains(QStringLiteral("папка для отказа")));
    QVERIFY(QDir(temp.filePath(QStringLiteral("папка для отказа"))).exists());
    approve = true;

    table(window)->selectRow(filesystemRow(window, QStringLiteral("навсегда.txt")));
    table(window)->setFocus();
    confirmation.clear();
    dialogTimer.start(1);
    PanelKeys::deletePermanently(table(window), action(window, "deleteAction"));
    dialogTimer.stop();
    QVERIFY(confirmation.contains(QStringLiteral("навсегда.txt")));
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("навсегда.txt"))));
    QCOMPARE(trash.moved.size(), 1);
    QTest::keyRelease(table(window), Qt::Key_Shift);

    trash.failAfter = 2;
    table(window)->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const int firstRow = filesystemRow(window, QStringLiteral("первый.txt"));
    const int secondRow = filesystemRow(window, QStringLiteral("второй.txt"));
    QVERIFY(firstRow >= 0);
    QVERIFY(secondRow >= 0);
    QVERIFY(firstRow != secondRow);
    QItemSelection selection;
    selection.select(table(window)->model()->index(firstRow, 0),
                     table(window)->model()->index(firstRow, table(window)->columnCount() - 1));
    selection.select(table(window)->model()->index(secondRow, 0),
                     table(window)->model()->index(secondRow, table(window)->columnCount() - 1));
    table(window)->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 2);
    confirmation.clear();
    dialogTimer.start(1);
    action(window, "deleteAction")->trigger();
    dialogTimer.stop();
    QVERIFY(!confirmation.isEmpty());
    QVERIFY(confirmation.contains(QLatin1Char('2')));
    QVERIFY2(QFileInfo::exists(temp.filePath(QStringLiteral("первый.txt"))) !=
                 QFileInfo::exists(temp.filePath(QStringLiteral("второй.txt"))),
             qPrintable(QStringLiteral("first=%1 second=%2 moved=%3")
                            .arg(QFileInfo::exists(temp.filePath(QStringLiteral("первый.txt"))))
                            .arg(QFileInfo::exists(temp.filePath(QStringLiteral("второй.txt"))))
                            .arg(trash.moved.size())));
}

void MainWindowOperationsTests::sourceFilesystemCreateAndInlineRename() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ControlledBackend backend;
    MainWindow window(&backend);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));

    const auto answer = [&window](const char *actionName, const QString &name, bool accept) {
        QTimer::singleShot(0, &window, [&window, name, accept] {
            auto *dialog = window.findChild<CComboDialog *>("comboDialog");
            QVERIFY(dialog);
            if (accept) {
                dialog->findChild<QComboBox *>("comboValue")->setCurrentText(name);
                dialog->accept();
            } else
                dialog->reject();
        });
        action(window, actionName)->trigger();
    };
    answer("createFolderAction", QStringLiteral("папка с пробелом/child"), true);
    QVERIFY(QDir(temp.filePath(QStringLiteral("папка с пробелом/child"))).exists());
    answer("createFileAction", QStringLiteral("папка с пробелом/child/файл.txt"), true);
    QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("папка с пробелом/child/файл.txt"))));
    QTimer warningTimer;
    connect(&warningTimer, &QTimer::timeout, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            else if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->accept();
    });
    warningTimer.start(1);
    answer("createFileAction", QStringLiteral("папка с пробелом/child/файл.txt"), true);
    warningTimer.stop();
    QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("папка с пробелом/child/файл.txt"))));
    answer("createFolderAction", QStringLiteral("отмена"), false);
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("отмена"))));
}

void MainWindowOperationsTests::inlineRenameCancel_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("width");
    for (int width : {700, 400})
        for (int mode : {0, 3})
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-width-%2").arg(mode).arg(width)))
                << mode << width;
}

void MainWindowOperationsTests::inlineRenameCancel() {
    QFETCH(int, mode);
    QFETCH(int, width);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("папка с пробелом"));
    QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("папка с пробелом")));
    ControlledBackend backend;
    MainWindow window(&backend);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    window.resize(width, 380);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
    table(window)->selectRow(filesystemRow(window, QStringLiteral("папка с пробелом")));
    action(window, "renameAction")->trigger();
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QVERIFY(editor);
    editor->setText(QStringLiteral("переименовано"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(QDir(path).exists());
    QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("переименовано"))));
}

void MainWindowOperationsTests::stage7FilesystemCommands() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir("destination"));
    const QString source = temp.filePath(QStringLiteral("имя с пробелом.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("original"), 8);
    file.close();
    ControlledBackend backend;
    FakeTrashService trash;
    MainWindow window(&backend, {}, &trash);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    const int row = filesystemRow(window, QStringLiteral("имя с пробелом.txt"));
    QVERIFY(row >= 0);
    table(window)->selectRow(row);
    QVERIFY(action(window, "copyAction")->isEnabled());
    QVERIFY(action(window, "moveAction")->isEnabled());
    QVERIFY(action(window, "deleteAction")->isEnabled());
    QTRY_VERIFY(action(window, "renameAction")->isEnabled());
    const QScopedPointer<QMimeData> outbound(
        table(window)->model()->mimeData(table(window)->selectionModel()->selectedIndexes()));
    QVERIFY(outbound->hasUrls());
    QCOMPARE(QFileInfo(outbound->urls().first().toLocalFile()).canonicalFilePath(),
             QFileInfo(source).canonicalFilePath());

    SetPanelData(table(window)->model()->index(row, 1), temp.filePath("forged-selection.txt"),
                 Qt::EditRole);

    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *dialog = qobject_cast<CopyDialog *>(widget))
                dialog->reject();
        }
    });
    action(window, "moveAction")->trigger();
    QVERIFY(QFileInfo::exists(source));

    const QString destination = temp.filePath("destination");
    QString copyInfo;
    QTimer::singleShot(0, &window, [destination, &copyInfo] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *dialog = qobject_cast<CopyDialog *>(widget)) {
                copyInfo = ToQString(static_cast<CopyDialog *>(dialog)->Info);
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            }
        }
    });
    action(window, "copyAction")->trigger();
    QVERIFY(copyInfo.startsWith("Files: 1    ( 8 bytes )\n\n"));
    QVERIFY(QDir::fromNativeSeparators(copyInfo).contains(temp.path() + '/'));
    QVERIFY(copyInfo.endsWith(QStringLiteral("  имя с пробелом.txt")));
    const QString copied = QDir(destination).filePath(QStringLiteral("имя с пробелом.txt"));
    QVERIFY(!action(window, "copyAction")->isEnabled());
    CProgressDialog *copyProgress = nullptr;
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->isVisible())
            if (auto *dialog = dynamic_cast<CProgressDialog *>(widget))
                copyProgress = dialog;
    QVERIFY(copyProgress);
    QVERIFY(!copyProgress->findChild<QPushButton *>("IDB_PAUSE")->isVisible());
    QVERIFY(!copyProgress->findChild<QPushButton *>("IDB_PROGRESS_BACKGROUND"));
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
    QFile copiedFile(copied);
    QVERIFY(copiedFile.open(QIODevice::ReadOnly));
    QCOMPARE(copiedFile.readAll(), QByteArray("original"));
    copiedFile.close();
    QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
    table(window)->selectRow(filesystemRow(window, QStringLiteral("имя с пробелом.txt")));
    QTimer warningTimer;
    bool renameAnswered = false;
    QString finalMessage;
    connect(&warningTimer, &QTimer::timeout, &window,
            [destination, &renameAnswered, &finalMessage] {
                for (QWidget *widget : QApplication::topLevelWidgets()) {
                    if (TestProgressDialog::DismissVisibleResult(widget))
                        continue;
                    if (auto *dialog = qobject_cast<CopyDialog *>(widget);
                        dialog && dialog->isVisible()) {
                        dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                        dialog->accept();
                    }
                    if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                        finalMessage = box->text() + box->informativeText();
                        box->accept();
                    }
                    if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                        dialog && dialog->isVisible()) {
                        renameAnswered = true;
                        dialog->findChild<QPushButton *>("overwriteRename")->click();
                    }
                }
            });
    warningTimer.start(1);
    action(window, "copyAction")->trigger();
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
    warningTimer.stop();
    QVERIFY(renameAnswered);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(copied));
    QVERIFY2(
        QFileInfo::exists(QDir(destination).filePath(QStringLiteral("имя с пробелом_1.txt"))),
        qPrintable(finalMessage + " :: " +
                   QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).join(',')));
    QTimer cancelTimer;
    table(window)->selectRow(filesystemRow(window, QStringLiteral("имя с пробелом.txt")));
    bool cancelAnswered = false;
    connect(&cancelTimer, &QTimer::timeout, &window, [destination, &cancelAnswered] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->accept();
            if (auto *dialog = qobject_cast<CopyDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            }
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible()) {
                cancelAnswered = true;
                dialog->findChild<QPushButton *>("overwriteCancel")->click();
            }
        }
    });
    cancelTimer.start(1);
    action(window, "copyAction")->trigger();
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
    cancelTimer.stop();
    QVERIFY(cancelAnswered);
    QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
    QVERIFY(QFileInfo::exists(source));
    QTimer moveErrorTimer;
    table(window)->selectRow(filesystemRow(window, QStringLiteral("имя с пробелом.txt")));
    connect(&moveErrorTimer, &QTimer::timeout, &window, [destination] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *dialog = qobject_cast<CopyDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            }
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->accept();
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible())
                dialog->findChild<QPushButton *>("overwriteNo")->click();
        }
    });
    moveErrorTimer.start(1);
    action(window, "moveAction")->trigger();
    QTRY_VERIFY(action(window, "moveAction")->isEnabled());
    moveErrorTimer.stop();
    QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(copied));

    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    const int copiedRow = filesystemRow(window, QStringLiteral("имя с пробелом.txt"));
    table(window)->selectRow(copiedRow);
    QVERIFY(WindowActivation::waitForActive(window));
    table(window)->setFocus();
    QTRY_VERIFY(table(window)->hasFocus());
    SetPanelData(table(window)->model()->index(copiedRow, 1), temp.filePath("forged-rename.txt"),
                 Qt::EditRole);
    action(window, "renameAction")->trigger();
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QVERIFY(editor);
    editor->setText(QStringLiteral("новое имя.txt"));
    QTest::keyClick(editor, Qt::Key_Return);
    const QString renamed = temp.filePath(QStringLiteral("новое имя.txt"));
    QTRY_VERIFY(QFileInfo::exists(renamed));
    QVERIFY(!QFileInfo::exists(source));

    QTRY_VERIFY(filesystemRow(window, QStringLiteral("новое имя.txt")) >= 0);
    table(window)->selectRow(filesystemRow(window, QStringLiteral("новое имя.txt")));
    QTRY_VERIFY(!table(window)->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!SetPanelData(table(window)->model()->index(
                              table(window)->selectionModel()->selectedRows().first().row(), 1),
                          temp.filePath("forged-delete.txt"), Qt::EditRole));
    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *confirmationDialog = dynamic_cast<QtDeleteConfirmation *>(widget);
                confirmationDialog && confirmationDialog->isVisible())
                ReplyDelete(*confirmationDialog, false);
        }
    });
    action(window, "deleteAction")->trigger();
    QVERIFY(QFileInfo::exists(renamed));
    QTimer deleteDialogTimer;
    connect(&deleteDialogTimer, &QTimer::timeout, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *confirmationDialog = dynamic_cast<QtDeleteConfirmation *>(widget);
                confirmationDialog && confirmationDialog->isVisible()) {
                ReplyDelete(*confirmationDialog);
                continue;
            }
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                box->accept();
            }
        }
    });
    deleteDialogTimer.start(1);
    action(window, "deleteAction")->trigger();
    deleteDialogTimer.stop();
    QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
    QVERIFY(!QFileInfo::exists(renamed));
    QCOMPARE(trash.moved.size(), 1);
    QCOMPARE(QFileInfo(trash.moved.first()).canonicalPath(), QFileInfo(renamed).canonicalPath());
    QCOMPARE(QFileInfo(trash.moved.first()).fileName(), QFileInfo(renamed).fileName());
    QVERIFY(QFileInfo::exists(copied));

    QFile movable(temp.filePath("move me.txt"));
    QVERIFY(movable.open(QIODevice::WriteOnly));
    movable.write("move");
    movable.close();
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    QTRY_VERIFY(filesystemRow(window, "move me.txt") >= 0);
    table(window)->selectRow(filesystemRow(window, "move me.txt"));
    QTRY_VERIFY(!table(window)->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!SetPanelData(table(window)->model()->index(
                              table(window)->selectionModel()->selectedRows().first().row(), 1),
                          temp.filePath("forged-move.txt"), Qt::EditRole));
    QTimer::singleShot(0, &window, [destination] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (TestProgressDialog::DismissVisibleResult(widget))
                continue;
            if (auto *dialog = qobject_cast<CopyDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            }
        }
    });
    action(window, "moveAction")->trigger();
    QTRY_VERIFY(QFileInfo::exists(QDir(destination).filePath("move me.txt")));
    QVERIFY(!QFileInfo::exists(movable.fileName()));
}
