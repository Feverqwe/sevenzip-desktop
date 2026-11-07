#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_page_fixture.h"
#include "support/options_presentation.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyleHints>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace OptionsPageFixture;

class AppearanceSettingsTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    Qt::ColorScheme previous_;
    UString previousLanguage_;
    QString path() const { return fixture_.filePath("appearance.ini"); }
    static QComboBox *choice(QtFmOptionsDialog &dialog) {
        return dialog.findChild<QComboBox *>("appearanceChoice");
    }
    static void choose(QtFmOptionsDialog &dialog, Qt::ColorScheme scheme) {
        choice(dialog)->setCurrentIndex(choice(dialog)->findData(int(scheme)));
    }
    static void verifyNativeScheme(Qt::ColorScheme scheme) {
        const QString platform = QGuiApplication::platformName();
        if (platform == "cocoa" || platform == "windows")
            QTRY_COMPARE(QGuiApplication::styleHints()->colorScheme(), scheme);
    }
    static void settlePalette(QWidget &view) {
        // Native palette changes reach existing widgets after the style hint.
        QTRY_COMPARE(view.palette().color(QPalette::Window),
                     QApplication::palette().color(QPalette::Window));
        QTRY_COMPARE(view.palette().color(QPalette::WindowText),
                     QApplication::palette().color(QPalette::WindowText));
    }

  private slots:
    void init() {
        QVERIFY(fixture_.isValid());
        QFile::remove(path());
        previous_ = QGuiApplication::styleHints()->colorScheme();
        previousLanguage_ = g_LangID;
    }
    void cleanup() {
        QGuiApplication::styleHints()->setColorScheme(previous_);
        SaveRegLang(previousLanguage_, path());
        ReloadLang(path());
        QtNotifyLanguageChanged();
        QFile::remove(path());
    }
    void draftCancelAndApplyReopen() {
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto services = std::make_shared<COptionsAssociations>();
        {
            QtFmOptionsDialog dialog(path(), nullptr, manager, services);
            choose(dialog, Qt::ColorScheme::Dark);
            QCOMPARE(QGuiApplication::styleHints()->colorScheme(), previous_);
            dialog.reject();
        }
        QtFmOptionsDialog dialog(path(), nullptr, manager, services);
        QCOMPARE(choice(dialog)->currentData().toInt(), int(Qt::ColorScheme::Unknown));
        choose(dialog, Qt::ColorScheme::Light);
        dialog.apply();
        verifyNativeScheme(Qt::ColorScheme::Light);
        choose(dialog, Qt::ColorScheme::Dark);
        SaveRegLang(UString("ru"), path());
        ReloadLang(path());
        QtNotifyLanguageChanged();
        QCOMPARE(choice(dialog)->currentData().toInt(), int(Qt::ColorScheme::Dark));
        QVERIFY(dialog.findChild<QDialogButtonBox *>("settingsButtons")
                    ->button(QDialogButtonBox::Apply)
                    ->isEnabled());
        dialog.reject();
        verifyNativeScheme(Qt::ColorScheme::Light);
        QtFmOptionsDialog reopened(path(), nullptr, manager, services);
        QCOMPARE(choice(reopened)->currentData().toInt(), int(Qt::ColorScheme::Light));
        choose(reopened, Qt::ColorScheme::Unknown);
        reopened.apply();
        QtFmOptionsDialog system(path(), nullptr, manager, services);
        QCOMPARE(choice(system)->currentData().toInt(), int(Qt::ColorScheme::Unknown));
        QCOMPARE(services->writes, 0);
    }
    void invalidMemoryKeepsAppearanceDraft() {
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto services = std::make_shared<COptionsAssociations>();
        QtFmOptionsDialog dialog(path(), nullptr, manager, services);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        choose(dialog, Qt::ColorScheme::Dark);
        dialog.findChild<QCheckBox *>("extractMemorySet")->setChecked(true);
        dialog.findChild<QLineEdit *>("extractMemoryEdit")->setText("invalid");
        bool error = false;
        QTimer closer;
        connect(&closer, &QTimer::timeout, &dialog, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                error = true;
                box->accept();
            }
        });
        closer.start(1);
        dialog.apply();
        closer.stop();
        QVERIFY(error);
        QCOMPARE(QGuiApplication::styleHints()->colorScheme(), previous_);
        QCOMPARE(choice(dialog)->currentData().toInt(), int(Qt::ColorScheme::Dark));
        dialog.reject();
        QtFmOptionsDialog reopened(path(), nullptr, manager, services);
        QCOMPARE(choice(reopened)->currentData().toInt(), int(Qt::ColorScheme::Unknown));
    }
    void nativePaletteAndOtherOptions() {
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto services = std::make_shared<COptionsAssociations>();
        QtFmOptionsDialog dialog(path(), nullptr, manager, services);
        OptionsPresentation::show(dialog, "appearanceChoice");
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        choose(dialog, Qt::ColorScheme::Light);
        dialog.apply();
        verifyNativeScheme(Qt::ColorScheme::Light);
        settlePalette(dialog);
        const QColor light = QApplication::palette().color(QPalette::Window);
        QVERIFY(UiInspection::writeReport(dialog, "appearance-settings-applied-light"));
        choose(dialog, Qt::ColorScheme::Dark);
        dialog.apply();
        verifyNativeScheme(Qt::ColorScheme::Dark);
        settlePalette(dialog);
        if (QGuiApplication::platformName() == "cocoa" ||
            QGuiApplication::platformName() == "windows")
            QTRY_VERIFY(QApplication::palette().color(QPalette::Window).lightness() <
                        light.lightness());
        dialog.findChild<QCheckBox *>("showDotsOption")->setChecked(true);
        dialog.apply();
        QtFmOptionsDialog reopened(path(), nullptr, manager, services);
        QCOMPARE(choice(reopened)->currentData().toInt(), int(Qt::ColorScheme::Dark));
        QVERIFY(reopened.findChild<QCheckBox *>("showDotsOption")->isChecked());
        QVERIFY(UiInspection::fullyVisible(*choice(dialog), dialog));
        QVERIFY(UiInspection::writeReport(dialog, "appearance-settings-applied-dark"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(AppearanceSettingsTests)
#include "appearance_settings_tests.moc"
