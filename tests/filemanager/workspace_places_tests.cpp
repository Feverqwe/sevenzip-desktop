#include "filemanager/providers/NetFolder.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "fileops/scanner/filesystemscanner.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QListView>
#include <QMenu>
#include <QMessageBox>
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
        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu->isVisible());
        auto *target = [&]() -> QAction * {
            for (auto *action : menu->actions())
                if (action->toolTip() == missing)
                    return action;
            return nullptr;
        }();
        QVERIFY(target);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(target).center());
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
        QVERIFY(!QSettings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat)
                     .value("FolderHistory")
                     .toStringList()
                     .contains(missing));
        QVERIFY(QDir().mkpath(missing));
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QAction *>("createFolderAction")->isEnabled());
        auto *files = window.findChild<FileTable *>("archiveTable");
        QCOMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())), missing);
        window.findChild<QAction *>("backAction")->trigger();
        QTRY_VERIFY(files->sourcePanel->IsFSDrivesFolder());
        window.findChild<QAction *>("forwardAction")->trigger();
        QTRY_COMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())),
                     missing);
    }
    void favoriteKeepsRetryTargetAfterBackgroundReadFailure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString origin = fixture.filePath("Original");
        const QString target = fixture.filePath("Large папка");
        QVERIFY(QDir().mkpath(origin));
        QVERIFY(QDir().mkpath(target));
        for (int i = 0; i < 301; ++i) {
            QFile file(QDir(target).filePath(QString::number(i) + ".txt"));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(origin);
        auto *files = window.findChild<FileTable *>("archiveTable");
        auto *scanner = window.findChild<FilesystemScanner *>();
        auto *retry = window.findChild<QPushButton *>("navigationRetry0");
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(files && scanner && retry && address);
        QTRY_VERIFY(files->isEnabled());
        QtFavoriteStore *store = nullptr;
        for (auto *object : qApp->children())
            if (auto *favorites = dynamic_cast<QtFavoriteStore *>(object))
                store = favorites;
        QVERIFY(store);
        store->Add(target, QStringLiteral("Large favorite"));

        // Replace the pending scan at its public progress boundary with a
        // real read failure. This avoids timing and filesystem permissions.
        const auto restart = connect(
            scanner, &FilesystemScanner::advanced, &window,
            [&] { scanner->start(fixture.filePath("Missing"), false); }, Qt::SingleShotConnection);
        QSignalSpy failed(scanner, &FilesystemScanner::failed);
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                message->accept();
        });
        dismiss.start(1);
        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu->isVisible());
        QAction *entry = nullptr;
        for (auto *item : menu->actions())
            if (item->toolTip() == target)
                entry = item;
        QVERIFY(entry);
        menu->setActiveAction(entry);
        QTest::keyClick(menu, Qt::Key_Return);
        QTRY_COMPARE(failed.size(), 1);
        QTRY_VERIFY(retry->isVisible());
        QVERIFY(!files->isEnabled());
        QCOMPARE(files->rowCount(), 0);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())), target);
        disconnect(restart);
        retry->click();
        QTRY_VERIFY(files->isEnabled() && !retry->isVisible());
        QCOMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())), target);
        QVERIFY(files->rowCount() >= 301);
        window.findChild<QAction *>("backAction")->trigger();
        QTRY_COMPARE(QDir::cleanPath(ToQString(files->sourcePanel->GetCurrentFolderPrefix())),
                     origin);
    }
    void computerMenuAndRootRowUseSameNamespace_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void computerMenuAndRootRowUseSameNamespace() {
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
        window.findChild<QAction *>("openRootAction")->trigger();
        QTRY_VERIFY(files->isEnabled());
        QVERIFY(files->sourcePanel->IsFSDrivesFolder());
        QVERIFY(!create->isEnabled());
        const QString computerPath = ToQString(files->sourcePanel->GetCurrentFolderPrefix());
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
        QCOMPARE(ToQString(files->sourcePanel->GetCurrentFolderPrefix()), computerPath);
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
        const QString label = QStringLiteral("B & C\tD");
        favorites->Add(b, label);
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
            if (action->toolTip() == b)
                selected = action;
        QVERIFY(selected);
        QCOMPARE(selected->text(), QStringLiteral("B && C D"));
        QVERIFY(QKeySequence::mnemonic(selected->text()).isEmpty());
        QCOMPARE(favorites->Favorite(1).label, label);
        if (!remove)
            QVERIFY(UiInspection::writeReport(*menu, "saved-places-literal-label"));
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
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspacePlacesTests)
#include "workspace_places_tests.moc"
