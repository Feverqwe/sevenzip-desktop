#include "Common/MyCom.h"
#include "archive/operations/ZipRegistry.h"
#include "localization/LangUtils.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_page_fixture.h"
#include "support/options_presentation.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSettings>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
using OptionsPageFixture::COptionsAssociations;
using OptionsPageFixture::COptionsManager;
QString text(const UString &value) { return QString::fromWCharArray(value.Ptr()); }
} // namespace

class OptionsIntegrationTests final : public QObject {
    Q_OBJECT
  private slots:
    void undoPendingFormatChoiceDoesNotExposeDeletionToApplyObserver() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto services = std::make_shared<COptionsAssociations>();
        services->otherHandler = true;
        QtFmOptionsDialog dialog(fixture.filePath("undo.ini"), nullptr, manager, services);
        auto *page = dialog.findChild<CSystemPage *>();
        auto *choice = dialog.findChild<QCheckBox *>("association-zip");
        auto *associations = dialog.findChild<QTreeWidget *>("systemAssociations");
        QVERIFY(page && choice && associations);
        QCOMPARE(associations->topLevelItemCount(), 1);
        const QString originalHandler = associations->topLevelItem(0)->text(1);
        QVERIFY(!originalHandler.isEmpty());
        choice->click();
        QVERIFY(choice->isChecked());
        QCOMPARE(services->writes, 0);

        // Apply observers must see the completed undo, which retains the
        // original handler, rather than the source cycle's temporary Clear.
        connect(page, &CSystemPage::Changed, &dialog, [&] { QVERIFY(dialog.apply()); });
        choice->click();
        QVERIFY(!choice->isChecked());
        QCOMPARE(services->writes, 0);
        QVERIFY(!page->IsModified());
        QCOMPARE(associations->topLevelItem(0)->text(1), originalHandler);
    }

    void formatSearchAndMassSelectionPreserveSourceDraft() {
        QTemporaryDir fixture;
        auto *formats = new COptionsManager;
        formats->Extensions = UString(L"7z zip tar");
        CMyComPtr<IFolderManager> manager = formats;
        auto services = std::make_shared<COptionsAssociations>();
        services->otherHandler = true;
        const QString path = fixture.filePath("search.ini");
        QtFmOptionsDialog dialog(path, nullptr, manager, services);
        dialog.show();
        QVERIFY(OptionsPresentation::show(dialog, "associationSearch"));
        QVERIFY(UiInspection::writeReport(dialog, "settings-format-search-initial"));
        auto *search = dialog.findChild<QLineEdit *>("associationSearch");
        auto *tree = dialog.findChild<QTreeWidget *>("associationChoices");
        auto *apply = dialog.findChild<QDialogButtonBox *>("settingsButtons")
                          ->button(QDialogButtonBox::Apply);
        QList<int> changedSelections;
        auto *page = dialog.findChild<CSystemPage *>();
        connect(page, &CSystemPage::Changed, &dialog, [&] {
            int selected = 0;
            for (const char *name : {"association-7z", "association-zip", "association-tar"})
                selected += dialog.findChild<QCheckBox *>(name)->isChecked();
            changedSelections.append(selected);
            // State observers must receive the completed checkbox/count view.
            QCOMPARE(dialog.findChild<QLabel *>("associationCount")->text(),
                     QCoreApplication::translate("SettingsRedesign", "%n selected formats", nullptr,
                                                 selected));
        });
        for (const QString &query : {QString("ZIP"), QString(".tar"), QString("no such format")}) {
            search->setText(query);
            QVERIFY(!apply->isEnabled());
            QCOMPARE(services->writes, 0);
        }
        QVERIFY(dialog.findChild<QLabel *>("associationEmpty")->isVisible());
        QVERIFY(UiInspection::writeReport(dialog, "settings-format-search-empty"));
        search->setText(".zip");
        QVERIFY(tree->isVisible());
        QVERIFY(UiInspection::writeReport(dialog, "settings-format-search-filtered"));
        dialog.findChild<QPushButton *>("associationSelectAll")->click();
        QCOMPARE(changedSelections, QList<int>{3});
        search->clear();
        for (const char *name : {"association-7z", "association-zip", "association-tar"})
            QVERIFY(dialog.findChild<QCheckBox *>(name)->isChecked());
        QCOMPARE(
            dialog.findChild<QLabel *>("associationCount")->text(),
            QCoreApplication::translate("SettingsRedesign", "%n selected formats", nullptr, 3));
        QVERIFY(OptionsPresentation::show(dialog, "editorCommand"));
        QVERIFY(OptionsPresentation::show(dialog, search));
        QVERIFY(dialog.findChild<QCheckBox *>("association-tar")->isChecked());
        search->setText(".zip");
        dialog.findChild<QPushButton *>("associationClearAll")->click();
        QCOMPARE(changedSelections, (QList<int>{3, 0}));
        search->clear();
        for (const char *name : {"association-7z", "association-zip", "association-tar"})
            QVERIFY(!dialog.findChild<QCheckBox *>(name)->isChecked());
        dialog.apply();
        QCOMPARE(services->writes, 0); // No deletion of the original Other handlers.
        dialog.findChild<QPushButton *>("associationSelectAll")->click();
        dialog.reject();
        QCOMPARE(services->writes, 0);
        QtFmOptionsDialog reopened(path, nullptr, manager, services);
        QVERIFY(!reopened.findChild<QCheckBox *>("association-zip")->isChecked());
    }
    void formatChoicesUseSourceStatesAndApplyOnlyOnConfirmation() {
        QTemporaryDir fixture;
        auto *formats = new COptionsManager;
        formats->Extensions = UString(L"7z zip tar");
        CMyComPtr<IFolderManager> manager = formats;
        auto services = std::make_shared<COptionsAssociations>();
        services->otherHandler = true;
        const QString path = fixture.filePath("formats.ini");
        {
            QtFmOptionsDialog dialog(path, nullptr, manager, services);
            dialog.show();
            auto *zip = dialog.findChild<QCheckBox *>("association-zip");
            QVERIFY(OptionsPresentation::show(dialog, zip));
            QVERIFY(zip->width() >= zip->style()->pixelMetric(QStyle::PM_IndicatorWidth));
            QVERIFY(zip->height() >= zip->style()->pixelMetric(QStyle::PM_IndicatorHeight));
            QVERIFY(UiInspection::writeReport(dialog, "settings-integration-choices"));
            QVERIFY(!zip->isChecked());
            for (int i = 0; i < 3; ++i) {
                QTest::keyClick(zip, Qt::Key_Space);
                QCOMPARE(zip->isChecked(), i != 1);
            }
            auto *tree = dialog.findChild<QTreeWidget *>("systemAssociations");
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                if (tree->topLevelItem(i)->text(0) == "zip")
                    QCOMPARE(tree->topLevelItem(i)->text(1), QString("7-Zip"));
            QVERIFY(OptionsPresentation::show(dialog, "association-tar"));
            dialog.findChild<QCheckBox *>("association-tar")->click();
            QVERIFY(UiInspection::writeReport(dialog, "settings-integration-other-formats"));
            auto *search = dialog.findChild<QLineEdit *>("associationSearch");
            search->setText(".zip");
            QTRY_VERIFY(!dialog.findChild<QCheckBox *>("association-tar")->isVisible());
            search->clear();
            QVERIFY(dialog.findChild<QCheckBox *>("association-tar")->isChecked());
            QVERIFY(OptionsPresentation::show(dialog, tree));
            QVERIFY(UiInspection::writeReport(dialog, "settings-integration-association-details"));
            dialog.reject();
        }
        QCOMPARE(services->writes, 0);
        QtFmOptionsDialog applied(path, nullptr, manager, services);
        for (const char *name : {"association-7z", "association-zip", "association-tar"})
            applied.findChild<QCheckBox *>(name)->click();
        applied.apply();
        QCOMPARE(services->writes, 3);
        QVERIFY(!applied.findChild<QDialogButtonBox *>("settingsButtons")
                     ->button(QDialogButtonBox::Apply)
                     ->isEnabled());
        applied.findChild<QCheckBox *>("association-tar")->click();
        applied.apply();
        QCOMPARE(services->writes, 4);
    }
    void pageChangesKeepDraftAndApplyCancel() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("groups.ini");
        QtFmOptionsDialog dialog(path);
        dialog.show();
        QVERIFY(OptionsPresentation::show(dialog, "editorCommand"));
        auto *editor = dialog.findChild<QLineEdit *>("editorCommand");
        editor->setText("custom editor --wait");
        QVERIFY(UiInspection::writeReport(dialog, "settings-external-apps-expanded"));
        QVERIFY(OptionsPresentation::show(dialog, "fullRowOption"));
        QVERIFY(UiInspection::writeReport(dialog, "settings-advanced-collapsed"));
        QVERIFY(OptionsPresentation::show(dialog, "languageChoice"));
        dialog.resize(780, 560);
        QApplication::processEvents();
        auto *description = dialog.findChild<QLabel *>("translationDescription");
        QVERIFY(description);
        const auto descriptionStart =
            description->mapTo(&dialog, description->contentsRect().topLeft());
        QVERIFY(UiInspection::writeReport(dialog, "settings-general"));
        QVERIFY(OptionsPresentation::show(dialog, "languageInfo"));
        QCOMPARE(dialog.findChild<QWidget *>("languageInfo")->mapTo(&dialog, QPoint()),
                 descriptionStart);
        QVERIFY(!description->isVisible());
        QVERIFY(UiInspection::writeReport(dialog, "settings-translation-expanded"));
        dialog.findChild<QtDisclosure *>("translationDetails")->setExpanded(false);
        QApplication::processEvents();
        QVERIFY(description->isVisible());
        QCOMPARE(description->mapTo(&dialog, description->contentsRect().topLeft()),
                 descriptionStart);
        QVERIFY(OptionsPresentation::show(dialog, editor));
        QCOMPARE(editor->text(), QString("custom editor --wait"));
        dialog.apply();
        dialog.reject();
        QtFmOptionsDialog reopened(path);
        QCOMPARE(reopened.findChild<QLineEdit *>("editorCommand")->text(),
                 QString("custom editor --wait"));
        reopened.findChild<QLineEdit *>("editorCommand")->setText("discarded");
        reopened.reject();
        QtFmOptionsDialog retained(path);
        QCOMPARE(retained.findChild<QLineEdit *>("editorCommand")->text(),
                 QString("custom editor --wait"));
    }
    void sourceInitialFrame_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void sourceInitialFrame() {
        QFETCH(QString, language);
        const QFont originalFont = QApplication::font();
        const auto restoreFont = qScopeGuard([&] { QApplication::setFont(originalFont); });
        for (int points : {8, 12}) {
            QFont font = originalFont;
            font.setPointSize(points);
            QApplication::setFont(font);
            QTemporaryDir fixture;
            const QString settings = fixture.filePath("options.ini");
            SaveRegLang(UString(language.toStdWString().c_str()), settings);
            CMyComPtr<IFolderManager> manager = new COptionsManager;
            auto associations = std::make_shared<COptionsAssociations>();
            QtFmOptionsDialog dialog(settings, nullptr, manager, associations);
            dialog.show();
            QApplication::processEvents();
            auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
            QCOMPARE(pages->currentIndex(), 0);
            QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QComboBox *>("appearanceChoice"),
                                               dialog));
            QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QComboBox *>("languageChoice"),
                                               dialog));
            QVERIFY(!dialog.findChild<QWidget *>("systemAssociations")->isVisible());
            QVERIFY(UiInspection::writeReport(
                dialog, QString("options-initial-%1-%2").arg(language).arg(points)));
            dialog.resize(520, 380);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(
                *dialog.findChild<QDialogButtonBox *>("settingsButtons"), dialog));
            QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QComboBox *>("appearanceChoice"),
                                               dialog));
            dialog.reject();
            QCOMPARE(associations->writes, 0);
            QCOMPARE(associations->notifications, 0);
        }
    }
    void translatedNavigationFitsNormalAndCompactWindows() {
        const QFont originalFont = QApplication::font();
        const auto restoreFont = qScopeGuard([&] { QApplication::setFont(originalFont); });
        QFont font = originalFont;
        font.setPointSize(12);
        QApplication::setFont(font);
        for (const char *language : {"-", "ru"}) {
            QTemporaryDir fixture;
            const QString settings = fixture.filePath("options.ini");
            SaveRegLang(UString(language), settings);
            CMyComPtr<IFolderManager> manager = new COptionsManager;
            auto associations = std::make_shared<COptionsAssociations>();
            QtFmOptionsDialog dialog(settings, nullptr, manager, associations);
            dialog.show();
            QApplication::processEvents();
            auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
            for (const QSize size : {QSize(780, 560), QSize(520, 420)}) {
                dialog.resize(size);
                QApplication::processEvents();
                auto *navigation = dialog.findChild<QListWidget *>("settingsNavigation");
                auto *help = dialog.findChild<QPushButton *>("settingsHelp");
                auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
                QVERIFY(UiInspection::fullyVisible(*help, dialog));
                QVERIFY(UiInspection::bounds(*help, dialog).right() <
                        UiInspection::bounds(*buttons, dialog).left());
                if (!navigation->isVisible())
                    navigation = dialog.findChild<QListWidget *>("settingsSectionChoice");
                QVERIFY(navigation->isVisible());
                for (int i = 0; i < navigation->count(); ++i) {
                    navigation->setCurrentRow(i);
                    QApplication::processEvents();
                    QVERIFY(navigation->viewport()->rect().contains(
                        navigation->visualItemRect(navigation->item(i))));
                    QCOMPARE(pages->currentWidget()->findChild<QLabel *>("settingsHeading")->text(),
                             navigation->item(i)->text());
                    QVERIFY(
                        UiInspection::writeReport(dialog, QString("settings-navigation-%1-%2-%3")
                                                              .arg(language)
                                                              .arg(size.width())
                                                              .arg(i)));
                }
            }
            dialog.reject();
            QCOMPARE(associations->writes, 0);
        }
    }
    void allPagesApplyCancelAndRestart_data() {
        QTest::addColumn<int>("finish");
        QTest::addColumn<LONG>("failure");
        QTest::newRow("cancel") << 0 << LONG(0);
        QTest::newRow("apply-then-cancel") << 1 << LONG(0);
        QTest::newRow("ok") << 2 << LONG(0);
        QTest::newRow("cancel-denied") << 0 << LONG(5);
        QTest::newRow("apply-denied") << 1 << LONG(5);
        QTest::newRow("ok-denied") << 2 << LONG(5);
        QTest::newRow("apply-external-change") << 1 << LONG(1237);
        QTest::newRow("ok-external-change") << 2 << LONG(1237);
        QTest::newRow("denied-then-invalid-memory") << 3 << LONG(5);
    }
    void allPagesApplyCancelAndRestart() {
        QFETCH(int, finish);
        QFETCH(LONG, failure);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("options 空.ini");
        SaveRegLang(UString("-"), path);
        {
            QSettings saved(path, QSettings::IniFormat);
            saved.setValue("Unrelated/value", "preserved");
        }
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto associations = std::make_shared<COptionsAssociations>();
        associations->failure = failure;
        QtFmOptionsDialog dialog(path, nullptr, manager, associations);
        dialog.resize(finish == 1 ? QSize(520, 380) : QSize(640, 390));
        dialog.show();
        auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
        QCOMPARE(pages->currentIndex(), 0);
        auto *apply = dialog.findChild<QDialogButtonBox *>("settingsButtons")
                          ->button(QDialogButtonBox::Apply);
        QVERIFY(!apply->isEnabled());
        QSignalSpy applied(&dialog, &QtFmOptionsDialog::Applied);
        auto *types = dialog.findChild<QTreeWidget *>("systemAssociations");
        QCOMPARE(types->topLevelItemCount(), 1);
        types->topLevelItem(0)->setSelected(true);
        dialog.findChild<QPushButton *>("systemCurrent")->click();
        dialog.findChild<QCheckBox *>("cascadedMenu")->click();
        OptionsPresentation::show(dialog, "workSpecified");
        dialog.findChild<QRadioButton *>("workSpecified")->click();
        const QString work = fixture.filePath("рабочая папка");
        dialog.findChild<QLineEdit *>("workPath")->setText(work);
        OptionsPresentation::show(dialog, "editorCommand");
        const QString editor = QStringLiteral("\"/fixture/editor 空\" --wait");
        auto *command = dialog.findChild<QLineEdit *>("editorCommand");
        command->setText(editor);
        command->setFocus();
        QCOMPARE(dialog.focusWidget(), command);
        OptionsPresentation::show(dialog, "gridOption");
        dialog.findChild<QCheckBox *>("gridOption")->setChecked(true);
        OptionsPresentation::show(dialog, "languageChoice");
        auto *language = dialog.findChild<QComboBox *>("languageChoice");
        QVERIFY(language->count() > 1);
        const int russian = language->findText(QString::fromUtf8("Русский"), Qt::MatchContains);
        QVERIFY(russian >= 0);
        language->setCurrentIndex(russian);
        auto *memory = dialog.findChild<QLineEdit *>("extractMemoryEdit");
        if (finish == 3) {
            dialog.findChild<QCheckBox *>("extractMemorySet")->setChecked(true);
            memory->setText("not-a-number");
        }
        QVERIFY(apply->isEnabled());
        int errors = 0;
        QMessageBox::Icon errorIcon = QMessageBox::NoIcon;
        QTimer errorDriver;
        connect(&errorDriver, &QTimer::timeout, &dialog, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            ++errors;
            errorIcon = box->icon();
            const bool visible = UiInspection::fullyVisible(*box, *box);
            const bool report = UiInspection::writeReport(
                *box, QString("options-apply-error-%1-%2-%3").arg(failure).arg(finish).arg(errors));
            box->accept();
            QVERIFY(visible);
            QVERIFY(report);
        });
        errorDriver.start(10);
        if (finish >= 2) {
            dialog.accept();
            if (finish == 3) {
                QVERIFY(dialog.isVisible());
                QCOMPARE(dialog.result(), int(QDialog::Rejected));
                QVERIFY(pages->currentWidget()->isAncestorOf(memory));
                QVERIFY(memory->isVisible());
                QCOMPARE(memory->text(), QString("not-a-number"));
                QCOMPARE(NExtract::Read_LimitGB(path), UInt32(-1));
                UString committedEditor;
                ReadRegEditor(true, committedEditor, path);
                QCOMPARE(text(committedEditor), editor);
                memory->setText("2");
                dialog.accept();
                QCOMPARE(NExtract::Read_LimitGB(path), UInt32(2));
            }
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        } else {
            if (finish == 1) {
                apply->click();
                QVERIFY(!apply->isEnabled());
                command->setText("discarded editor");
                dialog.findChild<QLineEdit *>("workPath")->setText("discarded folder");
                dialog.findChild<QCheckBox *>("gridOption")->setChecked(false);
                language->setCurrentIndex(0);
                dialog.findChild<QCheckBox *>("cascadedMenu")->setChecked(true);
                QVERIFY(apply->isEnabled());
            }
            dialog.reject();
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
        }
        QCOMPARE(applied.size(), finish == 0 ? 0 : 1);
        QCOMPARE(errors, (finish != 0 && failure != 0 ? 1 : 0) + (finish == 3 ? 1 : 0));
        if (errors)
            QCOMPARE(errorIcon, QMessageBox::Critical);
        QCOMPARE(associations->writes, finish == 0 ? 0 : 1);
        QCOMPARE(associations->notifications, finish == 0 ? 0 : 1);
        QCOMPARE(types->topLevelItem(0)->text(1),
                 finish != 0 && failure != 0 ? QString() : QString("7-Zip"));
        // Memory validation reveals the input; other applies keep the section.
        QVERIFY(pages->currentWidget()->isAncestorOf(
            finish == 3 ? static_cast<QWidget *>(memory)
                        : dialog.findChild<QWidget *>("appearanceChoice")));
        UString savedEditor;
        ReadRegEditor(true, savedEditor, path);
        QCOMPARE(text(savedEditor), finish == 0 ? QString() : editor);
        NWorkDir::CInfo folders;
        folders.Load(path);
        QCOMPARE(folders.Mode,
                 finish == 0 ? NWorkDir::NMode::kSystem : NWorkDir::NMode::kSpecified);
        QCOMPARE(QString::fromWCharArray(fs2us(folders.Path).Ptr()),
                 finish == 0 ? QString() : work);
        CFmSettings settings;
        settings.Load(path);
        QCOMPARE(settings.ShowGrid, finish != 0);
        CContextMenuInfo menu;
        menu.Load(path);
        QCOMPARE(menu.Cascaded.Val, finish == 0);
        UString savedLanguage;
        ReadRegLang(savedLanguage, path);
        QCOMPARE(text(savedLanguage), finish == 0 ? QString("-") : QString("ru"));
        QtFmOptionsDialog restarted(path, nullptr, manager, associations);
        QCOMPARE(restarted.findChild<QTabWidget *>("optionsPages")->currentIndex(), 0);
        QCOMPARE(restarted.findChild<QLineEdit *>("editorCommand")->text(),
                 finish == 0 ? QString() : editor);
        QVERIFY(!restarted.findChild<QDialogButtonBox *>("settingsButtons")
                     ->button(QDialogButtonBox::Apply)
                     ->isEnabled());
        QSettings saved(path, QSettings::IniFormat);
        QCOMPARE(saved.value("Unrelated/value").toString(), QString("preserved"));
        SaveRegLang(UString("-"), path);
        ReloadLang(path);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(OptionsIntegrationTests)
#include "options_integration_tests.moc"
