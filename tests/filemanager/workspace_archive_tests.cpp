#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "platform/support/external_editor_fixture.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QEventLoop>
#include <QScopeGuard>
#include <QTabBar>
#include <QToolButton>

using namespace ExternalEditorFixture;

class WorkspaceArchiveTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *service = dynamic_cast<QtWindowWorkspace *>(object))
                return service;
        return nullptr;
    }
    static FileTable *active(MainWindow &window) {
        for (auto *pane : window.findChildren<QWidget *>("filePanel0"))
            if (pane->isVisible())
                return pane->findChild<FileTable *>("archiveTable");
        return nullptr;
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cancelledNestedReopenKeepsDescriptorAndCanRetry() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("secret payload.txt");
        QVERIFY(write(fixture.filePath(entry), "encrypted control"));
        const QString password = QStringLiteral(" local 密碼 ");
        const QString innerName = QStringLiteral("inner encrypted.7z");
        const QString parent = QStringLiteral("d");
        QVERIFY(QDir().mkpath(fixture.filePath(parent + '/' + parent)));
        QVERIFY(createArchive(fixture.path(), fixture.filePath(parent + '/' + innerName), entry,
                              "7z", password));
        const QString outer = fixture.filePath("outer archive.zip");
        QVERIFY(createArchive(fixture.path(), outer, parent));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        int prompts = 0;
        bool reject = false;
        QTimer answer;
        widgetDialogs_.connectResponder(answer, &window, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(widgetDialogs_.activeDialog());
            if (!dialog)
                return;
            ++prompts;
            if (reject)
                dialog->reject();
            else {
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                dialog->accept();
            }
        });
        answer.start(1);
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, parent) >= 0);
        openRow(window, rowWithPath(window, parent));
        QTRY_VERIFY(rowWithPath(window, parent + '/' + innerName) >= 0);
        openRow(window, rowWithPath(window, parent + '/' + innerName));
        QTRY_VERIFY(rowWithPath(window, entry) >= 0 && active(window)->isEnabled());
        action(window, "twoPanelsAction")->trigger();
        QTRY_VERIFY(action(window, "newTabAction")->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        active(window)->setFocus();
        QTRY_VERIFY(active(window)->hasFocus());
        const QString requested = ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix());
        const auto saved = workspace(window)->Save();
        workspace(window)->NewTab(0, fixture.path());
        QTRY_VERIFY(active(window)->isEnabled());
        workspace(window)->CloseTab(0, 0);
        prompts = 0;
        reject = true;
        bool finished = false;
        workspace(window)->Restore(saved, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QVERIFY(prompts > 0);
        QVERIFY(!action(window, "copyFilesAction")->isEnabled());
        const QString canceled =
            QCoreApplication::translate("QtWindowWorkspace", "Opening canceled: %1").arg(requested);
        bool errorVisible = false;
        for (auto *label : window.findChildren<QLabel *>())
            errorVisible |= label->isVisible() && label->text() == canceled;
        QVERIFY(errorVisible);
        prompts = 0;
        reject = false;
        QPushButton *retry = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->isVisible() &&
                button->text() == QCoreApplication::translate("QtWindowWorkspace", "Retry"))
                retry = button;
        QVERIFY(retry);
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_VERIFY(active(window)->isEnabled());
        QVERIFY(prompts > 0);
        QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 2u);
        QCOMPARE(active(window)->model()->index(0, 0).data().toString(), entry);
    }
    void nestedLocationReopensWithItsNativeChain_data() {
        QTest::addColumn<bool>("restore");
        QTest::addColumn<bool>("missing");
        QTest::newRow("duplicate") << false << false;
        QTest::newRow("session") << true << false;
        QTest::newRow("missing-nested") << true << true;
    }
    void nestedLocationReopensWithItsNativeChain() {
        QFETCH(bool, restore);
        QFETCH(bool, missing);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("данные 空.txt");
        const QByteArray bytes("nested reopening control");
        const QString inside = QStringLiteral("inside 空");
        QVERIFY(QDir().mkpath(fixture.filePath(inside)));
        QVERIFY(write(fixture.filePath(inside + '/' + entry), bytes));
        for (int index = 0; index < 300; ++index)
            QVERIFY(
                write(fixture.filePath(inside + QString("/entry-%1.txt").arg(index)), "control"));
        const QString top = QStringLiteral("level one");
        const QString parent = top + QStringLiteral("/folder с пробелом.zip");
        QVERIFY(QDir().mkpath(fixture.filePath(parent)));
        const QString innerName = QStringLiteral("inner archive.zip");
        QVERIFY(createArchive(fixture.path(), fixture.filePath(parent + '/' + innerName), inside));
        const QString outer = fixture.filePath("outer archive.zip");
        QVERIFY(createArchive(fixture.path(), outer, top));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, top) >= 0);
        openRow(window, rowWithPath(window, top));
        QTRY_VERIFY(rowWithPath(window, parent) >= 0);
        openRow(window, rowWithPath(window, parent));
        QTRY_VERIFY(rowWithPath(window, parent + '/' + innerName) >= 0);
        openRow(window, rowWithPath(window, parent + '/' + innerName));
        QTRY_VERIFY(rowWithPath(window, inside) >= 0);
        openRow(window, rowWithPath(window, inside));
        QTRY_VERIFY(rowWithPath(window, inside + '/' + entry) >= 0);
        QTRY_VERIFY(active(window)->isEnabled());
        QPointer<CPanel> original(active(window)->sourcePanel);
        const QString requested = ToQString(original->GetCurrentFolderPrefix());
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        if (restore) {
            const auto saved = workspace(window)->Save();
            workspace(window)->NewTab(0, fixture.path());
            QTRY_VERIFY(active(window)->isEnabled());
            workspace(window)->CloseTab(0, 0);
            QTRY_VERIFY(original.isNull());
            if (missing) {
                QVERIFY(QFile::remove(outer));
                QVERIFY(write(fixture.filePath("replacement.txt"), "different contents"));
                QVERIFY(createArchive(fixture.path(), outer, "replacement.txt"));
            }
            bool finished = false;
            workspace(window)->Restore(saved, [&] { finished = true; });
            QTRY_VERIFY(finished);
            if (missing) {
                QCOMPARE(active(window)->rowCount(), 0);
                QVERIFY(!action(window, "copyFilesAction")->isEnabled());
                QVERIFY(!action(window, "createFolderAction")->isEnabled());
                bool errorVisible = false;
                for (auto *label : window.findChildren<QLabel *>())
                    errorVisible |= label->isVisible() && label->text().contains(requested);
                QVERIFY(errorVisible);
#ifndef Q_OS_WIN
                // POSIX permits replacing an archive while its old reader is
                // alive. Retry must read the new file rather than that reader.
                QVERIFY(QFile::rename(outer, outer + ".stale"));
                QVERIFY(createArchive(fixture.path(), outer, top));
                QPushButton *retry = nullptr;
                for (auto *button : window.findChildren<QPushButton *>())
                    if (button->isVisible() &&
                        button->text() == QCoreApplication::translate("QtWindowWorkspace", "Retry"))
                        retry = button;
                QVERIFY(retry);
                QTest::mouseClick(retry, Qt::LeftButton);
#else
                return;
#endif
            }
        } else {
            bool invoked = false;
            QTimer choose;
            choose.setInterval(5);
            connect(&choose, &QTimer::timeout, &window, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                choose.stop();
                const auto close = qScopeGuard([menu] { menu->close(); });
                for (auto *action : menu->actions())
                    if (action->text() ==
                        QCoreApplication::translate("QtWindowWorkspace", "Duplicate location")) {
                        invoked = true;
                        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                          menu->actionGeometry(action).center());
                        break;
                    }
            });
            choose.start();
            QVERIFY(QMetaObject::invokeMethod(tabs, "customContextMenuRequested",
                                              Qt::DirectConnection,
                                              Q_ARG(QPoint, tabs->tabRect(0).center())));
            QVERIFY(invoked);
            QTRY_COMPARE(tabs->count(), 2);
            QVERIFY(original && original != active(window)->sourcePanel);
            QVERIFY(original->IsArcFolder());
        }
        QTRY_VERIFY(active(window)->isEnabled());
        QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 2u);
        int row = -1;
        for (int index = 0; index < active(window)->rowCount(); ++index)
            if (active(window)->model()->index(index, 0).data().toString() == entry)
                row = index;
        QVERIFY(row >= 0);
        QApplication::clipboard()->clear();
        const auto clearClipboard = qScopeGuard([] { QApplication::clipboard()->clear(); });
        active(window)->selectRow(row);
        active(window)->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        action(window, "copyFilesAction")->trigger();
        QTRY_VERIFY(QApplication::clipboard()->mimeData()->hasUrls());
        const auto exported = QApplication::clipboard()->mimeData()->urls();
        QCOMPARE(exported.size(), 1);
        QCOMPARE(read(exported.front().toLocalFile()), bytes);
    }
    void breadcrumbsNavigateBothNestedArchiveBoundaries() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("данные 空.txt");
        QVERIFY(write(fixture.filePath(entry), "nested control"));
        const QString innerName = QStringLiteral("inner archive.zip");
        const QString inner = fixture.filePath(innerName);
        QVERIFY(createArchive(fixture.path(), inner, entry));
        const QString outer = fixture.filePath("outer archive.zip");
        QVERIFY(createArchive(fixture.path(), outer, innerName));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        const QString directory = fixture.filePath("ordinary folder.zip");
        QVERIFY(QDir().mkpath(directory));
        window.openPath(directory);
        QTRY_COMPARE(
            QDir::cleanPath(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix())),
            directory);
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        const QImage folderSymbol = tabs->tabIcon(0).pixmap(24, 24).toImage();
        window.openArchive(outer);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QTRY_VERIFY(tabs->tabIcon(0).pixmap(24, 24).toImage() != folderSymbol);
        openRow(window, rowWithPath(window, innerName));
        QTRY_VERIFY(rowWithPath(window, entry) >= 0);
        auto *bar = window.findChild<QWidget *>("panelNavigation0");
        QVERIFY(bar);
        const auto normalized = [](const QString &path) {
            return QDir::cleanPath(QDir::fromNativeSeparators(path));
        };
        QToolButton *outerButton = nullptr;
        QToolButton *innerButton = nullptr;
        for (auto *button : bar->findChildren<QToolButton *>()) {
            if (!button->isVisible())
                continue;
            const QString target = normalized(button->toolTip());
            if (target == normalized(outer))
                outerButton = button;
            if (target == normalized(outer + '/' + innerName))
                innerButton = button;
        }
        QVERIFY(outerButton);
        QVERIFY(innerButton);
        QVERIFY(!outerButton->icon().isNull());
        QVERIFY(!innerButton->icon().isNull());
        QVERIFY(UiInspection::writeReport(window, "workspace-nested-archive-tab"));
        QTest::mouseClick(outerButton, Qt::LeftButton);
        QTRY_VERIFY(rowWithPath(window, innerName) >= 0);
        QCOMPARE(rowWithPath(window, entry), -1);
        QVERIFY(table(window)->sourcePanel->IsArcFolder());
        QCOMPARE(contents(outer, innerName), read(inner));
    }
    void movedArchiveCanExportAfterOriginalWindowAndBackendAreDestroyed() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("данные file.txt");
        const QByteArray bytes("archive clipboard\0bytes", 23);
        QVERIFY(write(fixture.filePath(name), bytes));
        const QString archive = fixture.filePath("moving archive.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString destination = fixture.filePath("Export target");
        QVERIFY(QDir().mkpath(destination));
        auto originalBackend = std::make_unique<ArchiveBackend>();
        auto original = std::make_unique<MainWindow>(originalBackend.get());
        ArchiveBackend receiverBackend;
        MainWindow receiver(&receiverBackend);
        original->show();
        receiver.show();
        original->openArchive(archive);
        QTRY_COMPARE(active(*original)->rowCount(), 1);
        QTRY_VERIFY(active(*original)->isEnabled());
        active(*original)->selectRow(0);
        active(*original)->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        auto *tabs = original->findChild<QTabBar *>("panelTabs0");
        receiver.openPath(destination);
        QTRY_VERIFY(active(receiver)->isEnabled());
        const QString id = tabs->tabData(0).toString();
        bool moved = false;
        QTRY_VERIFY(moved ||
                    (moved = workspace(*original)->TransferTab(id, *workspace(receiver), 0)));
        original.reset();
        originalBackend.reset();
        QTRY_COMPARE(active(receiver)->rowCount(), 1);
        QTRY_VERIFY(action(receiver, "copyFilesAction")->isEnabled());
        action(receiver, "copyFilesAction")->trigger();
        QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
        receiver.openPath(destination);
        QTRY_VERIFY(action(receiver, "pasteFilesAction")->isEnabled());
        action(receiver, "pasteFilesAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(read(destination + '/' + name), bytes, 10000);
        QTRY_VERIFY(active(receiver)->isEnabled());
        QApplication::clipboard()->clear();
    }
    void externalEditReturnsToInactiveTabWithoutChangingActiveSelection() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("edited file.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("tab context.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString other = fixture.filePath("Other folder");
        QVERIFY(QDir().mkpath(other));
        QVERIFY(write(other + "/keep selected.txt", "peer"));
        const QString control = fixture.filePath("tab editor control");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        configure(control);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        action(window, "newTabAction")->trigger();
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        QTRY_COMPARE(tabs->count(), 2);
        const auto active = [&window]() -> FileTable * {
            for (auto *view : window.findChildren<FileTable *>())
                if (view->isVisible())
                    return view;
            return nullptr;
        };
        QTRY_VERIFY(active() && active()->isEnabled());
        window.openPath(other);
        auto *peer = active();
        int row = -1;
        for (int i = 0; i < peer->rowCount(); ++i)
            if (peer->model()->index(i, 0).data().toString() == "keep selected.txt")
                row = i;
        QVERIFY(row >= 0);
        peer->selectRow(row);
        peer->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        bool asked = false;
        QString errors;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, &window, [&] {
            auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog());
            if (box && box->objectName() == "externalUpdatePrompt") {
                asked = true;
                box->button(QMessageBox::Yes)->click();
            } else if (box) {
                errors += box->text();
                box->accept();
            }
        });
        responder.start(1);
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QEventLoop delivery;
        QTimer finished;
        connect(&finished, &QTimer::timeout, &delivery, [&] {
            if (!QFileInfo::exists(temporary))
                delivery.quit();
        });
        finished.start(5);
        QTimer::singleShot(10000, &delivery, &QEventLoop::quit);
        delivery.exec();
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QVERIFY(asked);
        QTRY_VERIFY(!QFileInfo::exists(temporary));
        QCOMPARE(tabs->currentIndex(), 1);
        QCOMPARE(QDir::cleanPath(ToQString(active()->sourcePanel->GetCurrentFolderPrefix())),
                 other);
        QCOMPARE(active()->model()->index(active()->currentRow(), 0).data().toString(),
                 "keep selected.txt");
        QVERIFY(active()->selectionModel()->isRowSelected(active()->currentRow(), {}));
        QCOMPARE(contents(archive, name), modified);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(active()->model()->index(0, 2).data().toString(),
                     QString::number(modified.size()));
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    UiInspection::configureReviewAppearance();
    WorkspaceArchiveTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "workspace_archive_tests.moc"
