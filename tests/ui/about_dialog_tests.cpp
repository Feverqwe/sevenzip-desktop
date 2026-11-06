#include "archive/codecs/LoadCodecs.h"
#include "dialogs/about/AboutDialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class AboutDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray previousSettings_;
    std::unique_ptr<QMimeData> savedClipboard_;
  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
        savedClipboard_ = std::make_unique<QMimeData>();
        auto *clipboard = QApplication::clipboard();
        if (clipboard->mimeData())
            for (const auto &format : clipboard->mimeData()->formats())
                savedClipboard_->setData(format, clipboard->mimeData()->data(format));
    }
    void cleanupTestCase() {
        QApplication::clipboard()->setMimeData(savedClipboard_.release());
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void init() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void copiesVisibleBuildAndModuleInformationOnly() {
        const QString secret = "do-not-copy-private-history-or-password";
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("history/extract", secret);
        settings.sync();
        CCodecs codecs;
        auto &error = codecs.Errors.AddNew();
        error.Path = FTEXT("failed module");
        error.Message = "incompatible module";
        UString diagnostic;
        codecs.GetCodecsErrorMessage(diagnostic);
        CAboutDialog dialog(nullptr, &codecs);
        dialog.findChild<QPushButton *>("aboutCopyInformation")->click();
        const QString copied = QApplication::clipboard()->text();
        QVERIFY(
            copied.contains(dialog.findChild<QWidget *>("aboutIdentity")->accessibleDescription()));
        QVERIFY(copied.contains(dialog.findChild<QLabel *>("aboutText101")->text()));
        QVERIFY(copied.contains(dialog.findChild<QLabel *>("aboutText102")->text()));
        for (const char *name : {"aboutArchitecture", "aboutQtVersion", "aboutRevision"}) {
            auto *label = dialog.findChild<QLabel *>(name);
            QVERIFY(label && !label->text().isEmpty());
            QVERIFY(copied.contains(label->text()));
        }
        QVERIFY(copied.contains(QString::fromWCharArray(diagnostic.Ptr())));
        QVERIFY(!copied.contains(secret));
    }
    void externalLinkFailureKeepsExactAddress() {
        QList<QUrl> requested;
        bool succeeds = false;
        CAboutDialog dialog(nullptr, nullptr, [&](const QUrl &url) {
            requested.append(url);
            return succeeds;
        });
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY(requested.isEmpty());
        dialog.findChild<QPushButton *>("aboutProjectHomepage")->click();
        const QString project = "https://github.com/Feverqwe/7zip-qt-port";
        QCOMPARE(requested, QList<QUrl>{QUrl(project)});
        auto *notice = dialog.findChild<QWidget *>("aboutUrlNotice");
        QVERIFY(notice->isVisible());
        QVERIFY(dialog.findChild<QLabel *>("aboutUrlMessage")->text().contains(project));
        dialog.findChild<QPushButton *>("aboutCopyUrl")->click();
        QCOMPARE(QApplication::clipboard()->text(), project);
        SaveRegLang(UString("ru"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtNotifyLanguageChanged();
        QVERIFY(dialog.findChild<QLabel *>("aboutUrlMessage")->text().contains(project));
        succeeds = true;
        dialog.findChild<QPushButton *>("aboutHomepage")->click();
        QCOMPARE(requested.last(), QUrl("https://www.7-zip.org/"));
        QVERIFY(!notice->isVisible());
    }
    void expandedDetailsRemainReachable() {
        CAboutDialog dialog(nullptr, nullptr, [](const QUrl &) { return false; });
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
        dialog.findChild<QPushButton *>("aboutProjectHomepage")->click();
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            for (const QSize size : {QSize(560, 600), QSize(360, 360)}) {
                dialog.resize(size);
                for (const char *name :
                     {"aboutIdentity", "aboutLicense", "aboutUpstream", "aboutArchitecture",
                      "aboutQtVersion", "aboutRevision", "aboutUrlMessage"}) {
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
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(AboutDialogTests)
#include "about_dialog_tests.moc"
