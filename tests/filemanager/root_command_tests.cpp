#include "7zip/UI/FileManager/resource.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>

class RootCommandTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
    static QString drivePrefix(const QString &language) {
#ifdef Q_OS_WIN
        Q_UNUSED(language);
        return QString::fromWCharArray(LangString(IDS_COMPUTER)) + '\\';
#else
        return (language == "ru" ? QStringLiteral("Тома") : QStringLiteral("Volumes")) + '/';
#endif
    }
    static void enterFolder(QLineEdit &address, const QString &path) {
        PanelKeys::focusAddress(&address);
        address.setText(path);
        QTest::keyClick(&address, Qt::Key_Return);
    }
  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void commandAndCharacterRoutesUseDrives_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("route");
        for (const char *language : {"-", "ru"})
            for (const QSize size : {QSize(980, 620), QSize(640, 400)})
                for (int route = 0; route < 4; ++route)
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(language).arg(size.width()).arg(route)))
                        << QString::fromLatin1(language) << size << route;
    }
    void commandAndCharacterRoutesUseDrives() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QFETCH(int, route);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString leftPath = fixture.filePath(QStringLiteral("left space/левый"));
        const QString rightPath = fixture.filePath(QStringLiteral("right space/右"));
        QVERIFY(QDir().mkpath(leftPath));
        QVERIFY(QDir().mkpath(rightPath));
        const QString control = leftPath + QStringLiteral("/контроль δ.bin");
        const QByteArray bytes("unchanged\0binary", 16);
        QVERIFY(ExternalEditorFixture::write(control, bytes));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *leftAddress = left->findChild<QLineEdit *>("archivePath");
        auto *rightAddress = right->findChild<QLineEdit *>("archivePath");
        auto *leftTable = left->findChild<FileTable *>("archiveTable");
        auto *rightTable = right->findChild<FileTable *>("archiveTable");
        QVERIFY(leftAddress && rightAddress && leftTable && rightTable);
        enterFolder(*leftAddress, leftPath);
        action(window, "viewMode3Action")->trigger();
        QTRY_VERIFY(leftTable->hasFocus());
        enterFolder(*rightAddress, rightPath);
        action(window, "viewMode3Action")->trigger();
        QTRY_VERIFY(rightTable->hasFocus());
        if (route == 0)
            action(window, "openRootAction")->trigger();
        else
            QTest::keyClick(rightTable, route == 1 ? Qt::Key_Backslash : Qt::Key_Slash,
                            route == 3 ? Qt::KeypadModifier : Qt::NoModifier);
        QTRY_COMPARE(rightAddress->text(), drivePrefix(language));
        QVERIFY(static_cast<FileTable *>(rightTable)->sourcePanel->IsFSDrivesFolder());
        QVERIFY(rightTable->rowCount() > 0);
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
        QVERIFY(!action(window, "copyAction")->isEnabled());
        QCOMPARE(leftAddress->text(), QFileInfo(leftPath).canonicalFilePath());
        QCOMPARE(leftTable->rowCount(), 1);
        QCOMPARE(ExternalEditorFixture::read(control), bytes);
        QCOMPARE(backend.listCalls, 0);
        if (route == 0) {
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(leftAddress), window));
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(rightAddress), window));
            QVERIFY(UiInspection::fullyVisible(*rightTable, window));
            QVERIFY(UiInspection::writeReport(
                window, QString("root-command-%1-%2").arg(language).arg(size.width())));
        }
        action(window, "parentFolderAction")->trigger();
        QTRY_VERIFY(rightAddress->text().isEmpty());
        QVERIFY(static_cast<FileTable *>(rightTable)->sourcePanel->IsRootFolder());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(rightAddress->text(), QFileInfo(rightPath).canonicalFilePath());
        QCOMPARE(leftAddress->text(), QFileInfo(leftPath).canonicalFilePath());
    }
    void driveViewIsRestoredFromFilesystemHistory_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void driveViewIsRestoredFromFilesystemHistory() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        enterFolder(*address, fixture.path());
        action(window, "openRootAction")->trigger();
        QTRY_COMPARE(address->text(), drivePrefix(language));
        enterFolder(*address, fixture.path());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(address->text(), drivePrefix(language));
        QVERIFY(static_cast<FileTable *>(table(window))->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(backend.listCalls, 0);
    }
    void liveLanguageUpdatesBothDriveAddresses_data() {
        QTest::addColumn<bool>("pending");
        QTest::newRow("bound") << false;
        QTest::newRow("pending") << true;
    }
    void liveLanguageUpdatesBothDriveAddresses() {
        QFETCH(bool, pending);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        QLineEdit *addresses[2];
        FileTable *views[2];
        for (unsigned i = 0; i < 2; ++i) {
            auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(i));
            QVERIFY(panel);
            addresses[i] = panel->findChild<QLineEdit *>("archivePath");
            views[i] = panel->findChild<FileTable *>("archiveTable");
            PanelKeys::focusAddress(addresses[i]);
            QTRY_VERIFY(addresses[i]->hasFocus());
            action(window, "viewMode3Action")->trigger();
            action(window, "openRootAction")->trigger();
            QTRY_COMPARE(addresses[i]->text(), drivePrefix("-"));
        }
        views[1]->setCurrentCell(0, 0);
        views[1]->selectRow(0);
        const QPersistentModelIndex selected = views[1]->model()->index(0, 0);
        QVERIFY(selected.isValid() && PanelIndexSelected(selected));
        const int count = views[1]->rowCount();
        const QString edit = QStringLiteral("черновик / path 密碼");
        if (pending) {
            addresses[0]->setText(edit);
            addresses[0]->setSelection(10, -4);
        }
        for (const QString language : {QStringLiteral("ru"), QStringLiteral("-")}) {
            SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            QCOMPARE(addresses[0]->text(), pending ? edit : drivePrefix(language));
            if (pending) {
                QCOMPARE(addresses[0]->cursorPosition(), 6);
                QCOMPARE(addresses[0]->selectedText(), edit.mid(6, 4));
            }
            QCOMPARE(addresses[1]->text(), drivePrefix(language));
            QCOMPARE(window.windowTitle(), drivePrefix(language));
            QCOMPARE(views[1]->rowCount(), count);
            QCOMPARE(views[1]->model()->index(0, 0), selected);
            QVERIFY(PanelIndexSelected(selected));
            QVERIFY(action(window, "openItemAction")->isEnabled());
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*addresses[0], window));
                QVERIFY(UiInspection::fullyVisible(*addresses[1], window));
                QVERIFY(UiInspection::writeReport(window, QString("root-language-%1-%2-%3")
                                                              .arg(pending)
                                                              .arg(language)
                                                              .arg(size.width())));
            }
        }
        if (pending) {
            widgetDialogs_.respond(window, [](QDialog &dialog) {
                auto *language = dialog.findChild<QComboBox *>("languageChoice");
                auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
                if (language && buttons)
                    language->setCurrentIndex(language->findData(QStringLiteral("ru")));
                if (buttons)
                    buttons->button(QDialogButtonBox::Ok)->click();
                if (dialog.isVisible())
                    dialog.reject();
            });
            action(window, "settingsAction")->trigger();
            QCOMPARE(addresses[1]->text(), drivePrefix("ru"));
            QCOMPARE(addresses[0]->text(), edit);
            QCOMPARE(addresses[0]->cursorPosition(), 6);
            QCOMPARE(addresses[0]->selectedText(), edit.mid(6, 4));
        }
        QCOMPARE(backend.listCalls, 0);
    }
    void rootCommandClosesArchiveLinks_data() {
        QTest::addColumn<bool>("nested");
        QTest::newRow("single") << false;
        QTest::newRow("nested") << true;
    }
    void rootCommandClosesArchiveLinks() {
        QFETCH(bool, nested);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("папка space/keep.txt");
        QVERIFY(QDir().mkpath(QFileInfo(fixture.filePath(entry)).absolutePath()));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "unchanged"));
        const QString archive = fixture.filePath(QStringLiteral("inner δ.zip"));
        const QString outer = fixture.filePath(QStringLiteral("outer 空.zip"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, entry));
        if (nested)
            QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                         QFileInfo(archive).fileName()));
        const QByteArray original = ExternalEditorFixture::read(archive);
        const QByteArray originalOuter = nested ? ExternalEditorFixture::read(outer) : QByteArray();
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(nested ? outer : archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        if (nested) {
            openRow(window, 0);
            QTRY_COMPARE(table(window)->model()->index(0, 0).data().toString(),
                         QStringLiteral("папка space"));
        }
        action(window, "openRootAction")->trigger();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QTRY_COMPARE(address->text(), drivePrefix("-"));
        QVERIFY(static_cast<FileTable *>(table(window))->sourcePanel->IsFSDrivesFolder());
        QVERIFY(!static_cast<FileTable *>(table(window))->sourcePanel->IsArcFolder());
        action(window, "parentFolderAction")->trigger();
        QTRY_VERIFY(address->text().isEmpty());
        QCOMPARE(ExternalEditorFixture::read(archive), original);
        if (nested)
            QCOMPARE(ExternalEditorFixture::read(outer), originalOuter);
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(entry)), QByteArray("unchanged"));
    }
};
QTEST_MAIN(RootCommandTests)
#include "root_command_tests.moc"
