#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/extract/alias.h"
#include "filemanager/support/window_state_support.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/window_activation.h"
#include <QSettings>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QWindow>

class HelpUiTests : public QObject {
    Q_OBJECT
    QTemporaryDir privateSettings_;
    QByteArray previousSettings_;
  private slots:
    void initTestCase() {
        QVERIFY(privateSettings_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        if (previousSettings_.isEmpty())
            qputenv("SEVENZIP_SETTINGS_PATH", privateSettings_.filePath("help.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void optionsHelpRoutesEachNativePageWithoutApplying() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QtFmOptionsDialog dialog(settingsPath);
        dialog.show();
        auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        auto *command = dialog.findChild<QLineEdit *>("viewerCommand");
        command->setText("pending viewer %1");
        QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
        const QStringList keysBefore = QSettings(settingsPath, QSettings::IniFormat).allKeys();
        const QStringList fragments = {"system", "sevenZip", "folders",
                                       "editor", "settings", "language"};
        const char *controls[] = {"systemAssociations", "menuItems",        "workSystem",
                                  "viewerCommand",      "extractMemorySet", "languageChoice"};
        for (int i = 0; i < fragments.size(); ++i) {
            if (!OptionsPresentation::show(dialog, controls[i]))
                continue; // Unsupported native integration has no user surface.
            dialog.findChild<QWidget *>(controls[i])->setFocus();
            dialog.findChild<QPushButton *>("settingsHelp")->click();
            auto *help = dialog.findChild<QDialog *>("helpWindow");
            QVERIFY(help && help->isVisible());
            auto *browser = help->findChild<QTextBrowser *>("helpBrowser");
            QCOMPARE(browser->source().path(), QString("/upstream/7zip/Help/fm/options.htm"));
            QCOMPARE(browser->source().fragment(), fragments[i]);
            help->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(command->text(), QString("pending viewer %1"));
            QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
            QCOMPARE(QSettings(settingsPath, QSettings::IniFormat).allKeys(), keysBefore);
        }
        OptionsPresentation::show(dialog, command);
        dialog.activateWindow();
        QVERIFY(WindowActivation::waitForActive(dialog));
        command->setFocus();
        QTest::keyClick(dialog.windowHandle(), Qt::Key_F1);
        auto *help = dialog.findChild<QDialog *>("helpWindow");
        QVERIFY(help && help->isVisible());
        QCOMPARE(help->findChild<QTextBrowser *>("helpBrowser")->source().fragment(),
                 QString("editor"));
        help->close();
        dialog.reject();
    }
    void addAndExtractHelpRetainPendingInput_data() {
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("button") << false;
        QTest::newRow("F1") << true;
    }
    void addAndExtractHelpRetainPendingInput() {
        QFETCH(bool, keyboard);
        QTemporaryDir fixture;
        CreateArchiveDialog add(fixture.path());
        add.show();
        add.activateWindow();
        QVERIFY(WindowActivation::waitForActive(add));
        auto *name = add.findChild<QLineEdit *>("archiveName");
        QVERIFY(name);
        name->setText(QStringLiteral("новый архив с пробелом"));
        const auto showHelp = [keyboard](QDialog &dialog, const QString &topic) {
            auto *buttons = dialog.findChild<QDialogButtonBox *>();
            auto *helpButton = buttons->button(QDialogButtonBox::Help);
            if (keyboard || !helpButton) {
                if (!WindowActivation::waitForActive(dialog) ||
                    !QTest::qWaitFor([&] { return QApplication::activeWindow() == &dialog; }))
                    return false;
                QTest::keyClick(dialog.windowHandle(), Qt::Key_F1);
            } else
                helpButton->click();
            if (!QTest::qWaitFor([&] {
                    auto *help = dialog.findChild<QDialog *>("helpWindow");
                    return help && help->isVisible();
                }))
                return false;
            auto *help = dialog.findChild<QDialog *>("helpWindow");
            const bool matches = help->findChild<QTextBrowser *>("helpBrowser")->source().path() ==
                                 "/upstream/7zip/Help/fm/plugins/7-zip/" + topic;
            help->close();
            return matches;
        };
        QVERIFY(showHelp(add, "add.htm"));
        QCOMPARE(name->text(), QStringLiteral("новый архив с пробелом"));
        add.reject();
        ExtractDialog extract(fixture.path(), {});
        extract.show();
        extract.activateWindow();
        QVERIFY(WindowActivation::waitForActive(extract));
        auto *destination = extract.findChild<QComboBox *>("extractDestination");
        destination->setEditText(fixture.filePath(QStringLiteral("папка с пробелом")));
        auto *password = extract.findChild<QLineEdit *>("extractPassword");
        password->setText("pending secret");
        const QString pendingDestination = destination->currentText();
        QVERIFY(showHelp(extract, "extract.htm"));
        QCOMPARE(destination->currentText(), pendingDestination);
        QCOMPARE(password->text(), QString("pending secret"));
        extract.reject();
    }
    void sourceAboutCommandUsesModalOwner() {
        MainWindow window;
        auto *action = window.findChild<QAction *>("aboutAction");
        QVERIFY(action && action->isEnabled());
        bool observed = false;
        QTimer close;
        connect(&close, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("aboutDialog");
            if (dialog && dialog->isVisible()) {
                observed = QApplication::activeModalWidget() == dialog;
                close.stop();
                dialog->accept();
            }
        });
        close.start(5);
        action->trigger();
        QVERIFY(observed);
        QVERIFY(!window.findChild<QDialog *>("aboutDialog"));
    }
};
QTEST_MAIN(HelpUiTests)
#include "help_ui_tests.moc"
