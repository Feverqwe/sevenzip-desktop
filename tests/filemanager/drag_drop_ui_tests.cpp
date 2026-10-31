#include "archive/support/native_archive_fixture.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/drag_event_position.h"
#include "ui/support/layout_inspection.h"
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragLeaveEvent>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QThread>

namespace {
int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).data().toString().normalized(
                QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
            return row;
    return -1;
}
void Navigate(QWidget &panel, const QString &path) {
    auto *address = panel.findChild<QLineEdit *>("archivePath");
    address->setFocus();
    address->setText(path);
    QTest::keyClick(address, Qt::Key_Return);
}
} // namespace

class DragDropUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void externalMiddleButtonUsesDropMenu() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QFile file(root.filePath(QStringLiteral("middle protected δ.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("middle button control"), qint64(21));
        file.close();
        SavePanelPath(0, ToUString(root.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        auto *panel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(panel);
        Navigate(*panel, root.path());
        QTRY_VERIFY(table->isEnabled() && Row(*table, QFileInfo(file).fileName()) >= 0);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::MiddleButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool menuShown = false;
        QTimer cancel;
        connect(&cancel, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(widget);
                if (menu && menu->objectName() == "dragContextMenu" && menu->isVisible()) {
                    menuShown = true;
                    QVERIFY(UiInspection::writeReport(*menu, "drag-middle-menu"));
                    QTest::keyClick(menu, Qt::Key_Escape);
                }
            }
        });
        cancel.start(1);
        // Buttons are released for Drop. DragEnter must retain the menu decision.
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &drop);
        cancel.stop();
        QVERIFY(menuShown);
        QVERIFY(!drop.isAccepted());
        QVERIFY(!backend.busy());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("middle button control"));
    }

    void archiveSourceCancelsThroughLiveController_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("right");
        QTest::newRow("table") << false << false;
        QTest::newRow("icons") << true << false;
        QTest::newRow("right-gesture") << false << true;
    }
    void archiveSourceCancelsThroughLiveController() {
        QFETCH(bool, icons);
        QFETCH(bool, right);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/SingleClick", 1);
        settings.sync();
        QTemporaryDir root;
        const QString name = QStringLiteral("исходный с пробелом.txt");
        QFile input(root.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("controller bytes"), qint64(16));
        input.close();
        const QString archive = root.filePath("source.zip");
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {input.fileName()}));
        QFile original(archive);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = original.readAll();
        original.close();
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *table = dynamic_cast<FileTable *>(window.findChild<FileTable *>("archiveTable"));
        QVERIFY(table && table->dragStarter);
        QTRY_VERIFY(table->isEnabled() && Row(*table, name) >= 0);
        if (icons)
            window.findChild<QAction *>("viewMode0Action")->trigger();
        table->selectRow(Row(*table, name));
        table->setCurrentCell(Row(*table, name), 0);
        CMyComPtr<IFolderFolder> provider = table->sourcePanel->GetFolder();
        finished.clear();
        QSet<QString> previousRoots;
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            previousRoots.insert(entry.path);
        QStringList dragRoots;
        bool cancelledNativeDrag = false;
        QTimer cancelNativeDrag;
        const bool xcb = QGuiApplication::platformName() == QStringLiteral("xcb");
        const int outerLoop = QThread::currentThread()->loopLevel();
        connect(&cancelNativeDrag, &QTimer::timeout, &window, [&] {
            // The offscreen plugin returns IgnoreAction itself. A real XCB
            // drag waits for a drop/cancel event; cancel only its entered loop,
            // after archive extraction and publication of the owned QDrag.
            if (window.findChild<QDrag *>() && QThread::currentThread()->loopLevel() > outerLoop) {
                cancelledNativeDrag = true;
                cancelNativeDrag.stop();
                QDrag::cancel();
            }
        });
        if (xcb)
            cancelNativeDrag.start(5);
        if (right) {
            QWidget *viewport = table->viewport();
            const QPoint point =
                table->visualRect(table->model()->index(Row(*table, name), 0)).center();
            const QPoint global = viewport->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::RightButton,
                              Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &press);
            QContextMenuEvent context(QContextMenuEvent::Mouse, point, global);
            QCoreApplication::sendEvent(viewport, &context);
            QVERIFY(!QApplication::activePopupWidget());
            const QPoint moved = point + QPoint(QApplication::startDragDistance() + 1, 0);
            QMouseEvent move(QEvent::MouseMove, moved, viewport->mapToGlobal(moved), Qt::NoButton,
                             Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, moved, viewport->mapToGlobal(moved),
                                Qt::RightButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &release);
        } else if (icons) {
            auto *list = dynamic_cast<FileIconList *>(window.findChild<QListView *>("iconList"));
            QVERIFY(list && list->isVisible() && list->dragEnabled() && list->dragStarter);
            list->dragStarter();
        } else
            table->dragStarter();
        cancelNativeDrag.stop();
        if (xcb && !right)
            QVERIFY(cancelledNativeDrag);
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            if (!previousRoots.contains(entry.path) &&
                QFileInfo::exists(QDir(entry.path).filePath(name)))
                dragRoots.append(entry.path);
        const bool nativeOle = QGuiApplication::platformName() == QStringLiteral("windows");
        if (right || nativeOle)
            QVERIFY(dragRoots.isEmpty());
#ifdef Q_OS_MACOS
        else
            QVERIFY(!dragRoots.isEmpty());
#endif
        QVERIFY(window.isVisible());
        QVERIFY(!backend.busy());
        QCOMPARE(table->sourcePanel->GetFolder(), provider.Interface());
        QTRY_VERIFY(Row(*table, name) >= 0 &&
                    PanelIndexSelected(table->model()->index(Row(*table, name), 0)));
        for (const QString &path : dragRoots) {
#ifdef Q_OS_MACOS
            // Default Cocoa IgnoreAction does not confirm that Finder stopped
            // reading. The live controller must keep these exposed bytes safe.
            QFile delivered(QDir(path).filePath(name));
            QVERIFY(delivered.open(QIODevice::ReadOnly));
            QCOMPARE(delivered.readAll(), QByteArray("controller bytes"));
            delivered.close();
            QtOwnedTempRegistry registry;
            bool found = false;
            for (const auto &entry : registry.Entries()) {
                if (entry.path != path)
                    continue;
                found = true;
                QVERIFY(entry.externallyProtected && !entry.active);
                QVERIFY(!registry.ClaimForDeletion(entry));
            }
            QVERIFY(found);
            QVERIFY(QDir(path).removeRecursively()); // Only this fixture's extracted root.
#else
            QVERIFY(!QFileInfo::exists(path));
#endif
        }
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes);
    }

    void externalDragSuspendsHoverWithoutOpeningFolder_data() {
        QTest::addColumn<bool>("rejectFirst");
        QTest::newRow("accepted") << false;
        QTest::newRow("rejected-then-accepted") << true;
    }
    void externalDragSuspendsHoverWithoutOpeningFolder() {
        QFETCH(bool, rejectFirst);
        QTemporaryDir root;
        const QString folder = QStringLiteral("назначение");
        QVERIFY(QDir().mkpath(root.filePath(folder)));
        QFile file(root.filePath("selected.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/SingleClick", 1);
        settings.sync();
        SavePanelPath(0, ToUString(root.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        Navigate(*window.findChild<QWidget *>("filePanel0"), root.path());
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled() && Row(*table, folder) >= 0 &&
                    Row(*table, "selected.txt") >= 0);
        const int selected = Row(*table, "selected.txt");
        table->selectRow(selected);
        table->setCurrentCell(selected, 0);
        const QModelIndex destination = table->model()->index(Row(*table, folder), 0);
        const QPoint point = table->visualRect(destination).center();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        if (rejectFirst) {
            QDragEnterEvent rejected(DragEventPosition(point), Qt::LinkAction, &mime,
                                     Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(table->viewport(), &rejected);
            QVERIFY(!rejected.isAccepted());
            QVERIFY(!destination.data(Qt::UserRole + 19).toBool());
            // A rejected entry has no matching DragLeave. Ordinary hover must
            // resume, and the next supported drag must still be accepted.
            QTest::mouseMove(table->viewport(), point);
            QTRY_COMPARE(table->currentRow(), destination.row());
            table->selectRow(selected);
            table->setCurrentCell(selected, 0);
        }
        QDragEnterEvent enter(DragEventPosition(point), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(destination.data(Qt::UserRole + 19).toBool());
        QTest::mouseMove(table->viewport(), point);
        QTest::qWait(450);
        QCOMPARE(table->currentRow(), selected);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(table->viewport(), &leave);
        QVERIFY(!destination.data(Qt::UserRole + 19).toBool());
        QTest::mouseMove(table->viewport(), point + QPoint(1, 0));
        QTRY_COMPARE(table->currentRow(), destination.row());
        QCOMPARE(
            QFileInfo(window.findChild<QLineEdit *>("archivePath")->text()).canonicalFilePath(),
            QFileInfo(root.path()).canonicalFilePath());
    }

    void externalFilesUseArchiveConfirmation_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<int>("answer");
        QTest::newRow("table-yes") << false << int(QMessageBox::Yes);
        QTest::newRow("icons-yes") << true << int(QMessageBox::Yes);
        QTest::newRow("table-no") << false << int(QMessageBox::No);
        QTest::newRow("icons-cancel") << true << int(QMessageBox::Cancel);
    }
    void externalFilesUseArchiveConfirmation() {
        QFETCH(bool, icons);
        QFETCH(int, answer);
        QTemporaryDir root;
        const QString seed = root.filePath("original.txt");
        const QString source = root.filePath(QStringLiteral("добавлен с пробелом.txt"));
        for (const QString &path : {seed, source}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("archive UI bytes"), qint64(16));
        }
        const QString archive = root.filePath("target.zip");
        ArchiveBackend backend;
        QSignalSpy created(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {seed}));
        QFile before(archive);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled() && Row(*table, "original.txt") >= 0);
        QVERIFY(table->dragEnabled());
        if (icons) {
            auto *action = window.findChild<QAction *>("viewMode0Action");
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
        }
        auto *iconList = window.findChild<QListView *>("iconList");
        QAbstractItemView *view = icons ? static_cast<QAbstractItemView *>(iconList) : table;
        QVERIFY(view->isVisible());
        QVERIFY(view->dragEnabled());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source)});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool questionShown = false;
        QString unexpectedError;
        QTimer respond;
        respond.setInterval(5);
        connect(&respond, &QTimer::timeout, &window, [&] {
            if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *button = question->button(QMessageBox::StandardButton(answer))) {
                    questionShown = true;
                    button->click();
                } else if (auto *button = question->button(QMessageBox::Ok)) {
                    unexpectedError = question->text();
                    button->click();
                }
            }
        });
        respond.start();
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &drop);
        respond.stop();
        QVERIFY2(unexpectedError.isEmpty(), qPrintable(unexpectedError));
        QVERIFY(questionShown);
        const bool success = answer == QMessageBox::Yes;
        QCOMPARE(drop.isAccepted(), success);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!backend.busy());
        if (!success) {
            QVERIFY(before.open(QIODevice::ReadOnly));
            QCOMPARE(before.readAll(), original);
            return;
        }
        QTRY_VERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
        const QString destination = root.filePath("extracted");
        QVERIFY(QDir().mkpath(destination));
        ArchiveBackend verifier;
        QSignalSpy extracted(&verifier, &ArchiveBackend::finished);
        verifier.list(archive);
        QVERIFY(extracted.wait(30000));
        QVERIFY(extracted.takeFirst().first().toBool());
        verifier.extract(archive, destination, {QFileInfo(source).fileName()});
        QVERIFY(extracted.wait(30000));
        QVERIFY(extracted.takeFirst().first().toBool());
        QFile result(QDir(destination).filePath(QFileInfo(source).fileName()));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("archive UI bytes"));
    }

    void nativeSourceUsesActualTargetView_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("move");
        QTest::addColumn<int>("sourceIndex");
        QTest::addColumn<bool>("right");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("left-table-copy") << false << false << 0 << false << false;
        QTest::newRow("right-table-move") << false << true << 1 << false << false;
        QTest::newRow("left-icons-move") << true << true << 0 << false << false;
        QTest::newRow("right-icons-copy") << true << false << 1 << false << false;
        QTest::newRow("right-button-copy") << false << false << 0 << true << false;
        QTest::newRow("right-button-cancel") << true << false << 1 << true << true;
    }
    void nativeSourceUsesActualTargetView() {
        QFETCH(bool, icons);
        QFETCH(bool, move);
        QFETCH(int, sourceIndex);
        QFETCH(bool, right);
        QFETCH(bool, cancel);
        QTemporaryDir root;
        const QString name = QStringLiteral("перенос с пробелом.txt");
        const QString source = root.filePath("source");
        const QString target = root.filePath("target");
        const QString subfolder = QStringLiteral("назначение с пробелом");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(QDir(target).filePath(subfolder)));
        QFile input(QDir(source).filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("UI drag bytes"), qint64(13));
        input.close();
        SavePanelPath(sourceIndex, ToUString(source),
                      qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SavePanelPath(1 - sourceIndex, ToUString(target),
                      qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        auto *two = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(two->isEnabled());
        if (!two->isChecked())
            two->trigger();
        auto *sourcePanel = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *targetPanel =
            window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourceIndex));
        QVERIFY(sourcePanel && targetPanel && targetPanel->isVisible());
        auto *sourceTable =
            dynamic_cast<FileTable *>(sourcePanel->findChild<FileTable *>("archiveTable"));
        auto *targetTable = targetPanel->findChild<FileTable *>("archiveTable");
        QVERIFY(sourceTable && sourceTable->dragStarter && targetTable);
        Navigate(*targetPanel, target);
        QTRY_VERIFY(targetTable->isEnabled() && Row(*targetTable, subfolder) >= 0);
        if (icons)
            window.findChild<QAction *>("viewMode0Action")->trigger();
        Navigate(*sourcePanel, source);
        QTRY_VERIFY(sourceTable->isEnabled() && Row(*sourceTable, name) >= 0);
        QVERIFY(sourceTable->sourcePanel);
        const int sourceRow = Row(*sourceTable, name);
        sourceTable->selectRow(sourceRow);
        sourceTable->setCurrentCell(sourceRow, 0);
        sourceTable->setFocus();
        QCoreApplication::processEvents();
        auto *targetIcons = targetPanel->findChild<QListView *>("iconList");
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(targetIcons) : targetTable;
        QVERIFY(view && view->isVisible());
        const int folderRow = Row(*targetTable, subfolder);
        const QModelIndex folder = targetTable->model()->index(folderRow, 0);
        const QPoint point = view->visualRect(folder).center();
        QVERIFY(view->viewport()->rect().contains(point));
        const auto selection = targetTable->selectionModel()->selectedRows();
        bool entered = false, dropped = false, highlighted = false, sourceRefused = false;
        bool menuShown = false;
        QTimer chooseMenu;
        chooseMenu.setInterval(5);
        connect(&chooseMenu, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(widget);
                if (!menu || menu->objectName() != "dragContextMenu" || !menu->isVisible())
                    continue;
                const UInt32 command =
                    cancel ? UInt32(NDragMenu::k_Cancel)
                           : UInt32(NDragMenu::k_Copy_Base | NDragMenu::k_MenuFlag_Copy);
                for (QAction *action : menu->actions()) {
                    if (!action->isSeparator() && action->data().toUInt() == command) {
                        menuShown = true;
                        menu->setActiveAction(action);
                        QTest::keyClick(menu, Qt::Key_Return);
                        return;
                    }
                }
            }
        });
        if (right)
            chooseMenu.start();
        QtPanelDragTransport transport(sourceTable, [&](QDrag &drag, Qt::DropActions) {
            const auto modifiers = move ? Qt::ShiftModifier : Qt::ControlModifier;
            const auto button = right ? Qt::RightButton : Qt::LeftButton;
            QDragEnterEvent self(DragEventPosition(QPoint(5, 5)), Qt::CopyAction | Qt::MoveAction,
                                 drag.mimeData(), button, modifiers);
            QCoreApplication::sendEvent(sourceTable->viewport(), &self);
            sourceRefused = !self.isAccepted();
            QDragLeaveEvent leave;
            QCoreApplication::sendEvent(sourceTable->viewport(), &leave);
            QDragEnterEvent enter(DragEventPosition(point), Qt::CopyAction | Qt::MoveAction,
                                  drag.mimeData(), button, modifiers);
            QCoreApplication::sendEvent(view->viewport(), &enter);
            entered = enter.isAccepted();
            highlighted = folder.data(Qt::UserRole + 19).toBool();
            QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, drag.mimeData(), button,
                            modifiers);
            QCoreApplication::sendEvent(view->viewport(), &drop);
            dropped = drop.isAccepted();
            return dropped ? drop.dropAction() : Qt::IgnoreAction;
        });
        sourceTable->sourcePanel->AttachDragServices(&transport);
        sourceTable->sourcePanel->OnDrag(right);
        sourceTable->sourcePanel->AttachDragServices(nullptr);
        chooseMenu.stop();
        QVERIFY(sourceRefused);
        QVERIFY(entered && highlighted);
        QCOMPARE(dropped, !cancel);
        QCOMPARE(menuShown, right);
        QVERIFY(!folder.data(Qt::UserRole + 19).toBool());
        QCOMPARE(targetTable->selectionModel()->selectedRows(), selection);
        if (cancel) {
            QVERIFY(PanelIndexSelected(sourceTable->model()->index(sourceRow, 0)));
            QVERIFY(QFileInfo::exists(input.fileName()));
            QVERIFY(!QFileInfo::exists(QDir(QDir(target).filePath(subfolder)).filePath(name)));
            return;
        }
        QFile result(QDir(QDir(target).filePath(subfolder)).filePath(name));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("UI drag bytes"));
        QCOMPARE(QFileInfo::exists(input.fileName()), !move);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("drag-%1-%2-%3x%4")
                                                          .arg(icons ? "icons" : "table")
                                                          .arg(sourceIndex)
                                                          .arg(size.width())
                                                          .arg(size.height())));
        }
    }
};
QTEST_MAIN(DragDropUiTests)
#include "drag_drop_ui_tests.moc"
