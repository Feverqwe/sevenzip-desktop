#include "filemanager/providers/NetFolder.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtplaceprojection.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspacesidebar.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QListView>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

class WorkspacePlacesTests final : public QObject {
    Q_OBJECT
    static QColor rowFill(QListView &view, const QModelIndex &index) {
        const QRect area = view.visualRect(index);
        const QImage image = view.viewport()->grab().toImage();
        return image.pixelColor(qRound((area.left() + 4) * image.devicePixelRatio()),
                                qRound(area.center().y() * image.devicePixelRatio()));
    }
  private slots:
    void retryMissingFavoriteKeepsItsRequestedTarget() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString missing = fixture.filePath("missing папка");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.findChild<QAction *>("openRootAction")->trigger();
        QtFavoriteStore *store = nullptr;
        for (auto *object : qApp->children())
            if (auto *favorites = dynamic_cast<QtFavoriteStore *>(object))
                store = favorites;
        QVERIFY(store);
        store->Add(missing, "Unavailable target");
        auto *list = window.findChild<QListView *>("favoritePlaces0");
        QVERIFY(list);
        QModelIndex target;
        for (int row = 0; row < list->model()->rowCount(); ++row)
            if (list->model()->index(row, 0).data(QtPlaceRoles::Path).toString() == missing)
                target = list->model()->index(row, 0);
        QVERIFY(target.isValid());
        list->scrollTo(target);
        QApplication::processEvents();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualRect(target).center());
        const auto errorVisible = [&] {
            for (auto *label : window.findChildren<QLabel *>())
                if (label->isVisible() && label->text().contains(missing))
                    return true;
            return false;
        };
        QTRY_VERIFY(errorVisible());
        QPushButton *retry = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->isVisible() &&
                button->text() == QCoreApplication::translate("QtWindowWorkspace", "Retry"))
                retry = button;
        QVERIFY(retry);
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_VERIFY(errorVisible());
        QVERIFY(!window.findChild<QAction *>("createFolderAction")->isEnabled());
    }
    void computerMenuAndSidebarUseSameNamespace_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void computerMenuAndSidebarUseSameNamespace() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(files->isEnabled());
        auto *create = window.findChild<QAction *>("createFolderAction");
        QTRY_VERIFY(create->isEnabled());
        auto *places = window.findChild<QListView *>("nativePlaces");
        QModelIndex computer;
        int computerRows = 0;
        for (int row = 0; row < places->model()->rowCount(); ++row)
            if (places->model()->index(row, 0).data(QtPlaceRoles::Identity).toString() ==
                "computer") {
                computer = places->model()->index(row, 0);
                ++computerRows;
            }
        QCOMPARE(computerRows, 1);
        QVERIFY(computer.isValid());
        QTest::mouseClick(places->viewport(), Qt::LeftButton, Qt::NoModifier,
                          places->visualRect(computer).center());
        QTRY_VERIFY(files->isEnabled());
        QVERIFY(!create->isEnabled());
        QVERIFY(files->sourcePanel->IsFSDrivesFolder());
        QVERIFY(computer.data(QtPlaceRoles::CurrentLocation).toBool());
        const QString sidebarTarget = ToQString(files->sourcePanel->GetCurrentFolderPrefix());
        window.openPath(fixture.path());
        QTRY_VERIFY(create->isEnabled());
        window.findChild<QAction *>("openRootAction")->trigger();
        QTRY_VERIFY(files->isEnabled());
        QCOMPARE(ToQString(files->sourcePanel->GetCurrentFolderPrefix()), sidebarTarget);
        QVERIFY(!create->isEnabled());
        QVERIFY(computer.data(QtPlaceRoles::CurrentLocation).toBool());
        window.findChild<QAction *>("parentFolderAction")->trigger();
        QTRY_VERIFY(files->sourcePanel->IsRootFolder());
        int icon = 0;
        const QString name = ToQString(RootFolder_GetName_Computer(icon));
        int rootComputer = -1;
        for (int row = 0; row < files->rowCount(); ++row)
            if (files->model()->index(row, 0).data().toString() == name)
                rootComputer = row;
        QVERIFY(rootComputer >= 0);
        files->selectRow(rootComputer);
        files->setCurrentCell(rootComputer, 0, QItemSelectionModel::NoUpdate);
        window.findChild<QAction *>("openItemAction")->trigger();
        QTRY_VERIFY(files->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(ToQString(files->sourcePanel->GetCurrentFolderPrefix()), sidebarTarget);
        QVERIFY(!create->isEnabled());
    }
    void savedPlacesMenuResolvesCurrentBookmark_data() {
        QTest::addColumn<bool>("remove");
        QTest::newRow("reordered") << false;
        QTest::newRow("removed") << true;
    }
    void savedPlacesMenuResolvesCurrentBookmark() {
        QFETCH(bool, remove);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray previous = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previous);
        });
        const QString settings = fixture.filePath("favorites.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        QSettings(settings, QSettings::IniFormat).setValue("workspace/favorites", "[]");
        const QString a = fixture.filePath("A folder");
        const QString b = fixture.filePath("B папка");
        QVERIFY(QDir().mkpath(a));
        QVERIFY(QDir().mkpath(b));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QtFavoriteStore *favorites = nullptr;
        for (auto *object : qApp->children())
            if (auto *store = dynamic_cast<QtFavoriteStore *>(object);
                store && store->property("settingsPath").toString() == settings)
                favorites = store;
        QVERIFY(favorites);
        favorites->Add(a, "A");
        favorites->Add(b, "B");
        const QString id = favorites->Favorite(1).id;
        QtWindowWorkspace *owner = nullptr;
        for (auto *object : window.children())
            if (auto *workspace = dynamic_cast<QtWindowWorkspace *>(object))
                owner = workspace;
        QVERIFY(owner);
        window.openPath(a);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), a);
        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu->isVisible());
        QAction *selected = nullptr;
        for (auto *action : menu->actions())
            if (action->text() == "B")
                selected = action;
        QVERIFY(selected);
        if (remove)
            QVERIFY(favorites->removeRow(favorites->IndexForId(id).row()));
        else
            QVERIFY(favorites->moveRow({}, 1, {}, 0));
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(selected).center());
        QTRY_VERIFY(!menu->isVisible());
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), remove ? a : b);
        QVERIFY(QFileInfo(b).isDir());
    }
    void compactPlacesRemainReachable_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void compactPlacesRemainReachable() {
        QFETCH(QString, language);
        QTemporaryDir fixture;
        const auto settings = fixture.filePath("places.ini");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QtFavoriteStore favorites(settings);
        QtNativePlaces nativePlaces;
        for (int index = 0; index < 8; ++index)
            favorites.Add(fixture.filePath(QString::number(index)),
                          QString("Очень длинное имя избранного — Project %1").arg(index));
        QString opened;
        bool addInvoked = false;
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *sidebar = new QtPlacesSidebar(favorites, nativePlaces,
                                            {[&] { addInvoked = true; },
                                             [&](const QString &path, QtPlaceTarget target) {
                                                 QCOMPARE(target, QtPlaceTarget::Current);
                                                 opened = path;
                                             }},
                                            0, &window);
        sidebar->SetCompact(true);
        sidebar->SetCurrentLocation(fixture.path());
        layout->addWidget(sidebar);
        window.resize(140, 230);
        window.show();
        auto *add = sidebar->findChild<QToolButton *>("addFavoriteButton");
        auto *locations = sidebar->findChild<QListView *>("nativePlaces");
        QVERIFY(add && locations && locations->model()->rowCount());
        QTRY_VERIFY(UiInspection::fullyVisible(*add, window));
        QTest::mouseClick(add, Qt::LeftButton);
        QVERIFY(addInvoked);
        QTRY_VERIFY(sidebar->verticalScrollBar()->maximum() > 0);
        sidebar->verticalScrollBar()->setValue(sidebar->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const auto last = locations->model()->index(locations->model()->rowCount() - 1, 0);
        const QRect row = locations->visualRect(last);
        const QRect exposed(locations->viewport()->mapTo(sidebar->viewport(), row.topLeft()),
                            row.size());
        QVERIFY(sidebar->viewport()->rect().contains(exposed));
        QTest::mouseClick(locations->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
        QCOMPARE(opened, last.data(Qt::UserRole + 1).toString());
        QVERIFY(!opened.isEmpty());
        QVERIFY(
            UiInspection::writeReport(window, QString("workspace-places-%1-bottom").arg(language)));
    }

    void currentPlaceStaysIndependentOfKeyboardCursorAndOtherWindow() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("current.ini");
        QSettings(settings, QSettings::IniFormat).setValue("workspace/favorites", "[]");
        QtFavoriteStore favorites(settings);
        const QString first = fixture.filePath("Project");
        const QString second = fixture.filePath("Other");
        favorites.Add(first, "Project");
        favorites.Add(second, "Another place with a long title");
        QtNativePlaces places;
        QWidget firstWindow, secondWindow;
        auto *layout = new QVBoxLayout(&firstWindow);
        layout->setContentsMargins(0, 0, 0, 0);
        int opened = 0;
        QString target;
        auto *sidebar = new QtPlacesSidebar(favorites, places,
                                            {[] {},
                                             [&](const QString &path, QtPlaceTarget) {
                                                 ++opened;
                                                 target = path;
                                             }},
                                            0);
        layout->addWidget(sidebar);
        auto *otherLayout = new QVBoxLayout(&secondWindow);
        auto *other = new QtPlacesSidebar(favorites, places,
                                          {[] {}, [](const QString &, QtPlaceTarget) {}}, 0);
        otherLayout->addWidget(other);
        sidebar->SetCurrentLocation(first);
        other->SetCurrentLocation(second);
        firstWindow.resize(190, 480);
        secondWindow.show();
        firstWindow.show();
        auto *view = sidebar->findChild<QListView *>("favoritePlaces0");
        auto *peer = other->findChild<QListView *>("favoritePlaces0");
        const QModelIndex active = view->model()->index(0, 0);
        const QModelIndex next = view->model()->index(1, 0);
        QVERIFY(active.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(!next.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(peer->model()->index(1, 0).data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(WindowActivation::waitForActive(firstWindow));
        view->setFocus();
        view->setCurrentIndex(active);
        QTRY_VERIFY(view->hasFocus());
        const QColor currentFill = rowFill(*view, active);
        QTest::keyClick(view, Qt::Key_Down);
        QCOMPARE(view->currentIndex(), next);
        QCOMPARE(opened, 0);
        QCOMPARE(rowFill(*view, active), currentFill);
        QVERIFY(active.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(!next.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(UiInspection::writeReport(firstWindow, "places-current-and-keyboard"));
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(opened, 1);
        QCOMPARE(target, second);
        // Navigation publishes success separately; a request alone changes no active place.
        QVERIFY(active.data(QtPlaceRoles::CurrentLocation).toBool());
        sidebar->SetCurrentLocation(second);
        QVERIFY(next.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(!active.data(QtPlaceRoles::CurrentLocation).toBool());
        other->SetCurrentLocation(first);
        QVERIFY(next.data(QtPlaceRoles::CurrentLocation).toBool());
        QVERIFY(peer->model()->index(0, 0).data(QtPlaceRoles::CurrentLocation).toBool());
    }

    void favoriteMenuResolvesStableIdentity_data() {
        QTest::addColumn<bool>("rename");
        QTest::addColumn<bool>("removeBeforeAction");
        QTest::newRow("remove-reordered") << false << false;
        QTest::newRow("remove-already-removed") << false << true;
        QTest::newRow("rename-reordered") << true << false;
    }
    void favoriteMenuResolvesStableIdentity() {
        QFETCH(bool, rename);
        QFETCH(bool, removeBeforeAction);
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("menu.ini");
        QSettings(settings, QSettings::IniFormat).setValue("workspace/favorites", "[]");
        QtFavoriteStore favorites(settings);
        for (const QString &name : {QString("A"), QString("B"), QString("C")}) {
            QVERIFY(QDir().mkpath(fixture.filePath(name)));
            favorites.Add(fixture.filePath(name), name);
        }
        const QString identity = favorites.Favorite(1).id;
        QtNativePlaces nativePlaces;
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *sidebar = new QtPlacesSidebar(favorites, nativePlaces,
                                            {[] {}, [](const QString &, QtPlaceTarget) {}}, 0);
        layout->addWidget(sidebar);
        sidebar->SetCurrentLocation(fixture.filePath("A"));
        window.resize(190, 480);
        window.show();
        auto *view = sidebar->findChild<QListView *>("favoritePlaces0");
        bool invoked = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto close = qScopeGuard([menu] { menu->close(); });
            const std::unique_ptr<QMimeData> drag(
                view->model()->mimeData({view->model()->index(0, 0)}));
            QVERIFY(view->model()->dropMimeData(drag.get(), Qt::MoveAction, 3, 0, {}));
            if (removeBeforeAction)
                QVERIFY(favorites.removeRow(favorites.IndexForId(identity).row()));
            const QString label = QCoreApplication::translate(
                "QtWindowWorkspace", rename ? "Rename favorite" : "Remove favorite");
            for (auto *action : menu->actions())
                if (action->text() == label) {
                    invoked = true;
                    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                      menu->actionGeometry(action).center());
                    break;
                }
            menu->close();
        });
        const QPoint position = view->visualRect(view->model()->index(1, 0)).center();
        QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, position)));
        QVERIFY(invoked);
        if (rename) {
            QPointer<QLineEdit> editor;
            QTRY_VERIFY((editor = view->findChild<QLineEdit *>()));
            QTRY_VERIFY(editor && editor->isVisible());
            editor->setText("Renamed B");
            QTest::keyClick(editor, Qt::Key_Return);
            QTRY_COMPARE(favorites.data(favorites.IndexForId(identity), Qt::DisplayRole).toString(),
                         QString("Renamed B"));
        } else {
            QVERIFY(!favorites.IndexForId(identity).isValid());
            QCOMPARE(favorites.rowCount(), 2);
        }
        for (const QString &name : {QString("A"), QString("B"), QString("C")})
            QVERIFY(QFileInfo(fixture.filePath(name)).isDir());
        bool keptA = false, keptC = false;
        for (int row = 0; row < favorites.rowCount(); ++row) {
            keptA |= favorites.Favorite(row).label == "A";
            keptC |= favorites.Favorite(row).label == "C";
        }
        QVERIFY(keptA && keptC);
    }

    void nativeRootCurrentSurvivesLanguageChange() {
#ifdef Q_OS_WIN
        QSKIP("The deterministic network-root fixture uses the POSIX mount provider");
#else
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        SaveRegLang(L"-", settings);
        ReloadLang(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *files = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(files->isEnabled());
        auto *network = new CNetFolder([] { return QList<QtVolumeMount>{}; });
        CMyComPtr<IFolderFolder> source = network;
        int icon = 0;
        const UString path = RootFolder_GetName_Network(icon) + WCHAR_PATH_SEPARATOR;
        network->Init(path);
        files->sourcePanel->SetNewFolder(source);
        files->sourcePanel->LoadFullPath();
        QCOMPARE(files->sourcePanel->RefreshListCtrl(), S_OK);
        auto *view = window.findChild<QListView *>("nativePlaces");
        QModelIndex current;
        for (int row = 0; row < view->model()->rowCount(); ++row)
            if (view->model()->index(row, 0).data(QtPlaceRoles::CurrentLocation).toBool())
                current = view->model()->index(row, 0);
        QVERIFY(current.isValid());
        const QPersistentModelIndex root = current;
        const QString oldTarget = root.data(QtPlaceRoles::Path).toString();
        SaveRegLang(L"ru", settings);
        ReloadLang(settings);
        QEvent language(QEvent::LanguageChange);
        QApplication::sendEvent(&window, &language);
        QTRY_VERIFY(root.data(QtPlaceRoles::Path).toString() != oldTarget);
        QVERIFY(root.isValid());
        QVERIFY(root.data(QtPlaceRoles::CurrentLocation).toBool());
        QCOMPARE(files->sourcePanel->GetCurrentFolderPrefix(), path);
#endif
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspacePlacesTests)
#include "workspace_places_tests.moc"
