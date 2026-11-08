#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QEventLoop>
#include <QMenu>
#include <QScopeGuard>
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
    static int filesystemRow(MainWindow &window, const QString &name) {
        auto *files = active(window);
        if (files)
            for (int row = 0; row < files->rowCount(); ++row)
                if (files->model()->index(row, 0).data().toString() == name)
                    return row;
        return -1;
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void archiveHistoryKeepsListFocus_data() {
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("zip") << false;
        QTest::newRow("encrypted-7z") << true;
    }
    void archiveHistoryKeepsListFocus() {
        QFETCH(bool, encrypted);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("payload 空.txt");
        const QString format = encrypted ? "7z" : "zip";
        const QString filename = "history." + format;
        const QString archive = fixture.filePath(filename);
        const QString password = encrypted ? QString("local password") : QString();
        QVERIFY(write(fixture.filePath(name), "history payload"));
        QVERIFY(createArchive(fixture.path(), archive, name, format, password));
        const QString plainArchive = "other.zip";
        const QString plainEntry = "plain.txt";
        if (encrypted) {
            QVERIFY(write(fixture.filePath(plainEntry), "other archive payload"));
            QVERIFY(createArchive(fixture.path(), fixture.filePath(plainArchive), plainEntry));
        }
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *view = active(window);
        QVERIFY(view);
        QTRY_VERIFY(view->isEnabled() && filesystemRow(window, filename) >= 0);
        bool cancel = false;
        int canceledPrompts = 0;
        QTimer answer;
        widgetDialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(widgetDialogs_.activeDialog())) {
                auto *edit = dialog->findChild<QLineEdit *>("passwordEdit");
                if (cancel) {
                    ++canceledPrompts;
                    QTest::keyClick(edit, Qt::Key_Escape);
                } else {
                    edit->setText(password);
                    dialog->accept();
                }
            }
        });
        answer.start(5);
        view->setFocus();
        if (encrypted) {
            view->selectRow(filesystemRow(window, filename));
            view->setCurrentCell(filesystemRow(window, filename), 0);
            cancel = true;
            PanelKeys::openSelection(window.focusWidget());
            QTRY_COMPARE(canceledPrompts, 1);
            QVERIFY(WindowActivation::waitForActive(window));
            QTRY_VERIFY(view->isEnabled() && view->hasFocus() && view->sourcePanel->IsFSFolder());
            QCOMPARE(view->currentIndex().data().toString(), filename);
            QVERIFY(!action(window, "forwardAction")->isEnabled());

            const int other = filesystemRow(window, plainArchive);
            QVERIFY(other >= 0);
            view->selectRow(other);
            view->setCurrentCell(other, 0);
            const QDir peerFolder(fixture.filePath("peer"));
            QVERIFY(QDir().mkpath(peerFolder.path()));
            action(window, "twoPanelsAction")->trigger();
            auto *peerPane = window.findChild<QWidget *>("filePanel1");
            QVERIFY(peerPane && peerPane->isVisible());
            auto *peer = peerPane->findChild<FileTable *>("archiveTable");
            auto *peerAddress = peerPane->findChild<QLineEdit *>("archivePath");
            QVERIFY(peer && peerAddress);
            PanelKeys::focusAddress(peerAddress);
            QTRY_VERIFY(peerAddress->hasFocus());
            window.openPath(peerFolder.path());
            QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 0);
            peer->sourcePanel->Set_AutoRefresh_Mode(false);
            for (int i = 0; i < 301; ++i)
                QVERIFY(write(peerFolder.filePath(QString("item-%1.txt").arg(i)), "peer bytes"));
            peer->setFocus();
            QVERIFY(peer->hasFocus());
            QTest::keySequence(peer, action(window, "refreshAction")->shortcut());
            QVERIFY(!peer->isEnabled());
            view->setFocus();
            QVERIFY(view->hasFocus());
            QCOMPARE(view->currentIndex().siblingAtColumn(0).data().toString(), plainArchive);
            QVERIFY(view->selectionModel()->isRowSelected(other, {}));
            PanelKeys::openSelection(window.focusWidget());
            QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsArcFolder());
            QVERIFY(filesystemRow(window, plainEntry) >= 0);
            QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 301);
            QCOMPARE(canceledPrompts, 1);
            QVERIFY(WindowActivation::waitForActive(window));
            QTRY_VERIFY(view->hasFocus());
            QTest::keySequence(window.focusWidget(), action(window, "backAction")->shortcut());
            QTRY_VERIFY(view->isEnabled() && view->hasFocus() && view->sourcePanel->IsFSFolder());
            QCOMPARE(view->currentIndex().data().toString(), plainArchive);
            cancel = false;
        }
        const int archiveRow = filesystemRow(window, filename);
        QVERIFY(archiveRow >= 0);
        view->selectRow(archiveRow);
        view->setCurrentCell(archiveRow, 0);
        PanelKeys::openSelection(window.focusWidget());
        QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsArcFolder());
        for (bool forward : {false, true}) {
            QVERIFY(WindowActivation::waitForActive(window));
            view->setFocus();
            auto *travel = action(window, forward ? "forwardAction" : "backAction");
            QVERIFY(travel->isEnabled());
            if (forward && encrypted) {
                const int row = filesystemRow(window, filename);
                QVERIFY(row >= 0);
                view->selectRow(row);
                cancel = true;
                travel->trigger();
                QTRY_COMPARE(canceledPrompts, 2);
                QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsFSFolder());
                QTRY_COMPARE(window.focusWidget(), view);
                QVERIFY(view->selectionModel()->isRowSelected(row, {}));
                QVERIFY(travel->isEnabled());
                cancel = false;
                QVERIFY(WindowActivation::waitForActive(window));
            }
            travel->trigger();
            QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsArcFolder() == forward);
            QTRY_COMPARE(window.focusWidget(), view);
        }
    }

    void canceledFavoriteKeepsCurrentFolderAndSelection() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("payload 空.txt");
        const QString password = QStringLiteral("local 密碼");
        const QString archive = fixture.filePath("encrypted favorite.7z");
        const QByteArray bytes("favorite payload");
        QVERIFY(write(fixture.filePath(name), bytes));
        QVERIFY(createArchive(fixture.path(), archive, name, "7z", password));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *view = active(window);
        QVERIFY(view);
        QTRY_VERIFY(view->isEnabled() && filesystemRow(window, name) >= 0);
        const auto origin = view->sourcePanel->GetCurrentFolderPrefix();
        view->setCurrentCell(filesystemRow(window, name), 0);
        view->selectRow(view->currentRow());
        QtFavoriteStore *favorites = nullptr;
        for (auto *object : qApp->children())
            if (auto *store = dynamic_cast<QtFavoriteStore *>(object);
                store && store->property("settingsPath").toString() ==
                             qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"))
                favorites = store;
        QVERIFY(favorites);
        QtArchiveLocation location;
        location.physicalPath = QDir::toNativeSeparators(archive);
        favorites->Add(archive, "Encrypted favorite", {}, location);

        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        const auto open = [&] {
            menu->popup(window.mapToGlobal(QPoint(20, 20)));
            QTRY_VERIFY(menu->isVisible());
            for (auto *entry : menu->actions())
                if (entry->toolTip() == archive) {
                    menu->setActiveAction(entry);
                    QTest::keyClick(menu, Qt::Key_Return);
                    return;
                }
            menu->hide();
            QFAIL("Encrypted favorite is absent from the menu");
        };
        bool cancel = true;
        int prompts = 0;
        QTimer answer;
        widgetDialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(widgetDialogs_.activeDialog())) {
                ++prompts;
                if (cancel)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    dialog->accept();
                }
            }
        });
        answer.start(1);
        open();
        QTRY_COMPARE(prompts, 1);
        QVERIFY(view->isEnabled());
        QCOMPARE(view->sourcePanel->GetCurrentFolderPrefix(), origin);
        QCOMPARE(view->currentRow(), filesystemRow(window, name));
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        cancel = false;
        open();
        QTRY_VERIFY(view->isEnabled() && rowWithPath(window, name) >= 0 &&
                    view->sourcePanel->IsArcFolder());
        QCOMPARE(prompts, 2);
        const QString output = fixture.filePath("export");
        view->selectRow(rowWithPath(window, name));
        window.extractTo(output, true);
        QCOMPARE(read(QDir(output).filePath(name)), bytes);
    }
    void newWindowOpensCurrentNestedArchiveIndependently_data() {
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("plain") << false;
        QTest::newRow("cancel-password-and-retry") << true;
    }
    void newWindowOpensCurrentNestedArchiveIndependently() {
        QFETCH(bool, encrypted);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("payload 空.txt");
        const QString bytes = QStringLiteral("nested window payload");
        const QString password = encrypted ? QStringLiteral(" local 密碼 ") : QString();
        const QString innerName = QStringLiteral("inner архив.7z");
        const QString outer = fixture.filePath("outer.zip");
        QVERIFY(write(fixture.filePath(name), bytes.toUtf8()));
        QVERIFY(createArchive(fixture.path(), fixture.filePath(innerName), name, "7z", password));
        QVERIFY(createArchive(fixture.path(), outer, innerName));
        QtWorkspaceApplication application(*qApp);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        bool refuse = false;
        QTimer answer;
        widgetDialogs_.connectResponder(answer, &window, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(widgetDialogs_.activeDialog());
            if (!dialog)
                return;
            if (refuse)
                dialog->reject();
            else {
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                dialog->accept();
            }
        });
        answer.start(1);
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, innerName) >= 0);
        openRow(window, rowWithPath(window, innerName));
        QTRY_VERIFY(rowWithPath(window, name) >= 0 && active(window)->isEnabled());
        auto *source = active(window);
        const auto requested = source->sourcePanel->GetCurrentFolderPrefix();
        const int row = rowWithPath(window, name);
        source->selectRow(row);
        source->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        refuse = encrypted;
        action(window, "newWindowAction")->trigger();

        QPointer<MainWindow> copy;
        const auto findCopy = [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *candidate = dynamic_cast<MainWindow *>(widget);
                    candidate && candidate != &window && candidate->isVisible())
                    return candidate;
            return static_cast<MainWindow *>(nullptr);
        };
        const auto close = qScopeGuard([&] {
            if (copy)
                copy->close();
        });
        QTRY_VERIFY((copy = findCopy()));
        if (encrypted) {
            auto *retry = copy->findChild<QPushButton *>("navigationRetry0");
            QVERIFY(retry);
            QTRY_VERIFY(retry->isVisible());
            QVERIFY(!active(*copy)->isEnabled());
            QCOMPARE(source->sourcePanel->GetCurrentFolderPrefix(), requested);
            refuse = false;
            retry->click();
        }
        QTRY_VERIFY(active(*copy) && active(*copy)->isEnabled() && rowWithPath(*copy, name) >= 0);
        QCOMPARE(active(*copy)->sourcePanel->GetParentFolders().Size(), 2u);
        QCOMPARE(active(*copy)->sourcePanel->GetCurrentFolderPrefix(), requested);
        QVERIFY(active(*copy)->selectionModel()->selectedRows().isEmpty());
        QCOMPARE(source->selectionModel()->selectedRows().size(), 1);
        const QString output = fixture.filePath("export");
        active(*copy)->selectRow(rowWithPath(*copy, name));
        copy->extractTo(output, true);
        QCOMPARE(read(QDir(output).filePath(name)), bytes.toUtf8());
        QCOMPARE(source->sourcePanel->GetCurrentFolderPrefix(), requested);
    }
    void encryptedArchiveSessionResumesItsDirectory_data() {
        QTest::addColumn<bool>("legacy");
        QTest::newRow("current-session") << false;
        QTest::newRow("earlier-archive-descriptor") << true;
    }
    void encryptedArchiveSessionResumesItsDirectory() {
        QFETCH(bool, legacy);
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
        QTRY_VERIFY(active(window)->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        active(window)->setFocus();
        QTRY_VERIFY(active(window)->hasFocus());
        auto saved = workspace(window)->Save();
        if (legacy) {
            auto panels = saved.value("panels").toArray();
            auto tab = panels[0].toObject();
            tab.insert("path", ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()));
            tab.insert("archiveLocation", QJsonObject{{"physical", outer}});
            panels[0] = QJsonObject{
                {"tabs", QJsonArray{QJsonObject{{"path", QDir::homePath()}}, tab}}, {"active", 1}};
            saved.insert("panels", panels);
        }
        window.openPath(fixture.path());
        QTRY_VERIFY(active(window)->isEnabled() && active(window)->sourcePanel->IsFSFolder());
        prompts = 0;
        reject = true;
        bool finished = false;
        workspace(window)->Restore(saved, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QCOMPARE(prompts, 0);
        QVERIFY(active(window)->sourcePanel->IsFSFolder());
        QCOMPARE(QDir::cleanPath(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix())),
                 QDir::cleanPath(fixture.path()));
        QTRY_VERIFY(filesystemRow(window, QFileInfo(outer).fileName()) >= 0);
    }
    void nestedSessionResumesDirectory_data() {
        QTest::addColumn<bool>("missing");
        QTest::newRow("session") << false;
        QTest::newRow("missing-nested") << true;
    }
    void nestedSessionResumesDirectory() {
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
        const auto saved = workspace(window)->Save();
        window.openPath(fixture.path());
        QTRY_VERIFY(active(window)->isEnabled() && active(window)->sourcePanel->IsFSFolder());
        if (missing) {
            QVERIFY(QFile::remove(outer));
            QVERIFY(write(fixture.filePath("replacement.txt"), "different contents"));
            QVERIFY(createArchive(fixture.path(), outer, "replacement.txt"));
        }
        bool finished = false;
        workspace(window)->Restore(saved, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QVERIFY(active(window)->sourcePanel->IsFSFolder());
        QCOMPARE(QDir::cleanPath(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix())),
                 QDir::cleanPath(fixture.path()));
        QTRY_VERIFY(filesystemRow(window, QFileInfo(outer).fileName()) >= 0);
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
        window.openArchive(outer);
        QTRY_COMPARE(table(window)->rowCount(), 1);
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
        QVERIFY(UiInspection::writeReport(window, "workspace-nested-archive-address"));
        QTest::mouseClick(outerButton, Qt::LeftButton);
        QTRY_VERIFY(rowWithPath(window, innerName) >= 0);
        QCOMPARE(rowWithPath(window, entry), -1);
        QVERIFY(table(window)->sourcePanel->IsArcFolder());
        QCOMPARE(contents(outer, innerName), read(inner));
    }
    void externalEditDoesNotChangePeerWindowSelection() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("edited file.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("source context.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString other = fixture.filePath("Other folder");
        QVERIFY(QDir().mkpath(other));
        QVERIFY(write(other + "/keep selected.txt", "peer"));
        const QString control = fixture.filePath("editor control");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend, {}, nullptr, nullptr, DeviceSelectionServices::system(),
                          opener(control));
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "openOutsideAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        ControlledBackend peerBackend;
        MainWindow peerWindow(&peerBackend);
        peerWindow.show();
        peerWindow.openPath(other);
        auto *peer = table(peerWindow);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() > 0);
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
            auto *dialog = widgetDialogs_.activeDialog();
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (dialog && dialog->objectName() == "externalUpdatePrompt") {
                asked = true;
                dialog->findChild<QPushButton *>("externalUpdateCommit")->click();
            } else if (box) {
                errors += box->text();
                box->accept();
            }
        });
        responder.start(1);
        waitForKnownEditor();
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
        QCOMPARE(QDir::cleanPath(ToQString(peer->sourcePanel->GetCurrentFolderPrefix())), other);
        QCOMPARE(peer->model()->index(peer->currentRow(), 0).data().toString(),
                 "keep selected.txt");
        QVERIFY(peer->selectionModel()->isRowSelected(peer->currentRow(), {}));
        QCOMPARE(contents(archive, name), modified);
        QTRY_COMPARE(table(window)->model()->index(0, 2).data().toString(),
                     QtRegionalFormat::bytes(quint64(modified.size())));
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
