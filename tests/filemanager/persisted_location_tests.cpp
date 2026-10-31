#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "ui/support/layout_inspection.h"
#include <QTreeWidget>

class PersistedLocationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *leftTable(MainWindow &window) {
        return static_cast<FileTable *>(
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable"));
    }
    static QString prefix(MainWindow &window) {
        return ToQString(leftTable(window)->sourcePanel->GetCurrentFolderPrefix());
    }
    static QString saved(unsigned index) {
        CFastFolders folders;
        folders.Read(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        return ToQString(folders.GetString(index));
    }
    static void openEmptyHistoryItem(MainWindow &window, const QString &phase) {
        QTimer::singleShot(0, &window, [&window, phase] {
            auto *dialog = window.findChild<QDialog *>("foldersHistoryDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *tree = dialog->findChild<QTreeWidget *>("foldersHistoryList");
            QVERIFY(tree);
            const auto items = tree->findItems(QString(), Qt::MatchExactly);
            QCOMPARE(items.size(), 1);
            tree->setCurrentItem(items.front());
            for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
                dialog->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*tree, *dialog));
                QVERIFY(UiInspection::writeReport(*dialog, QString("root-history-%1-%2-%3")
                                                               .arg(QTest::currentDataTag(), phase)
                                                               .arg(size.width())));
            }
            dialog->accept();
        });
        action(window, "foldersHistoryAction")->trigger();
    }
  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void historyKeepsOriginatingPanel_data() {
        QTest::addColumn<unsigned>("owner");
        QTest::newRow("left") << 0u;
        QTest::newRow("right") << 1u;
    }
    void historyKeepsOriginatingPanel() {
        QFETCH(unsigned, owner);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath(QStringLiteral("история 密碼"));
        QVERIFY(QDir().mkpath(target));
        QVERIFY(ExternalEditorFixture::write(target + "/target.txt", "target"));
        CFolderHistory history;
        history.AddString(ToUString(target));
        history.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        QLineEdit *addresses[2];
        FileTable *tables[2];
        QString locations[2];
        for (unsigned index = 0; index < 2; ++index) {
            auto *pane = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            addresses[index] = pane->findChild<QLineEdit *>("archivePath");
            tables[index] = pane->findChild<FileTable *>("archiveTable");
            locations[index] = fixture.filePath(QString("panel %1").arg(index));
            QVERIFY(QDir().mkpath(locations[index]));
            QVERIFY(ExternalEditorFixture::write(locations[index] + "/initial.txt", "initial"));
            addresses[index]->setFocus();
            QTRY_VERIFY(addresses[index]->hasFocus());
            window.openPath(locations[index]);
            QTRY_VERIFY(action(window, "foldersHistoryAction")->isEnabled());
        }
        addresses[owner]->setFocus();
        QTRY_VERIFY(addresses[owner]->hasFocus());
        const unsigned peer = 1 - owner;
        const QString peerPrefix = ToQString(tables[peer]->sourcePanel->GetCurrentFolderPrefix());
        const QString peerDraft = QStringLiteral("peer черновик");
        addresses[peer]->setText(peerDraft);
        bool peerFocused = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("foldersHistoryDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *tree = dialog->findChild<QTreeWidget *>("foldersHistoryList");
            QVERIFY(tree);
            const auto items = tree->findItems(target, Qt::MatchExactly);
            QCOMPARE(items.size(), 1);
            tree->setCurrentItem(items.front());
            connect(dialog, &QDialog::finished, &window, [&] {
                window.activateWindow();
                addresses[peer]->setFocus();
                QTRY_VERIFY(addresses[peer]->hasFocus());
                peerFocused = true;
            });
            dialog->accept();
        });
        action(window, "foldersHistoryAction")->trigger();
        QVERIFY(peerFocused);
        QTRY_COMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                         ToQString(tables[owner]->sourcePanel->GetCurrentFolderPrefix()))),
                     target);
        QCOMPARE(ToQString(tables[peer]->sourcePanel->GetCurrentFolderPrefix()), peerPrefix);
        QCOMPARE(addresses[peer]->text(), peerDraft);
        QCOMPARE(ExternalEditorFixture::read(target + "/target.txt"), QByteArray("target"));
    }
    void driveBookmarkUsesFocusedSourcePrefix_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void driveBookmarkUsesFocusedSourcePrefix() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer =
            window.findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
        const QString peerDraft = QStringLiteral("peer черновик 密碼");
        peer->setText(peerDraft);
        auto *address =
            window.findChild<QWidget *>("filePanel0")->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        QTRY_VERIFY(address->hasFocus());
        action(window, "openRootAction")->trigger();
        const QString source = prefix(window);
        QVERIFY(!source.isEmpty());
        QVERIFY(action(window, "saveBookmark3Action")->isEnabled());
        action(window, "saveBookmark3Action")->trigger();
        QCOMPARE(saved(3), source);
        action(window, "parentFolderAction")->trigger();
        QVERIFY(prefix(window).isEmpty());
        action(window, "openBookmark3Action")->trigger();
        QCOMPARE(prefix(window), source);
        QVERIFY(leftTable(window)->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(peer->text(), peerDraft);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*table(window), window));
            QVERIFY(UiInspection::writeReport(
                window, QString("persisted-drive-%1-%2").arg(language).arg(size.width())));
        }
        ControlledBackend restartedBackend;
        MainWindow restarted(&restartedBackend);
        action(restarted, "openBookmark3Action")->trigger();
        QCOMPARE(prefix(restarted), source);
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(restartedBackend.listCalls, 0);
    }
    void emptySlotOpensRootAndRootSaveClearsSlot() {
        ControlledBackend backend;
        MainWindow window(&backend);
        QVERIFY(action(window, "openBookmark9Action")->isEnabled());
        action(window, "openBookmark9Action")->trigger();
        QVERIFY(static_cast<FileTable *>(table(window))->sourcePanel->IsRootFolder());
        QVERIFY(prefix(window).isEmpty());
        action(window, "openRootAction")->trigger();
        action(window, "saveBookmark9Action")->trigger();
        QVERIFY(!saved(9).isEmpty());
        action(window, "parentFolderAction")->trigger();
        QVERIFY(action(window, "saveBookmark9Action")->isEnabled());
        action(window, "saveBookmark9Action")->trigger();
        QVERIFY(saved(9).isEmpty());
        QCOMPARE(backend.listCalls, 0);
    }
    void emptyRootHistorySurvivesRestart_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void emptyRootHistorySurvivesRestart() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "openRootAction")->trigger();
        action(window, "parentFolderAction")->trigger();
        QVERIFY(leftTable(window)->sourcePanel->IsRootFolder());
        QVERIFY(prefix(window).isEmpty());
        action(window, "openRootAction")->trigger();
        openEmptyHistoryItem(window, "initial");
        QVERIFY(leftTable(window)->sourcePanel->IsRootFolder());
        QVERIFY(prefix(window).isEmpty());
        ControlledBackend restartedBackend;
        MainWindow restarted(&restartedBackend);
        action(restarted, "openRootAction")->trigger();
        openEmptyHistoryItem(restarted, "restart");
        QVERIFY(leftTable(restarted)->sourcePanel->IsRootFolder());
        QVERIFY(prefix(restarted).isEmpty());
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(restartedBackend.listCalls, 0);
    }
    void realArchiveSourceLocationsAndBindingFallback_data() {
        QTest::addColumn<bool>("nested");
        QTest::addColumn<QString>("language");
        for (bool nested : {false, true})
            for (const char *language : {"-", "ru"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(nested).arg(language)))
                    << nested << QString::fromLatin1(language);
    }
    void realArchiveSourceLocationsAndBindingFallback() {
        QFETCH(bool, nested);
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const QString entry = QStringLiteral("папка space/密碼.txt");
        QVERIFY(QDir().mkpath(QFileInfo(fixture.filePath(entry)).absolutePath()));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "unchanged"));
        const QString inner = fixture.filePath(QStringLiteral("inner δ.zip"));
        const QString outer = fixture.filePath(QStringLiteral("outer 空.zip"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), inner, entry));
        if (nested)
            QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                         QFileInfo(inner).fileName()));
        const QString archive = nested ? outer : inner;
        const QByteArray original = ExternalEditorFixture::read(archive);
        const QByteArray originalInner = ExternalEditorFixture::read(inner);
        QString source, display;
        {
            ArchiveBackend backend;
            MainWindow window(&backend);
            window.show();
            window.openArchive(archive);
            QTRY_COMPARE(table(window)->rowCount(), 1);
            if (nested) {
                openRow(window, 0);
                QTRY_COMPARE(table(window)->model()->index(0, 0).data().toString(),
                             QStringLiteral("папка space"));
            }
            openRow(window, rowWithPath(window, QStringLiteral("папка space")));
            source = prefix(window);
            QCOMPARE(QDir::fromNativeSeparators(source),
                     archive + (nested ? '/' + QFileInfo(inner).fileName() : QString()) +
                         QStringLiteral("/папка space/"));
            display = window.findChild<QLineEdit *>("archivePath")->text();
            QVERIFY(source.contains(QFileInfo(archive).fileName()));
            QVERIFY(action(window, "saveBookmark4Action")->isEnabled());
            action(window, "saveBookmark4Action")->trigger();
            QCOMPARE(saved(4), source);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*table(window), window));
                QVERIFY(UiInspection::writeReport(window, QString("persisted-archive-%1-%2-%3")
                                                              .arg(nested)
                                                              .arg(language)
                                                              .arg(size.width())));
            }
            QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"),
                               QSettings::IniFormat);
            QVERIFY(settings.value("FolderHistory").toStringList().contains(source));
            action(window, "parentFolderAction")->trigger();
            action(window, "openBookmark4Action")->trigger();
            QCOMPARE(prefix(window), source);
            action(window, "openRootAction")->trigger();
            action(window, "openBookmark4Action")->trigger();
            QCOMPARE(QDir::fromNativeSeparators(prefix(window)),
                     nested ? archive + '/' : QDir::fromNativeSeparators(source));
            QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(),
                     nested ? archive : display);
        }
        ArchiveBackend backend;
        MainWindow restarted(&backend);
        action(restarted, "openBookmark4Action")->trigger();
        QCOMPARE(QDir::fromNativeSeparators(prefix(restarted)),
                 nested ? archive + '/' : QDir::fromNativeSeparators(source));
        QCOMPARE(restarted.findChild<QLineEdit *>("archivePath")->text(),
                 nested ? archive : display);
        QCOMPARE(saved(4), source);
        QCOMPARE(ExternalEditorFixture::read(archive), original);
        QCOMPARE(ExternalEditorFixture::read(inner), originalInner);
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(entry)), QByteArray("unchanged"));
    }
    void legacyDisplayEntriesPreserveLiteralComponents_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void legacyDisplayEntriesPreserveLiteralComponents() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
#ifdef Q_OS_WIN
        const QString folder = QStringLiteral("папка space");
#else
        const QString folder = QStringLiteral("left / right 密碼");
#endif
        const QString entry = folder + "/keep.txt";
        QVERIFY(QDir().mkpath(QFileInfo(fixture.filePath(entry)).absolutePath()));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "unchanged"));
        const QString archive = fixture.filePath(QStringLiteral("legacy δ.zip"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, entry));
        const QByteArray original = ExternalEditorFixture::read(archive);
        const QString legacy = archive + " / " + folder;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("FolderShortcuts", QStringList{legacy});
            settings.setValue("FolderHistory", QStringList{legacy});
        }
        ArchiveBackend backend;
        MainWindow window(&backend);
        action(window, "openBookmark0Action")->trigger();
        const QString source = prefix(window);
        QCOMPARE(QDir::fromNativeSeparators(source), archive + '/' + folder + '/');
        QCOMPARE(table(window)->model()->index(table(window)->rowCount() - 1, 0).data().toString(),
                 QString("keep.txt"));
        action(window, "openRootAction")->trigger();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("foldersHistoryDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *list = dialog->findChild<QTreeWidget *>("foldersHistoryList");
            QVERIFY(list);
            const auto items = list->findItems(legacy, Qt::MatchExactly);
            QCOMPARE(items.size(), 1);
            list->setCurrentItem(items.front());
            for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
                dialog->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*list, *dialog));
                QVERIFY(UiInspection::writeReport(
                    *dialog, QString("persisted-history-%1-%2").arg(language).arg(size.width())));
            }
            dialog->accept();
        });
        action(window, "foldersHistoryAction")->trigger();
        QCOMPARE(prefix(window), source);
        QCOMPARE(saved(0), legacy);
        QCOMPARE(ExternalEditorFixture::read(archive), original);
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(entry)), QByteArray("unchanged"));
    }
};
QTEST_MAIN(PersistedLocationTests)
#include "persisted_location_tests.moc"
