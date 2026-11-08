#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QContextMenuEvent>
#include <QScopeGuard>
#include <QToolButton>

class SourceContextMenuTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QAction *menuAction(QMenu &menu, const char *name) {
        for (QAction *action : menu.actions())
            if (action->objectName() == QLatin1String(name))
                return action;
            else if (action->menu())
                if (auto *found = menuAction(*action->menu(), name))
                    return found;
        return nullptr;
    }
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        QToolButton *edit = nullptr;
        for (auto *button : ui->findChildren<QToolButton *>())
            if (button->isVisible() &&
                button->accessibleName() ==
                    QCoreApplication::translate("QtNavigationBar", "Edit location"))
                edit = button;
        if (!edit)
            return nullptr;
        QTest::mouseClick(edit, Qt::LeftButton);
        if (!address->isVisible() || !address->hasFocus())
            return nullptr;
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data(Qt::UserRole + 3).toString() == name)
                return i;
        return -1;
    }

  private slots:
    void contextMenuKeepsSelection_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("details-keyboard") << false << true;
        QTest::newRow("icons-keyboard") << true << true;
        QTest::newRow("details-mouse") << false << false;
        QTest::newRow("icons-mouse") << true << false;
    }

    void contextMenuKeepsSelection() {
        QFETCH(bool, icons);
        QFETCH(bool, keyboard);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const auto *name : {"a.txt", "b.txt", "c.txt"}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(name), qint64(5));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *files = openPanel(window, 0, fixture.path());
        QVERIFY(files);
        QTRY_VERIFY(row(*files, "c.txt") >= 0);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *stack = window.findChild<QStackedWidget *>("fileViewStack");
        auto *view = qobject_cast<QAbstractItemView *>(stack->currentWidget());
        QVERIFY(view);
        view->setFocus();
        auto *selection = view->selectionModel();
        const auto index = [&](const char *name) {
            return view->model()->index(row(*files, name), 0);
        };
        const QModelIndex focused = index("a.txt");
        selection->setCurrentIndex(focused,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        selection->select(index("b.txt"), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const auto selected = selection->selectedRows();
        QCOMPARE(selected.size(), 2);
        const QModelIndex hit = index(keyboard ? "c.txt" : "b.txt");
        const QPoint point = view->visualRect(hit).center();
        QCOMPARE(view->indexAt(point).row(), hit.row());
        bool menuShown = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (menu) {
                menuShown = menu->objectName() == "panelContextMenu";
                const auto close = qScopeGuard([&] { menu->close(); });
                if (!keyboard) {
                    auto *rename = menuAction(*menu, "renameAction");
                    QVERIFY(rename && rename->isEnabled());
                    menu->setActiveAction(rename);
                    QTest::keyClick(menu, Qt::Key_Return);
                }
            }
        });
        QContextMenuEvent event(keyboard ? QContextMenuEvent::Keyboard : QContextMenuEvent::Mouse,
                                point, view->viewport()->mapToGlobal(point));
        QApplication::sendEvent(view->viewport(), &event);
        QVERIFY(menuShown);
        QCOMPARE(selection->currentIndex(), keyboard ? focused : hit);
        QCOMPARE(selection->selectedRows(), selected);
        if (!keyboard) {
            auto *editor = view->findChild<QLineEdit *>();
            QVERIFY(editor);
            QCOMPARE(editor->text(), QString("b.txt"));
            editor->setText("renamed.txt");
            QTest::keyClick(editor, Qt::Key_Return);
            QFile renamed(fixture.filePath("renamed.txt"));
            QTRY_VERIFY(renamed.exists());
            QVERIFY(renamed.open(QIODevice::ReadOnly));
            QCOMPARE(renamed.readAll(), QByteArray("b.txt"));
            QVERIFY(!QFileInfo::exists(fixture.filePath("b.txt")));
            QFile peer(fixture.filePath("a.txt"));
            QVERIFY(peer.open(QIODevice::ReadOnly));
            QCOMPARE(peer.readAll(), QByteArray("a.txt"));
        }
    }

    void failedPasteLeavesTheDestinationPanelUsable() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const auto clearClipboard = qScopeGuard([] { QApplication::clipboard()->clear(); });
        QTemporaryDir destination, peerDirectory;
        QVERIFY(destination.isValid() && peerDirectory.isValid());
        const QString child = destination.filePath("child");
        QVERIFY(QDir().mkpath(child));
        QFile control(child + "/control.txt");
        QVERIFY(control.open(QIODevice::WriteOnly));
        control.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = openPanel(window, 0, destination.path());
        auto *peer = openPanel(window, 1, peerDirectory.path());
        QVERIFY(owner && peer);
        QTRY_VERIFY(row(*owner, "child") >= 0);
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());

        const QString missingName = QStringLiteral("missing 文件.txt");
        const QString existingName = QStringLiteral("ready файл.txt");
        const auto writeSource = [&](const QString &name, const QByteArray &bytes) {
            QFile file(peerDirectory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        };
        writeSource(existingName, "ready bytes");
        const QList<QUrl> sources{QUrl::fromLocalFile(peerDirectory.filePath(existingName)),
                                  QUrl::fromLocalFile(peerDirectory.filePath(missingName))};
        auto *clipboard = new QMimeData;
        clipboard->setUrls(sources);
        QApplication::clipboard()->setMimeData(clipboard);
        const QString pasteLabel = action(window, "pasteFilesAction")->text();
        bool pasteChosen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto close = qScopeGuard([&] { menu->close(); });
            for (auto *item : menu->actions())
                if (item->text() == pasteLabel) {
                    QVERIFY(item->isEnabled());
                    pasteChosen = true;
                    menu->setActiveAction(item);
                    QTest::keyClick(menu, Qt::Key_Return);
                    return;
                }
            QFAIL("Paste action missing");
        });
        QString report;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *result = TestProgressDialog::ResultWindow(widget);
                    result && result->isVisible()) {
                    report = TestProgressDialog::ReportText(*result);
                    TestProgressDialog::CloseResult(*result);
                }
        });
        dismiss.start(5);
        const QPoint point =
            owner->visualRect(owner->model()->index(row(*owner, "child"), 0)).center();
        QVERIFY(QMetaObject::invokeMethod(owner, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, point)));
        QVERIFY(pasteChosen);
        QTRY_VERIFY(!report.isEmpty());
        dismiss.stop();
        QVERIFY(report.contains(missingName));
        QVERIFY(!QFileInfo::exists(destination.filePath(missingName)));
        QVERIFY(!QFileInfo::exists(destination.filePath(existingName)));
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), sources);
        QTRY_VERIFY(action(window, "settingsAction")->isEnabled());
        window.activateWindow();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *ownerUi = window.findChild<QWidget *>("filePanel0");
        auto *address = ownerUi->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(child);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(row(*owner, "control.txt") >= 0);
        writeSource(missingName, "restored bytes");
        auto *paste = action(window, "pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY(owner->isEnabled() && row(*owner, missingName) >= 0 &&
                    row(*owner, existingName) >= 0);
        QFile restored(QDir(child).filePath(missingName));
        QFile existing(QDir(child).filePath(existingName));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArray("restored bytes"));
        QCOMPARE(existing.readAll(), QByteArray("ready bytes"));
    }

    void owningPanel_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("icons");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool icons : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(icons)))
                        << pane << language << icons;
    }
    void owningPanel() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(bool, icons);
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
        preferences.Save(settings);
        QTemporaryDir fixture, peerDirectory;
        QVERIFY(fixture.isValid() && peerDirectory.isValid());
        const QString original = QStringLiteral("original 空.txt");
        const QString created = QStringLiteral("created 空");
        for (const QString &path : {fixture.filePath(original), peerDirectory.filePath(original)}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected"), qint64(9));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = openPanel(window, pane, fixture.path());
        QVERIFY(owner);
        QTRY_COMPARE(owner->rowCount(), 1);
        owner->sourcePanel->Set_AutoRefresh_Mode(false);
        owner->setCurrentCell(row(*owner, original), 0);
        owner->selectRow(row(*owner, original));
        QWidget *ownerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *iconView = ownerUi->findChild<QListView *>("iconList");
        if (icons) {
            action(window, "viewMode0Action")->trigger();
            QVERIFY(iconView);
        }
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QVERIFY(peer);
        QTRY_COMPARE(peer->rowCount(), 1);
        peer->setCurrentCell(0, 0);
        peer->selectRow(0);
        peer->setFocus();
        CMyComPtr<IFolderFolder> ownerFolder = owner->sourcePanel->GetFolder();
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        QAbstractItemView *view = icons ? static_cast<QAbstractItemView *>(iconView) : owner;
        const int timeColumn = owner->columnForProperty(kpidATime);
        QVERIFY(timeColumn >= 0);
        const bool ownerHidden = owner->isColumnHidden(timeColumn);
        const int peerTimeColumn = peer->columnForProperty(kpidATime);
        const bool peerHidden = peer->isColumnHidden(peerTimeColumn);
        const CPropColumns peerCatalog = peer->sourcePanel->GetColumns();
        bool headerObserved = false;
        QTimer headerAnswer;
        connect(&headerAnswer, &QTimer::timeout, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu || !menu->isVisible())
                return;
            headerAnswer.stop();
            const auto close = qScopeGuard([&] { menu->close(); });
            headerObserved = true;
            for (QAction *option : menu->actions())
                if (option->text() ==
                    owner->model()->headerData(timeColumn, Qt::Horizontal).toString()) {
                    menu->setActiveAction(option);
                    QTest::keyClick(menu, Qt::Key_Return);
                    return;
                }
            QFAIL("source column action missing");
        });
        headerAnswer.start(5);
        QVERIFY(QMetaObject::invokeMethod(owner->horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        QVERIFY(headerObserved);
        QVERIFY(action(window, "settingsAction")->isEnabled());
        QCOMPARE(owner->isColumnHidden(timeColumn), !ownerHidden);
        QCOMPARE(peer->isColumnHidden(peerTimeColumn), peerHidden);
        QVERIFY(peer->sourcePanel->GetColumns().IsEqualTo(peerCatalog));
        const auto &ownerCatalog = owner->sourcePanel->GetColumns();
        FOR_VECTOR(i, ownerCatalog) {
            const CPropColumn &column = ownerCatalog[i];
            if (column.ID == kpidATime)
                QCOMPARE(column.IsVisible, ownerHidden);
        }
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            peer->setFocus();
            for (bool keyboard : {false, true}) {
                bool cancelled = false;
                QTimer cancel;
                connect(&cancel, &QTimer::timeout, &window, [&] {
                    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                    if (!menu || !menu->isVisible())
                        return;
                    cancel.stop();
                    const auto close = qScopeGuard([&] { menu->close(); });
                    cancelled = true;
                    QVERIFY(!window.close());
                    QVERIFY(window.isVisible());
                    auto *open = menuAction(*menu, "openItemAction");
                    auto *rename = menuAction(*menu, "renameAction");
                    auto *create = menuAction(*menu, "createFolderAction");
                    QVERIFY(open && rename && create);
                    QVERIFY(menu->actions().indexOf(open) >= 0);
                    QVERIFY(menu->actions().indexOf(open) < menu->actions().indexOf(rename));
                    auto *more = menu->findChild<QMenu *>("moreCommandsMenu");
                    QVERIFY(more && more->actions().contains(create));
                    const auto hashes = menu->findChildren<QMenu *>("contextHashMenu");
                    QCOMPARE(hashes.size(), 1);
                    QVERIFY(!hashes.first()->actions().isEmpty());
                    const auto archives = menu->findChildren<QMenu *>("contextArchiveMenu");
                    QCOMPARE(archives.size(), 1);
                    const QStringList archiveCaptions =
                        language == "ru"
                            ? QStringList{QStringLiteral("Добавить"), QStringLiteral("Извлечь"),
                                          QStringLiteral("Тестировать")}
                            : QStringList{"Add", "Extract", "Test"};
                    QStringList actualCaptions;
                    for (auto *command : archives.first()->actions())
                        actualCaptions.append(command->text());
                    QCOMPARE(actualCaptions, archiveCaptions);
                    if (!keyboard && width == 980 && pane == 0 && !icons) {
                        for (auto *sub : {hashes.first(), archives.first()}) {
                            const QPoint position = menu->mapToGlobal(
                                menu->actionGeometry(sub->menuAction()).topRight());
                            sub->popup(position);
                            QTRY_VERIFY(sub->isVisible());
                            QVERIFY(UiInspection::writeReport(
                                *sub, QString("source-context-submenu-%1-%2")
                                          .arg(sub->objectName(), language)));
                            sub->hide();
                        }
                    }
                    QVERIFY(
                        UiInspection::writeReport(*menu, QString("source-context-%1-%2-%3-%4-%5")
                                                             .arg(pane)
                                                             .arg(language)
                                                             .arg(icons)
                                                             .arg(width)
                                                             .arg(keyboard)));
                    QTest::keyClick(menu, Qt::Key_Escape);
                });
                cancel.start(5);
                const QPoint point = keyboard
                                         ? QPoint(-1, -1)
                                         : view->visualRect(view->model()->index(0, 0)).center();
                QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested",
                                                  Qt::DirectConnection, Q_ARG(QPoint, point)));
                QVERIFY(cancelled);
                QVERIFY(!QTest::currentTestFailed());
                QVERIFY(action(window, "settingsAction")->isEnabled());
                QCOMPARE(owner->sourcePanel->GetFolder(), (IFolderFolder *)ownerFolder);
                QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
                QVERIFY(!QFileInfo::exists(fixture.filePath(created)));
                QVERIFY(!QFileInfo::exists(peerDirectory.filePath(created)));
            }
        }
        bool menuObserved = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            const auto close = qScopeGuard([&] { menu->close(); });
            menuObserved = true;
            QCOMPARE(menu->objectName(), QString("panelContextMenu"));
            auto *create = menuAction(*menu, "createFolderAction");
            QVERIFY(create);
            auto *more = menu->findChild<QMenu *>("moreCommandsMenu");
            QVERIFY(more && more->actions().contains(create));
            QVERIFY(create->isEnabled());
            menu->setActiveAction(more->menuAction());
            QTest::keyClick(menu, Qt::Key_Right);
            QApplication::processEvents();
            QVERIFY(more->isVisible());
            QVERIFY(UiInspection::writeReport(
                *more, QString("source-context-more-%1-%2-%3").arg(pane).arg(language).arg(icons)));
            more->setActiveAction(create);
            QTest::keyClick(more, Qt::Key_Return);
        });
        const QModelIndex index = view->model()->index(0, 0);
        const QPoint point = view->visualRect(index).center();
        QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, point)));
        QVERIFY(menuObserved);
        auto *name = ownerUi->findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        name->setText(created);
        QTest::keyClick(name, Qt::Key_Return);
        QVERIFY2(
            QFileInfo::exists(fixture.filePath(created)),
            qPrintable(
                QString("owner: %1; peer: %2")
                    .arg(
                        QDir(fixture.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).join('|'))
                    .arg(QDir(peerDirectory.path())
                             .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
                             .join('|'))));
        QVERIFY(!QFileInfo::exists(peerDirectory.filePath(created)));
        QTRY_VERIFY(row(*owner, created) >= 0);
        QCOMPARE(row(*peer, created), -1);
        UInt32 ownerCount = 0, peerCount = 0;
        QCOMPARE(owner->sourcePanel->GetFolder()->GetNumberOfItems(&ownerCount), S_OK);
        QCOMPARE(peer->sourcePanel->GetFolder()->GetNumberOfItems(&peerCount), S_OK);
        QCOMPARE(ownerCount, UInt32(2));
        QCOMPARE(peerCount, UInt32(1));
        QCOMPARE(owner->sourcePanel->GetFolder(), (IFolderFolder *)ownerFolder);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(backend.extractCalls, 0);
        for (const QString &path : {fixture.filePath(original), peerDirectory.filePath(original)}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("protected"));
        }
    }
    void emptyAndParent_data() {
        QTest::addColumn<bool>("showDots");
        QTest::addColumn<QString>("language");
        for (bool showDots : {false, true})
            for (const QString language : {"-", "ru"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(showDots).arg(language)))
                    << showDots << language;
    }
    void emptyAndParent() {
        QFETCH(bool, showDots);
        QFETCH(QString, language);
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
        preferences.ShowDots = showDots;
        preferences.Save(settings);
        QTemporaryDir fixture, peerDirectory;
        QVERIFY(fixture.isValid() && peerDirectory.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *owner = openPanel(window, 0, fixture.path());
        QVERIFY(owner);
        QTRY_COMPARE(owner->rowCount(), showDots ? 1 : 0);
        if (showDots) {
            owner->setCurrentCell(0, 0);
            owner->selectRow(0);
        }
        auto *peer = openPanel(window, 1, peerDirectory.path());
        QVERIFY(peer);
        QTRY_COMPARE(peer->rowCount(), showDots ? 1 : 0);
        QTRY_VERIFY(owner->isEnabled() && peer->isEnabled());
        peer->setFocus();
        bool observed = false;
        QTimer cancel;
        connect(&cancel, &QTimer::timeout, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu || !menu->isVisible())
                return;
            cancel.stop();
            const auto close = qScopeGuard([&] { menu->close(); });
            observed = true;
            auto *rename = menuAction(*menu, "renameAction");
            auto *create = menuAction(*menu, "createFolderAction");
            QVERIFY(rename && create);
            QVERIFY(!rename->isEnabled());
            QVERIFY(create->isEnabled());
            QVERIFY(UiInspection::writeReport(
                *menu, QString("source-context-empty-%1-%2").arg(showDots).arg(language)));
            QTest::keyClick(menu, Qt::Key_Escape);
        });
        cancel.start(5);
        QVERIFY(QMetaObject::invokeMethod(owner, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(-1, -1))));
        QVERIFY(observed);
        QVERIFY(QDir(fixture.path()).entryList(QDir::Files).isEmpty());
        QVERIFY(QDir(peerDirectory.path()).entryList(QDir::Files).isEmpty());
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(backend.extractCalls, 0);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceContextMenuTests)
#include "source_context_menu_tests.moc"
