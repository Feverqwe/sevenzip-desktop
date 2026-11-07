#include "7zip/MyVersion.h"
#include "dialogs/about/AboutDialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/color_contrast.h"
#include "ui/support/layout_inspection.h"
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class AboutDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray previousSettings_;
    QUrl openedUrl;
  public slots:
    void openUrl(const QUrl &url) { openedUrl = url; }
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
    void aboutPaletteChangesRestyleOpenDialog() {
        const QPalette original = QApplication::palette();
        const auto restore = qScopeGuard([&] { QApplication::setPalette(original); });
        CAboutDialog dialog;
        dialog.show();
        auto *attribution = dialog.findChild<QWidget *>("aboutUpstream");
        const auto sampleSurface = [&] {
            const QPixmap rendered = attribution->grab();
            return rendered.toImage().pixelColor(qRound(12 * rendered.devicePixelRatio()),
                                                 qRound(12 * rendered.devicePixelRatio()));
        };
        QColor previous;
        QColor previousWindow;
        for (const QColor window :
             {QColor("#f1f2f4"), QColor("#202326"), QColor("#596577"), QColor("#f0e5d5")}) {
            QPalette colors = original;
            colors.setColor(QPalette::Window, window);
            colors.setColor(QPalette::WindowText,
                            window.lightnessF() < 0.5 ? Qt::white : Qt::black);
            // Deliberately unsuitable on some surfaces: the link tone must adapt.
            colors.setColor(QPalette::Link, QColor("#34518c"));
            QApplication::setPalette(colors);
            QApplication::processEvents();
            const QColor actualWindow = dialog.palette().color(QPalette::Window);
            if (previousWindow.isValid() && previousWindow != actualWindow) {
                QTRY_VERIFY2(sampleSurface() != previous,
                             qPrintable(QString("window=%1 parent=%2 style=%3")
                                            .arg(actualWindow.name(),
                                                 attribution->parentWidget()
                                                     ->palette()
                                                     .color(QPalette::Window)
                                                     .name(),
                                                 attribution->styleSheet())));
            }
            const QColor surface = sampleSurface();
            QVERIFY(surface != actualWindow);
            previous = surface;
            previousWindow = actualWindow;
            for (auto *label : attribution->findChildren<QLabel *>())
                QVERIFY(textContrast(label->palette().color(QPalette::WindowText), surface) >= 4.5);
            for (auto *button : dialog.findChildren<QPushButton *>()) {
                if (button->isFlat())
                    QVERIFY(textContrast(button->palette().color(QPalette::ButtonText),
                                         button->parentWidget() == attribution ? surface
                                                                               : window) >= 4.5);
            }
        }
    }
    void aboutInitialFrameScalesWithFont() {
        const QFont originalFont = QApplication::font();
        const auto restoreFont = qScopeGuard([&] { QApplication::setFont(originalFont); });
        for (int points : {8, 12}) {
            QFont font = originalFont;
            font.setPointSize(points);
            QApplication::setFont(font);
            CAboutDialog dialog;
            dialog.show();
            QApplication::processEvents();
            for (auto *label : dialog.findChildren<QLabel *>()) {
                if (!label->isVisible())
                    continue;
                auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
                scroll->ensureWidgetVisible(label);
                QTRY_VERIFY(UiInspection::fullyVisible(*label, dialog));
                QVERIFY2(UiInspection::labelFits(*label), qPrintable(label->objectName()));
            }
            QVERIFY(UiInspection::writeReport(dialog, QString("about-source-font-%1").arg(points)));
            dialog.reject();
        }
    }
    void aboutInitialReturnClosesSourceDialog() {
        CAboutDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->hasFocus());
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QTest::keyClick(&dialog, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
        QVERIFY(!dialog.isVisible());
        QVERIFY(!dialog.findChild<QDialog *>("helpWindow"));
    }

    void aboutIdentityAttributionLinksAndGeometry_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("systemPalette");
        QTest::newRow("system") << false << true;
        QTest::newRow("light") << false << false;
        QTest::newRow("dark") << true << false;
    }
    void aboutIdentityAttributionLinksAndGeometry() {
        QFETCH(bool, dark);
        QFETCH(bool, systemPalette);
        const QPalette original = QApplication::palette();
        const auto restorePalette = qScopeGuard([&] { QApplication::setPalette(original); });
        QPalette colors = original;
        colors.setColor(QPalette::Window, QColor(dark ? "#272b30" : "#f1f2f4"));
        colors.setColor(QPalette::WindowText, QColor(dark ? "#e8eaed" : "#202326"));
        // Native macOS can supply a bright AlternateBase in dark appearance.
        colors.setColor(QPalette::AlternateBase, QColor(dark ? "#8e8e8e" : "#ffffff"));
        colors.setColor(QPalette::Button, colors.color(QPalette::Window));
        colors.setColor(QPalette::ButtonText, colors.color(QPalette::WindowText));
        colors.setColor(QPalette::Link, QColor(dark ? "#8cbcff" : "#205bb0"));
        if (!systemPalette)
            QApplication::setPalette(colors);
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("language.ini");
        CAboutDialog dialog;
        dialog.show();
        auto *version = dialog.findChild<QLabel *>("aboutText101");
        QVERIFY(version->text().contains("7-Zip " MY_VERSION));
        QVERIFY(dialog.findChild<QLabel *>("aboutText102")->text().contains(MY_DATE));
        QVERIFY(
            dialog.findChild<QLabel *>("aboutUpstreamCopyright")->text().contains("Igor Pavlov"));
        auto *identity = dialog.findChild<QWidget *>("aboutIdentity");
        QVERIFY(identity->accessibleDescription().contains(QString(SEVENZIP_DESKTOP_VERSION)));
        QDesktopServices::setUrlHandler("https", this, "openUrl");
        const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
        dialog.findChild<QPushButton *>("aboutProjectHomepage")->click();
        QCOMPARE(openedUrl, QUrl("https://github.com/Feverqwe/7zip-qt-port"));
        for (const char *language : {"-", "ru", "-"}) {
            SaveRegLang(UString(language), settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
            QCOMPARE(
                dialog.windowTitle(),
                QString(language == QByteArray("ru") ? "О программе Sevenzip" : "About Sevenzip"));
            QVERIFY(version->text().contains("7-Zip " MY_VERSION));
            QVERIFY(identity->accessibleDescription().contains(QString(SEVENZIP_DESKTOP_VERSION)));
            for (QSize size : {QSize(360, 330), QSize(300, 310)}) {
                dialog.resize(size);
                for (auto *label : dialog.findChildren<QLabel *>()) {
                    if (!label->isVisible())
                        continue;
                    dialog.findChild<QScrollArea *>("dialogContentScroll")
                        ->ensureWidgetVisible(label);
                    // Language changes can queue a second layout pass for the
                    // upstream group's newly wrapped free-software notice.
                    QTRY_VERIFY(UiInspection::fullyVisible(*label, dialog));
                    QTRY_VERIFY2(UiInspection::labelFits(*label), qPrintable(label->objectName()));
                }
                for (auto *button : dialog.findChildren<QPushButton *>()) {
                    if (!button->isVisible())
                        continue;
                    auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
                    if (scroll->widget()->isAncestorOf(button))
                        scroll->ensureWidgetVisible(button);
                    QTRY_VERIFY(UiInspection::fullyVisible(*button, dialog));
                    const auto captionFits = [button] {
                        QStyleOptionButton option;
                        option.initFrom(button);
                        option.text = button->text();
                        const QRect contents = button->style()->subElementRect(
                            QStyle::SE_PushButtonContents, &option, button);
                        return button->fontMetrics()
                                   .size(Qt::TextShowMnemonic, button->text())
                                   .width() <= contents.width();
                    };
                    QTRY_VERIFY2(captionFits(), qPrintable(button->objectName()));
                }
                QVERIFY(UiInspection::bounds(*identity, dialog).bottom() <
                        UiInspection::bounds(*version, dialog).top());
                auto *attribution = dialog.findChild<QWidget *>("aboutUpstream");
                dialog.findChild<QScrollArea *>("dialogContentScroll")
                    ->ensureWidgetVisible(attribution);
                QTRY_VERIFY(UiInspection::fullyVisible(*attribution, dialog));
                const QPixmap rendered = dialog.grab();
                const QImage surface = rendered.toImage();
                // Sample the empty top padding inside the rounded panel so
                // contrast includes the stylesheet's painted background.
                const QPoint sample = attribution->mapTo(&dialog, QPoint(12, 12));
                const QColor background =
                    surface.pixelColor(qRound(sample.x() * rendered.devicePixelRatio()),
                                       qRound(sample.y() * rendered.devicePixelRatio()));
                for (auto *label : attribution->findChildren<QLabel *>())
                    QVERIFY2(
                        textContrast(label->palette().color(QPalette::WindowText), background) >=
                            4.5,
                        qPrintable(QString("label=%1 text=%2 surface=%3 window=%4 style=%5")
                                       .arg(label->objectName(),
                                            label->palette().color(QPalette::WindowText).name(),
                                            background.name(),
                                            dialog.palette().color(QPalette::Window).name(),
                                            attribution->styleSheet())));
                QVERIFY(UiInspection::writeReport(dialog, QString("about-%1-%2-%3")
                                                              .arg(language)
                                                              .arg(size.width())
                                                              .arg(QTest::currentDataTag())));
            }
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(AboutDialogTests)
#include "about_dialog_tests.moc"
