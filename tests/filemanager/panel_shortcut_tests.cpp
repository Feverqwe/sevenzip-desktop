#include "dialogs/combo/ComboDialog.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/review_test_main.h"
#include <QClipboard>
#include <QScopeGuard>
#include <QToolBar>
#include <QVBoxLayout>
#include <memory>

namespace {
int row(FileTable &table, const QString &name) {
    for (int i = 0; i < table.rowCount(); ++i)
        if (table.model()->index(i, 0).data().toString() == name)
            return i;
    return -1;
}
bool write(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("protected bytes") == 15;
}
} // namespace

class PanelShortcutTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void transferTargetsFocusedPanel_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("icons");
        QTest::newRow("left-copy-table") << 0u << false << false;
        QTest::newRow("right-copy-icons") << 1u << false << true;
        QTest::newRow("left-move-icons") << 0u << true << true;
        QTest::newRow("right-move-table") << 1u << true << false;
    }

    void transferTargetsFocusedPanel() {
        QFETCH(unsigned, pane);
        QFETCH(bool, move);
        QFETCH(bool, icons);
        QTemporaryDir fixture, destination;
        QVERIFY(fixture.isValid() && destination.isValid());
        const QString name = QStringLiteral("файл with space 空.txt");
        QVERIFY(write(fixture.filePath(name)));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(pane, ToUString(fixture.path()), settings);
        SavePanelPath(1 - pane, ToUString(destination.path()), settings);
        CWindowInfo info{{0, 0, 980, 620}, false, 2, pane, 490};
        info.Save(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *table = panel->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(row(*table, name) >= 0);
        table->setCurrentCell(row(*table, name), 0);
        table->selectRow(row(*table, name));
        if (icons)
            action(window, "viewMode0Action")->trigger();
        QWidget *view = icons ? panel->findChild<QWidget *>("iconList") : table;
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        auto *command = action(window, move ? "moveAction" : "copyAction");
        QTRY_VERIFY(command->isEnabled());
        QSignalSpy invoked(command, &QAction::triggered);
        int shown = 0;
        bool correctSource = false, correctDestination = false;
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = qobject_cast<CCopyDialog *>(dialogs_.activeDialog())) {
                ++shown;
                correctSource =
                    ToQString(dialog->Info).normalized(QString::NormalizationForm_C).contains(name);
                correctDestination = QFileInfo(dialog->destination()).canonicalFilePath() ==
                                     QFileInfo(destination.path()).canonicalFilePath();
                dialog->reject();
            }
        });
        answer.start(5);
        PanelKeys::command(view, move ? Qt::Key_M : Qt::Key_C,
                           Qt::ControlModifier | Qt::ShiftModifier);
        answer.stop();
        QCOMPARE(invoked.size(), 1);
        QCOMPARE(shown, 1);
        QVERIFY(correctSource && correctDestination);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QVERIFY(QFileInfo::exists(fixture.filePath(name)));
        QVERIFY(!QFileInfo::exists(destination.filePath(name)));
        QVERIFY(!backend.busy());
    }

    void createAndNavigateWithoutFunctionKeys() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(0, ToUString(fixture.path()), settings);
        CWindowInfo info{{0, 0, 980, 620}, false, 1, 0, 490};
        info.Save(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *path = window.findChild<QLineEdit *>("archivePath");
        QTRY_VERIFY(table->isEnabled());
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        const QString created = QStringLiteral("новая папка 空");
        QSignalSpy invoked(action(window, "createFolderAction"), &QAction::triggered);
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = qobject_cast<CComboDialog *>(dialogs_.activeDialog())) {
                dialog->findChild<QComboBox *>("comboValue")->setEditText(created);
                dialog->accept();
            }
        });
        answer.start(5);
        PanelKeys::command(table, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        answer.stop();
        QCOMPARE(invoked.size(), 1);
        QVERIFY(QFileInfo(fixture.filePath(created)).isDir());
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(row(*table, created) >= 0);
        table->setCurrentCell(row(*table, created), 0);
        table->selectRow(row(*table, created));
        table->setFocus();
#ifdef Q_OS_MACOS
        PanelKeys::command(table, Qt::Key_Down, Qt::ControlModifier);
#else
        QTest::keyClick(table, Qt::Key_Return);
#endif
        QTRY_COMPARE(QFileInfo(path->text()).canonicalFilePath(),
                     QFileInfo(fixture.filePath(created)).canonicalFilePath());
        QTRY_VERIFY(table->isEnabled());
        table->setFocus();
#ifdef Q_OS_MACOS
        PanelKeys::command(table, Qt::Key_Up, Qt::ControlModifier);
#else
        PanelKeys::command(table, Qt::Key_Up, Qt::AltModifier);
#endif
        QTRY_COMPARE(QFileInfo(path->text()).canonicalFilePath(),
                     QFileInfo(fixture.path()).canonicalFilePath());
        QTRY_VERIFY(table->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        table->setFocus();
        PanelKeys::command(table, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(path->hasFocus());
        QCOMPARE(path->selectedText(), path->text());
        PanelKeys::command(path, Qt::Key_2, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(action(window, "twoPanelsAction")->isChecked());
        QVERIFY(UiInspection::writeReport(window, "modern-shortcuts-two-panels"));
    }

    void reservedAndEditorKeysDoNotInvokeFileCommands() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("unchanged 空.txt");
        QVERIFY(write(fixture.filePath(name)));
        QVERIFY(write(fixture.filePath("other.txt")));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(0, ToUString(fixture.path()), settings);
        CWindowInfo info{{0, 0, 980, 620}, false, 1, 0, 490};
        info.Save(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *path = window.findChild<QLineEdit *>("archivePath");
        QTRY_VERIFY(row(*table, name) >= 0);
        table->setCurrentCell(row(*table, name), 0);
        table->selectRow(row(*table, name));
        table->setFocus();
        int unexpectedDialogs = 0;
        QTimer rejectUnexpected;
        dialogs_.connectResponder(rejectUnexpected, &window, [&] {
            if (auto *dialog = dialogs_.activeDialog()) {
                ++unexpectedDialogs;
                dialog->reject();
            }
        });
        rejectUnexpected.start(5);
        auto *add = action(window, "toolbarAddAction");
        const bool addEnabled = add->isEnabled();
        add->setEnabled(false);
        QSignalSpy added(add, &QAction::triggered);
        PanelKeys::command(table, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(added.size(), 0);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        add->setEnabled(addEnabled);
        PanelKeys::command(table, Qt::Key_N, Qt::ControlModifier);
        PanelKeys::command(table, Qt::Key_Z, Qt::ControlModifier);
        PanelKeys::focusAddress(path);
        path->setText("draft");
        QTest::keyClicks(path, "x");
        QTest::keySequence(path, QKeySequence::Undo);
        QCOMPARE(path->text(), QStringLiteral("draft"));
        QSignalSpy copy(action(window, "copyAction"), &QAction::triggered);
        QSignalSpy deleted(action(window, "deleteAction"), &QAction::triggered);
        PanelKeys::command(path, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        PanelKeys::command(path, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(deleted.size(), 0);
        QCOMPARE(copy.size(), 0);
        path->setText(fixture.path());
        table->setFocus();
#ifdef Q_OS_MACOS
        QTest::keyClick(table, Qt::Key_Return);
#else
        QTest::keyClick(table, Qt::Key_F2);
#endif
        QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
        auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(editor != path && table->isAncestorOf(editor));
        editor->setText("draft");
        QTest::keyClicks(editor, "x");
        QTest::keySequence(editor, QKeySequence::Undo);
        QCOMPARE(editor->text(), QStringLiteral("draft"));
        PanelKeys::command(editor, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        PanelKeys::command(editor, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        PanelKeys::command(editor, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(deleted.size(), 0);
        QCOMPARE(copy.size(), 0);
        QTest::keyClick(editor, Qt::Key_Escape);
        rejectUnexpected.stop();
        QCOMPARE(unexpectedDialogs, 0);
        QVERIFY(QFileInfo::exists(fixture.filePath(name)));
        QVERIFY(!QFileInfo::exists(fixture.filePath("draft")));
    }

    void modalAndClipboardEditingDoNotReachPanels() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        QSignalSpy add(action(window, "toolbarAddAction"), &QAction::triggered);
        QSignalSpy copy(action(window, "copyAction"), &QAction::triggered);
        auto savedClipboard = std::make_unique<QMimeData>();
        auto *clipboard = QApplication::clipboard();
        if (const auto *original = clipboard->mimeData())
            for (const QString &format : original->formats())
                savedClipboard->setData(format, original->data(format));
        const auto restoreClipboard =
            qScopeGuard([&] { clipboard->setMimeData(savedClipboard.release()); });
        QDialog dialog(&window);
        auto *layout = new QVBoxLayout(&dialog);
        auto *edit = new QLineEdit(&dialog);
        layout->addWidget(edit);
        bool checked = false;
        QTimer::singleShot(0, &dialog, [&] {
            const auto close = qScopeGuard([&] { dialog.reject(); });
            QVERIFY(WindowActivation::waitForActive(dialog));
            edit->setFocus();
            edit->setText("clipboard draft");
            edit->selectAll();
            QTest::keySequence(edit, QKeySequence::Cut);
            QVERIFY(edit->text().isEmpty());
            QTest::keySequence(edit, QKeySequence::Paste);
            QCOMPARE(edit->text(), QStringLiteral("clipboard draft"));
            PanelKeys::command(edit, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
            PanelKeys::command(edit, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
            PanelKeys::command(edit, Qt::Key_W, Qt::ControlModifier);
            QVERIFY(dialog.isVisible() && window.isVisible());
            QCOMPARE(add.size(), 0);
            QCOMPARE(copy.size(), 0);
            checked = true;
        });
        dialog.exec();
        QVERIFY(checked);
    }

    void nativeCommandAUsesZeroVirtualKey() {
#ifndef Q_OS_MACOS
        QSKIP("Cocoa native virtual-key protocol");
#else
        if (QApplication::platformName() != "cocoa")
            QSKIP("Requires native Cocoa key mapping");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("a.txt")) && write(fixture.filePath("b.txt")));
        SavePanelPath(0, ToUString(fixture.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CWindowInfo info{{0, 0, 980, 620}, false, 1, 0, 490};
        info.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(view->isEnabled() && row(*view, "a.txt") >= 0);
        view->setCurrentCell(row(*view, "a.txt"), 0);
        view->selectRow(row(*view, "a.txt"));
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QSignalSpy add(action(window, "toolbarAddAction"), &QAction::triggered);
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dialogs_.activeDialog())
                dialog->reject();
        });
        answer.start(5);
        // Recorded Cocoa protocol: physical A has virtual key zero. The
        // Cyrillic logical character must not hide the native Command chord.
        const auto modifiers = Qt::ControlModifier | Qt::ShiftModifier;
        constexpr quint32 nativeModifiers = (1u << 20) | (1u << 17);
        QKeyEvent override(QEvent::ShortcutOverride, 0x0424, modifiers, 0, 0, nativeModifiers);
        override.ignore();
        QApplication::sendEvent(view, &override);
        QVERIFY(override.isAccepted());
        QKeyEvent press(QEvent::KeyPress, 0x0424, modifiers, 0, 0, nativeModifiers);
        QApplication::sendEvent(view, &press);
        answer.stop();
        QTest::keyRelease(view, Qt::Key_Shift);
        QTest::keyRelease(view, Qt::Key_Control);
        QCOMPARE(add.size(), 1);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
#endif
    }

    void windowShortcutsRespectActiveWindow() {
        ControlledBackend backend, peerBackend;
        MainWindow window(&backend), peer(&peerBackend);
        peer.show();
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *path = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(path);
        QTRY_VERIFY(path->hasFocus());
        bool settingsSeen = false;
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dialogs_.activeDialog()) {
                settingsSeen =
                    dialog->objectName() == "settingsDialog" && dialog->parentWidget() == &window;
                dialog->reject();
            }
        });
        answer.start(5);
        PanelKeys::command(path, Qt::Key_Comma, Qt::ControlModifier);
        answer.stop();
        QVERIFY(settingsSeen);
        QVERIFY(window.isVisible() && peer.isVisible());
        QVERIFY(WindowActivation::waitForActive(window));
        auto *toolbar = window.findChild<QToolBar *>("panelFileActions0");
        auto *button = toolbar->widgetForAction(toolbar->actions().first());
        QVERIFY(button);
        button->setFocus();
        QTRY_VERIFY(button->hasFocus());
        PanelKeys::command(button, Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY(!window.isVisible());
        QVERIFY(peer.isVisible());
    }
};

SEVENZIP_UI_TEST_MAIN(PanelShortcutTests)
#include "panel_shortcut_tests.moc"
