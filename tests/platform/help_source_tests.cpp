#include "7zip/MyVersion.h"
#include "archive/codecs/LoadCodecs.h"
#include "dialogs/about/AboutDialog.h"
#include "dialogs/edit/EditDialog.h"
#include "filemanager/HelpUtils.h"
#include "help/qthelpservices.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/color_contrast.h"
#include "ui/support/layout_inspection.h"
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QTimer>
#include <QtTest>

class HelpSourceTests : public QObject {
    Q_OBJECT
    QUrl openedUrl;
  public slots:
    void openUrl(const QUrl &url) { openedUrl = url; }
  private slots:
    void inactiveWindowsUseTheirOwnPaletteGroup() {
        CAboutDialog about;
        QPalette colors = about.palette();
        colors.setColor(QPalette::Active, QPalette::Window, QColor("#f1f2f4"));
        colors.setColor(QPalette::Active, QPalette::WindowText, Qt::black);
        colors.setColor(QPalette::Inactive, QPalette::Window, QColor("#202326"));
        colors.setColor(QPalette::Inactive, QPalette::WindowText, Qt::white);
        about.setPalette(colors);
        about.show();
        about.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&about));
        ShowHelpWindow("start.htm", &about);
        auto *help = about.findChild<QDialog *>("helpWindow");
        help->setPalette(colors);
        help->show();
        help->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(help));
        auto *browser = help->findChild<QTextBrowser *>("helpBrowser");
        const QUrl source = browser->source();
        const QString text = browser->toPlainText();
        QWidget other;
        other.show();
        other.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QVERIFY(!about.isActiveWindow() && !help->isActiveWindow());
        QApplication::processEvents();
        QCOMPARE(browser->source(), source);
        QCOMPARE(browser->toPlainText(), text);
        QCOMPARE(browser->document()->rootFrame()->frameFormat().background().color(),
                 colors.color(QPalette::Inactive, QPalette::Window));
        auto *panel = about.findChild<QWidget *>("aboutUpstream");
        const QPixmap rendered = panel->grab();
        const QColor surface = rendered.toImage().pixelColor(
            qRound(12 * rendered.devicePixelRatio()), qRound(12 * rendered.devicePixelRatio()));
        QVERIFY(textContrast(colors.color(QPalette::Inactive, QPalette::WindowText), surface) >=
                4.5);
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
    void tableParameterNamesStayReadable() {
        QWidget parent;
        ShowHelpWindow("cmdline/switches/method.htm#7Z", &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        const QString parameter = "{N}={MethodID}[:param1][:param2][..]";
        for (QSize size : {QSize(760, 560), QSize(420, 300)}) {
            window->resize(size);
            QApplication::processEvents();
            const auto token = browser->document()->find(parameter);
            QVERIFY(!token.isNull());
            QTextCursor first = token;
            first.setPosition(token.selectionStart());
            QTextCursor last = token;
            last.setPosition(token.selectionEnd() - 1);
            QCOMPARE(browser->cursorRect(first).top(), browser->cursorRect(last).top());
            const auto *table = token.currentTable();
            const auto header = table->cellAt(0, table->columns() - 1);
            first = header.firstCursorPosition();
            last = header.lastCursorPosition();
            last.movePosition(QTextCursor::PreviousCharacter);
            QCOMPARE(browser->cursorRect(first).top(), browser->cursorRect(last).top());
            browser->setTextCursor(last);
            // Qt can publish the document's new scroll range on the next event
            // turn, after laying out the nonbreaking table cells.
            QTRY_VERIFY2((browser->ensureCursorVisible(),
                          browser->cursorRect(last).left() < browser->viewport()->width()),
                         qPrintable(QString("cursor %1, viewport %2, document %3, scroll %4/%5")
                                        .arg(browser->cursorRect(last).left())
                                        .arg(browser->viewport()->width())
                                        .arg(browser->document()->size().width())
                                        .arg(browser->horizontalScrollBar()->value())
                                        .arg(browser->horizontalScrollBar()->maximum())));
            const int horizontal = browser->horizontalScrollBar()->value();
            browser->scrollToAnchor("7Z");
            browser->horizontalScrollBar()->setValue(horizontal);
            QVERIFY(UiInspection::writeReport(
                *window, QString("help-method-table-right-%1").arg(size.width())));
            browser->horizontalScrollBar()->setValue(0);
            browser->scrollToAnchor("7Z");
            QVERIFY(UiInspection::writeReport(*window,
                                              QString("help-method-table-%1").arg(size.width())));
        }
        window->close();
    }
    void commandBlocksPreserveSourceText_data() {
        QTest::addColumn<QString>("topic");
        QTest::newRow("syntax") << "cmdline/syntax.htm";
        QTest::newRow("examples") << "cmdline/commands/add.htm";
        QTest::newRow("nested-examples") << "cmdline/switches/method.htm#SevenZipYV";
        QTest::newRow("indented-examples") << "cmdline/switches/method.htm#MemUse";
    }
    void commandBlocksPreserveSourceText() {
        QFETCH(QString, topic);
        QWidget parent;
        ShowHelpWindow(topic.toUtf8().constData(), &parent);
        auto *browser = parent.findChild<QTextBrowser *>("helpBrowser");
        QTextBrowser reference;
        reference.setSource(browser->source());
        // Presentation must retain the upstream whitespace and line structure,
        // including PRE blocks nested inside table cells and definitions.
        QCOMPARE(browser->toPlainText(), reference.toPlainText());
        auto *window = parent.findChild<QDialog *>("helpWindow");
        for (QSize size : {QSize(760, 560), QSize(420, 300)}) {
            window->resize(size);
            browser->scrollToAnchor(browser->source().fragment());
            const QString needle = topic.contains("SevenZipYV") ? "yv=1600"
                                   : topic.contains("MemUse")   ? "memuse=p60"
                                                                : "";
            if (!needle.isEmpty()) {
                const auto code = browser->document()->find(needle);
                QVERIFY(!code.isNull());
                browser->setTextCursor(code);
                browser->ensureCursorVisible();
                browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->value() +
                                                       browser->cursorRect(code).center().y() -
                                                       browser->viewport()->height() / 2);
                QVERIFY(textContrast(code.charFormat().foreground().color(),
                                     code.blockFormat().background().color()) >= 4.5);
                auto caret = browser->textCursor();
                caret.clearSelection();
                browser->setTextCursor(caret);
            }
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*browser, *window));
            QVERIFY(UiInspection::fullyVisible(*window->findChild<QDialogButtonBox *>(), *window));
            QVERIFY(UiInspection::writeReport(
                *window,
                QString("help-code-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        window->close();
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

    void aboutReportsSourceCodecErrors() {
        CCodecs codecs;
        auto &error = codecs.Errors.AddNew();
        error.Path = FTEXT("unusable plugin");
        error.Message = "the module is not compatible with program";
        UString expected;
        codecs.GetCodecsErrorMessage(expected);
        QVERIFY(!expected.IsEmpty());
        for (const QSize size : {QSize(600, 300), QSize(400, 250)}) {
            QString shown;
            CAboutDialog dialog(nullptr, &codecs);
            dialog.resize(size);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QVERIFY(!qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
            auto *notice = dialog.findChild<QWidget *>("aboutCodecNotice");
            QVERIFY(notice && notice->isVisible());
            dialog.findChild<QScrollArea *>("dialogContentScroll")->ensureWidgetVisible(notice);
            QTRY_VERIFY(UiInspection::fullyVisible(*notice, dialog));
            QTimer answer;
            connect(&answer, &QTimer::timeout, &dialog, [&] {
                for (QWidget *widget : QApplication::topLevelWidgets())
                    if (auto *report = dynamic_cast<CEditDialog *>(widget);
                        report && report->isVisible() && report->isModal()) {
                        answer.stop();
                        shown = report->findChild<QPlainTextEdit *>("editText")->toPlainText();
                        report->reject();
                    }
            });
            answer.start(5);
            dialog.findChild<QPushButton *>("aboutCodecDetails")->click();
            QCOMPARE(shown, QString::fromWCharArray(expected.Ptr()));
            QVERIFY(UiInspection::writeReport(dialog, "about-codec-error-" +
                                                          QString::number(size.width())));
        }
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
        QVERIFY(!dialog.OnButtonClicked(999, nullptr));
        dialog.findChild<QPushButton *>("aboutProjectHomepage")->click();
        QCOMPARE(openedUrl, QUrl("https://github.com/Feverqwe/7zip-qt-port"));
        dialog.findChild<QPushButton *>("aboutHomepage")->click();
        QCOMPARE(openedUrl, QUrl("https://www.7-zip.org/"));
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
        bool licenseShown = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &dialog, [&] {
            auto *licenses = dialog.findChild<QDialog *>("licensesDialog");
            if (licenses && licenses->isVisible() && licenses->isModal()) {
                answer.stop();
                licenseShown = true;
                licenses->reject();
            }
        });
        answer.start(5);
        dialog.findChild<QPushButton *>("aboutLicense")->click();
        QVERIFY(licenseShown);
        QTest::keyClick(&dialog, Qt::Key_F1);
        QVERIFY(!dialog.findChild<QDialog *>("helpWindow"));
    }
    void sourceTopicAndOfflineNavigation() {
        QWidget parent;
        ShowHelpWindow("FM/index.htm", &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        QVERIFY(window);
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(browser);
        auto *back = window->findChild<QPushButton *>("helpBack");
        QVERIFY(!back->isEnabled());
        QCOMPARE(browser->source().path(), QString("/upstream/7zip/Help/fm/index.htm"));
        QVERIFY(browser->toPlainText().contains("File Manager"));
        browser->anchorClicked(QUrl("options.htm#language"));
        QCOMPARE(browser->source().path(), QString("/upstream/7zip/Help/fm/options.htm"));
        QCOMPARE(browser->source().fragment(), QString("language"));
        QVERIFY(browser->toPlainText().contains("Language"));
        QVERIFY(back->isEnabled());
        back->click();
        QCOMPARE(browser->source().path(), QString("/upstream/7zip/Help/fm/index.htm"));
        QVERIFY(!back->isEnabled());
        window->close();
    }
    void firstLinkLoadsDestination_data() {
        QTest::addColumn<QString>("topic");
        QTest::addColumn<QString>("caption");
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("destination");
        QTest::newRow("chapter") << "start.htm" << "General information about 7-Zip"
                                 << "The main features of 7-Zip" << "general/index.htm";
        QTest::newRow("same-directory") << "fm/index.htm" << "Options Dialog Box"
                                        << "Language" << "fm/options.htm";
        QTest::newRow("nested-directory") << "cmdline/index.htm" << "Command Line syntax"
                                          << "expression1" << "cmdline/syntax.htm";
        QTest::newRow("fragment") << "fm/options.htm" << "Language Page"
                                  << "Options Dialog Box" << "fm/options.htm#language";
    }
    void firstLinkLoadsDestination() {
        QFETCH(QString, topic);
        QFETCH(QString, caption);
        QFETCH(QString, content);
        QFETCH(QString, destination);
        QWidget parent;
        ShowHelpWindow(qPrintable(topic), &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const QUrl source = browser->source();
        const QString initialText = browser->toPlainText();
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto link = browser->document()->find(caption);
            QVERIFY(!link.isNull());
            browser->setTextCursor(link);
            browser->ensureCursorVisible();
            QApplication::processEvents();
            link.setPosition(link.selectionStart() + 2);
            const QPoint click = browser->cursorRect(link).center();
            QVERIFY(!browser->anchorAt(click).isEmpty());
            QTest::mouseClick(browser->viewport(), Qt::LeftButton, Qt::NoModifier, click);
            QApplication::processEvents();
            QCOMPARE(browser->source(), QUrl("qrc:/upstream/7zip/Help/" + destination));
            QVERIFY2(browser->toPlainText().contains(content), qPrintable(browser->toPlainText()));
            browser->backward();
            QApplication::processEvents();
            QCOMPARE(browser->source(), source);
            QCOMPARE(browser->toPlainText(), initialText);
        }
        window->close();
    }
    void sourceContextAndLanguageGeometry() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("language.ini");
        QWidget parent;
        ShowHelpWindow("fm/plugins/7-zip/add.htm#options", &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        QVERIFY(window);
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(browser->toPlainText().contains("Add to Archive"));
        const QUrl source = browser->source();
        for (const char *language : {"-", "ru", "-"}) {
            SaveRegLang(UString(language), settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
            QCOMPARE(browser->source(), source);
            QCOMPARE(window->windowTitle(), QString(language) == "ru"
                                                ? QStringLiteral("Справка 7-Zip")
                                                : QString("7-Zip Help"));
            for (QSize size : {QSize(760, 560), QSize(420, 300)}) {
                window->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*browser, *window));
                auto *buttons = window->findChild<QDialogButtonBox *>();
                QVERIFY(UiInspection::fullyVisible(*buttons, *window));
                QVERIFY(UiInspection::writeReport(
                    *window, QString("help-%1-%2").arg(language).arg(size.width())));
            }
        }
        window->close();
    }
    void addHelpCrossChapterLinks_data() {
        QTest::addColumn<QString>("caption");
        QTest::addColumn<QString>("topic");
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("method") << "-m (Method)" << "method.htm"
                                << "Specifies the compression method." << false;
        QTest::newRow("sfx") << "-sfx (Create SFX archive)" << "sfx.htm"
                             << "Creates self extracting archive." << false;
        QTest::newRow("method-keyboard") << "-m (Method)" << "method.htm"
                                         << "Specifies the compression method." << true;
        QTest::newRow("sfx-keyboard") << "-sfx (Create SFX archive)" << "sfx.htm"
                                      << "Creates self extracting archive." << true;
    }
    void addHelpCrossChapterLinks() {
        QFETCH(QString, caption);
        QFETCH(QString, topic);
        QFETCH(QString, content);
        QFETCH(bool, keyboard);
        QWidget parent;
        ShowHelpWindow("fm/plugins/7-zip/add.htm#options", &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        QVERIFY(window);
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        const QUrl source = browser->source();
        const auto link = browser->document()->find(caption);
        QVERIFY(!link.isNull());
        QVERIFY(link.charFormat().isAnchor());
        browser->setTextCursor(link);
        browser->ensureCursorVisible();
        QApplication::processEvents();
        QTextCursor clickCursor = link;
        clickCursor.setPosition(link.selectionStart() + 2);
        const QPoint click = browser->cursorRect(clickCursor).center();
        QVERIFY(!browser->anchorAt(click).isEmpty());
        if (keyboard)
            QTest::keyClick(browser, Qt::Key_Return);
        else
            QTest::mouseClick(browser->viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QCOMPARE(browser->source().path(), "/upstream/7zip/Help/cmdline/switches/" + topic);
        QVERIFY2(browser->toPlainText().contains(content), qPrintable(browser->toPlainText()));
        browser->backward();
        QCOMPARE(browser->source(), source);
        QVERIFY(browser->toPlainText().contains("Add to Archive"));
        browser->forward();
        QVERIFY(browser->toPlainText().contains(content));
        window->close();
    }
    void helpFollowsPaletteWithoutLosingNavigation() {
        const QPalette original = QApplication::palette();
        const auto restore = qScopeGuard([&] { QApplication::setPalette(original); });
        QWidget parent;
        ShowHelpWindow("start.htm", &parent);
        auto *window = parent.findChild<QDialog *>("helpWindow");
        auto *browser = window->findChild<QTextBrowser *>("helpBrowser");
        browser->anchorClicked(QUrl("general/formats.htm#zip"));
        const QUrl source = browser->source();
        const QString text = browser->toPlainText();
        for (const QColor background : {QColor("#202326"), QColor("#f1f2f4"), QColor("#596577"),
                                        QColor("#f0e5d5"), QColor("#202326")}) {
            const bool dark = background.lightnessF() < 0.5;
            QPalette colors = original;
            colors.setColor(QPalette::Base, QColor(dark ? "#202326" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e8eaed" : "#202326"));
            colors.setColor(QPalette::AlternateBase, QColor(dark ? "#2c3035" : "#edf0f3"));
            colors.setColor(QPalette::Link, QColor(dark ? "#8cbcff" : "#205bb0"));
            colors.setColor(QPalette::Window, background);
            colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
            colors.setColor(QPalette::Button, colors.color(QPalette::Window));
            colors.setColor(QPalette::ButtonText, colors.color(QPalette::Text));
            colors.setColor(QPalette::Mid, QColor(dark ? "#50565f" : "#b0b6be"));
            QApplication::processEvents();
            browser->verticalScrollBar()->setValue(40);
            const int scroll = browser->verticalScrollBar()->value();
            QApplication::setPalette(colors);
            QApplication::processEvents();
            QCOMPARE(browser->source(), source);
            QCOMPARE(browser->toPlainText(), text);
            QCOMPARE(browser->verticalScrollBar()->value(), scroll);
            QCOMPARE(browser->document()->rootFrame()->frameFormat().background().color(),
                     window->palette().color(QPalette::Window));
            QCOMPARE(browser->viewport()->palette().color(browser->viewport()->backgroundRole()),
                     window->palette().color(QPalette::Window));
            auto cursor = browser->document()->find("Supported formats");
            QVERIFY(!cursor.isNull());
            QCOMPARE(cursor.charFormat().foreground().color(),
                     window->palette().color(QPalette::WindowText));
            cursor = browser->document()->find("BZIP2");
            QVERIFY(cursor.currentTable());
            QCOMPARE(cursor.currentTable()->cellAt(cursor).format().background().color(),
                     window->palette().color(QPalette::Window));
            cursor = browser->document()->find("Creation");
            const QColor header =
                cursor.currentTable()->cellAt(cursor).format().background().color();
            QVERIFY(header != window->palette().color(QPalette::Window));
            QVERIFY(textContrast(window->palette().color(QPalette::WindowText), header) >= 4.5);
            cursor = browser->document()->find("ZIP", 0, QTextDocument::FindWholeWords);
            QVERIFY(cursor.charFormat().isAnchor());
            QVERIFY(textContrast(cursor.charFormat().foreground().color(),
                                 window->palette().color(QPalette::Window)) >= 4.5);
            browser->verticalScrollBar()->setValue(0);
            for (QSize size : {QSize(760, 560), QSize(420, 300)}) {
                window->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*browser, *window));
                QVERIFY(UiInspection::writeReport(
                    *window, QString("help-theme-%1-%2").arg(dark).arg(size.width())));
                browser->setSource(QUrl("qrc:/upstream/7zip/Help/cmdline/syntax.htm"));
                const auto syntax = browser->document()->find("expression1");
                QVERIFY(!syntax.isNull());
                QVERIFY(syntax.blockFormat().background().color() != background);
                QVERIFY(textContrast(syntax.charFormat().foreground().color(),
                                     syntax.blockFormat().background().color()) >= 4.5);
                QCOMPARE(syntax.charFormat().foreground().color(),
                         window->palette().color(QPalette::WindowText));
                QVERIFY(browser->toPlainText().contains(
                    "expression1 | expression2 | ... | expressionN"));
                browser->verticalScrollBar()->setValue(0);
                QVERIFY(UiInspection::writeReport(
                    *window, QString("help-syntax-%1-%2").arg(dark).arg(size.width())));
                browser->backward();
            }
        }
        browser->backward();
        QVERIFY(browser->toPlainText().contains("Welcome to 7-Zip"));
        browser->forward();
        QCOMPARE(browser->source(), source);
        window->close();
    }
    void unavailableTopicIsObservableAndDoesNotOpenWindow() {
        QTemporaryDir fixture;
        QWidget parent;
        QString warning;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &parent, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warning = box->text();
                box->accept();
            }
        });
        answer.start(5);
        QtOpenHelpTopic("fm/index.htm", &parent, fixture.path());
        QCOMPARE(warning, QString("Help topic is unavailable: fm/index.htm"));
        QVERIFY(!parent.findChild<QDialog *>("helpWindow"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(HelpSourceTests)
#include "help_source_tests.moc"
