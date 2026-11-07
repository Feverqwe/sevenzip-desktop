#include "dialogs/about/AboutDialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class AboutDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray previousSettings_;
  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void init() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void expandedDetailsRemainReachable() {
        CAboutDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        auto *section = dialog.findChild<QWidget *>("aboutBuildDetails");
        auto *footer = dialog.findChild<QDialogButtonBox *>();
        QTRY_VERIFY(UiInspection::bounds(*footer, dialog).top() -
                        UiInspection::bounds(*section, dialog).bottom() <=
                    dialog.fontMetrics().height() * 3);
        const QSize initialSize = dialog.size();
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            for (const QSize size : {initialSize, QSize(360, 360)}) {
                dialog.resize(size);
                scroll->verticalScrollBar()->setValue(0);
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("about-default-%1-%2").arg(language).arg(size.width())));
            }
        }
        section->findChild<QToolButton *>()->setChecked(true);
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            for (const QSize size : {QSize(560, 600), QSize(360, 360)}) {
                dialog.resize(size);
                for (const char *name : {"aboutIdentity", "aboutProjectHomepage", "aboutUpstream",
                                         "aboutArchitecture", "aboutQtVersion", "aboutRevision"}) {
                    auto *control = dialog.findChild<QWidget *>(name);
                    QVERIFY(control);
                    scroll->ensureWidgetVisible(control);
                    QTRY_VERIFY2(UiInspection::fullyVisible(*control, dialog), name);
                    if (auto *label = qobject_cast<QLabel *>(control))
                        QVERIFY(UiInspection::labelFits(*label));
                    QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QDialogButtonBox *>(),
                                                       dialog));
                }
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("about-details-%1-%2").arg(language).arg(size.width())));
            }
        }
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog.isVisible());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(AboutDialogTests)
#include "about_dialog_tests.moc"
