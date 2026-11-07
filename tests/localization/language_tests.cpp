#include "7zip/UI/Common/ArchiveCommandLine.h"
#include "7zip/UI/Explorer/resource.h"
#include "Windows/ErrorMsg.h"
#include "dialogs/compress/CompressDialog.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/LangPage.h"
#include "settings/RegistryUtils.h"
#include "support/translation_catalog.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/dialogs/qtmodalresult.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <memory>

namespace {
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
} // namespace

class LanguageTests : public QObject {
    Q_OBJECT
  private slots:
    void combinedFilesystemDiagnosticsRemainLocalized() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settings = directory.filePath("diagnostics.ini");
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        const UString source(
            L"SquashFS user or group IDs are unavailable.\n"
            L"The SquashFS file fragment is unavailable. Only the readable prefix is available.\n"
            L"source 空\n");
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QCOMPARE(QString::fromWCharArray(QtArchiveDiagnosticText(source)),
                 QString("ID пользователя или группы SquashFS недоступны.\n"
                         "Фрагмент файла SquashFS недоступен. Доступно только начало файла.\n"
                         "source 空\n"));
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        QCOMPARE(QString::fromWCharArray(QtArchiveDiagnosticText(source)),
                 QString::fromWCharArray(source));
    }
    void resourceParametersAndCardinalForms() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("quantities.ini");
        const QLocale original;
        const auto restore = qScopeGuard([&] {
            QLocale::setDefault(original);
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        QLocale::setDefault(QLocale::c());
        const auto quantity = [](UInt32 id, UInt64 count) {
            return QString::fromWCharArray(MyFormatNew(id, count));
        };
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        QCOMPARE(quantity(3504, 1), QString("1 byte"));
        QCOMPARE(quantity(3504, 2), QString("2 bytes"));
        QCOMPARE(quantity(6105, 1), QString("Are you sure you want to delete this 1 item?"));
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QCOMPARE(quantity(3504, 1), QString::fromUtf8("1 байт"));
        QCOMPARE(quantity(3504, 2), QString::fromUtf8("2 байта"));
        QCOMPARE(quantity(3504, 5), QString::fromUtf8("5 байт"));
        QCOMPARE(quantity(3504, 21), QString::fromUtf8("21 байт"));
        QCOMPARE(quantity(3504, 22), QString::fromUtf8("22 байта"));
        QCOMPARE(quantity(3504, 25), QString::fromUtf8("25 байт"));
        QCOMPARE(quantity(3504, UInt64(1) << 40), QString::fromUtf8("1099511627776 Б"));
        const UString path(L"имя %1 {0} 空.zip");
        const QString text = QString::fromWCharArray(MyFormatNew(3005, path));
        QVERIFY(text.contains(QString::fromWCharArray(path)));
        QVERIFY(!text.contains(QStringLiteral("'{0}'")));
    }
    void memoryPresentationUsesRegionAndCatalog() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settings = directory.filePath("memory-language.ini");
        const QLocale originalLocale;
        const auto restore = qScopeGuard([&] {
            QLocale::setDefault(originalLocale);
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        QCOMPARE(NCompressDialog::AddMemUsage(UInt64(1234) << 20), QString::fromUtf8("1,234 МБ"));
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        QCOMPARE(NCompressDialog::AddMemUsage(UInt64(1234) << 20), QString::fromUtf8("1.234 МБ"));
    }
    void errorFormatterKeepsCodesAndNativeReasons() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settings = directory.filePath("error-language.ini");
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        const auto error = [](DWORD code) {
            return QString::fromWCharArray(NWindows::NError::MyFormatMessage(code));
        };
#ifdef Q_OS_WIN
        const DWORD nativeCode = DWORD(HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
#else
        const DWORD nativeCode = DWORD(HRESULT_FROM_WIN32(EINVAL));
#endif
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        const QString nativeReason = error(nativeCode);
        QVERIFY(!nativeReason.isEmpty());
        QCOMPARE(error(0xe1234567), QString("Error #E1234567"));
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QCOMPARE(error(0xe1234567), QString::fromUtf8("Ошибка № E1234567"));
        QCOMPARE(error(DWORD(MY_HRES_ERROR_INTERNAL_ERROR)),
                 QString::fromUtf8(
                     "Внутренняя ошибка: сбой оборудования (ОЗУ или ЦП), ОС или программы"));
        QCOMPARE(error(nativeCode), nativeReason);
#ifndef Q_OS_WIN
        QCOMPARE(error(DWORD(E_ABORT)), QString::fromUtf8("E_ABORT : Операция отменена"));
#endif
    }
    void upstreamParserErrorKeepsUnicodeArgument_data() {
        QTest::addColumn<QString>("argument");
        QTest::addColumn<QString>("message");
        QTest::newRow("charset") << QStringLiteral("-scsencoding 空 with spaces")
                                 << QStringLiteral(
                                        "Неподдерживаемая кодировка:\nencoding 空 with spaces");
        QTest::newRow("unknown-switch")
            << QStringLiteral("-未知 空") << QStringLiteral("Неизвестный ключ:\n-未知 空");
    }
    void upstreamParserErrorKeepsUnicodeArgument() {
        QFETCH(QString, argument);
        QFETCH(QString, message);
        QTemporaryDir directory;
        const QString settings = directory.filePath("parser-language.ini");
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        CArcCmdLineParser parser;
        CArcCmdLineOptions options;
        UStringVector arguments;
        arguments.Add(L"a");
        arguments.Add(UString(argument.toStdWString().c_str()));
        arguments.Add(L"unused.7z");
        bool caught = false;
        try {
            parser.Parse1(arguments, options);
            parser.Parse2(options);
        } catch (const CArcCmdLineException &error) {
            QCOMPARE(QString::fromWCharArray(error), message);
            caught = true;
        }
        QVERIFY(caught);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void selectionPluralForms_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("caption");
        QTest::newRow("zero") << 0 << QStringLiteral("Выбрано 0 объектов");
        QTest::newRow("one") << 1 << QStringLiteral("Выбран 1 объект");
        QTest::newRow("two") << 2 << QStringLiteral("Выбрано 2 объекта");
        QTest::newRow("five") << 5 << QStringLiteral("Выбрано 5 объектов");
        QTest::newRow("eleven") << 11 << QStringLiteral("Выбрано 11 объектов");
        QTest::newRow("twenty-one") << 21 << QStringLiteral("Выбран 21 объект");
    }
    void selectionPluralForms() {
        QFETCH(int, count);
        QFETCH(QString, caption);
        const QLocale previous;
        const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        QTemporaryDir directory;
        const QString settings = directory.filePath("plural.ini");
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QCOMPARE(QtSelectedItemsText(count), caption);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        QCOMPARE(QtSelectedItemsText(count), QString("Selected objects: %1").arg(count));
    }
    void selectionCountsUseRegionalNumberFormatting() {
        const QLocale previous;
        const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
        for (const auto &locale : {QLocale(QLocale::English, QLocale::UnitedStates),
                                   QLocale(QLocale::German, QLocale::Germany)}) {
            QLocale::setDefault(locale);
            const QString text = QtSelectionStatusText(1234, 5678);
            QVERIFY(text.contains(locale.toString(1234)));
            QVERIFY(text.contains(locale.toString(5678)));
        }
    }
    void standardQtEditMenuFollowsLanguage() {
        QTemporaryDir directory;
        const QString settings = directory.filePath("qt-widgets.ini");
        QLineEdit edit;
        edit.setText("editable text");
        for (const char *code : {"ru", "-"}) {
            SaveRegLang(UString(code), settings);
            ReloadLang(settings);
            const std::unique_ptr<QMenu> menu(edit.createStandardContextMenu());
            bool selectedAll = false;
            const QString caption =
                QByteArray(code) == "ru" ? QStringLiteral("Выделить всё") : QString("Select All");
            for (QAction *action : menu->actions())
                selectedAll |= QString(action->text()).remove('&').section('\t', 0, 0) == caption;
            QVERIFY2(selectedAll, qPrintable(caption));
        }
    }
    void malformedCatalogDoesNotReplaceSourceFallback() {
        QTemporaryDir directory;
        QVERIFY(writeFile(directory.filePath("sevenzip_de.qm"), "not a QM catalog"));
        const QString settings = directory.filePath("invalid.ini");
        SaveRegLang(UString("de"), settings);
        ReloadLang(settings, directory.path());
        QCOMPARE(QString::fromWCharArray(LangString(IDS_CONTEXT_OPEN)), QString("Open archive"));
    }
    void sourceReloadAndFallback() {
        QTemporaryDir directory(QDir::tempPath() + QStringLiteral("/localization язык 空-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString settings = directory.filePath("options.ini");
        QString catalogError;
        QVERIFY2(WriteTranslationCatalog(directory.path(),
                                         {{2322, "translated\nsecond\tfield\\end"}}, &catalogError),
                 qPrintable(catalogError));
        SaveRegLang(UString("de-DE"), settings);
        ReloadLang(settings, directory.path());
        QCOMPARE(QString::fromWCharArray(LangString(IDS_CONTEXT_OPEN)),
                 QString("translated\nsecond\tfield\\end"));
        UString only;
        LangString_OnlyFromLangFile(IDS_CONTEXT_EXTRACT, only);
        QVERIFY(only.IsEmpty());
        QCOMPARE(QString::fromWCharArray(LangString(IDS_CONTEXT_EXTRACT)),
                 QString("Extract files..."));
        SaveRegLang(UString("missing"), settings);
        ReloadLang(settings, directory.path());
        QCOMPARE(QString::fromWCharArray(g_LangID), QString("-"));
        QCOMPARE(QString::fromWCharArray(LangString(IDS_CONTEXT_OPEN)), QString("Open archive"));
        SaveRegLang(UString("../de"), settings);
        ReloadLang(settings, directory.path());
        QCOMPARE(QString::fromWCharArray(g_LangID), QString("-"));
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings, directory.path());
        QCOMPARE(QString::fromWCharArray(g_LangID), QString("-"));
    }
    void sourceCaptionHelpersPreserveMissingResourceAndReload() {
        QTemporaryDir directory;
        const QString settings = directory.filePath("helpers.ini");
        QVERIFY(WriteTranslationCatalog(directory.path(),
                                        {{2322, "Speed::"}, {2323, "Window translated"}}));
        SaveRegLang(UString("de"), settings);
        ReloadLang(settings, directory.path());
        QWidget window;
        QLabel colon(&window), remove(&window), missingColon(&window), missingRemove(&window);
        colon.setProperty("languageColon", true);
        remove.setProperty("languageRemoveColon", true);
        missingColon.setProperty("languageColon", true);
        missingRemove.setProperty("languageRemoveColon", true);
        QtBindResourceText(&window, 2323, "Window resource");
        QtBindResourceText(&colon, 2322, "Colon resource:");
        QtBindResourceText(&remove, 2322, "Remove resource:");
        QtBindResourceText(&missingColon, 2324, "Missing colon:");
        QtBindResourceText(&missingRemove, 2324, "Missing remove:");
        QCOMPARE(window.windowTitle(), QString("Window translated"));
        QCOMPARE(colon.text(), QString("Speed:::"));
        QCOMPARE(remove.text(), QString("Speed:"));
        QCOMPARE(missingColon.text(), QString("Missing colon:"));
        QCOMPARE(missingRemove.text(), QString("Missing remove:"));
        SaveRegLang(UString("missing"), settings);
        ReloadLang(settings, directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(window.windowTitle(), QString("Window resource"));
        QCOMPARE(colon.text(), QString("Colon resource:"));
        QCOMPARE(remove.text(), QString("Remove resource:"));
        QCOMPARE(missingColon.text(), QString("Missing colon:"));
        QCOMPARE(missingRemove.text(), QString("Missing remove:"));
        SaveRegLang(UString("de"), settings);
        ReloadLang(settings, directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(window.windowTitle(), QString("Window translated"));
        QCOMPARE(colon.text(), QString("Speed:::"));
        QCOMPARE(remove.text(), QString("Speed:"));
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void sourceStandardControlBindingsPreserveResourceAndDynamicClose() {
        QTemporaryDir directory;
        const QString settings = directory.filePath("standard.ini");
        QVERIFY(WriteTranslationCatalog(directory.path(), {{402, "Fixture cancel"}}));
        SaveRegLang(UString("de"), settings);
        ReloadLang(settings, directory.path());
        QWidget window;
        QPushButton cancel(&window), help(&window);
        QtBindStandardResourceText(&cancel, IDCANCEL, "Cancel resource");
        QtBindStandardResourceText(&help, IDHELP, "Help resource");
        QCOMPARE(cancel.text(), QString("Fixture cancel"));
        QCOMPARE(help.text(), QString("Help resource"));
        SaveRegLang(UString("missing"), settings);
        ReloadLang(settings, directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(cancel.text(), QString("Cancel resource"));
        QCOMPARE(help.text(), QString("Help resource"));
        SaveRegLang(UString("de"), settings);
        ReloadLang(settings, directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(cancel.text(), QString("Fixture cancel"));
        // Progress rebinds the same native Cancel control to its dynamic Close text.
        QtBindLangText(&cancel, 408, "Close");
        QCOMPARE(cancel.text(), QString("&Close"));
        QtNotifyLanguageChanged();
        QCOMPARE(cancel.text(), QString("&Close"));
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void bundledLanguagePageApplyCancelAndGeometry() {
        QTemporaryDir directory;
        const QString settings = directory.filePath("options.ini");
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        {
            CLangPage cancelled(settings);
            auto *choice = cancelled.findChild<QComboBox *>("languageChoice");
            QCOMPARE(choice->currentData().toString(), QString("-"));
            QVERIFY(choice->itemText(0).contains("English"));
            const int russian = choice->findText(QStringLiteral("Русский"), Qt::MatchContains);
            QVERIFY(russian >= 0);
            choice->setCurrentIndex(russian);
            QVERIFY(cancelled.IsModified());
        }
        UString saved;
        ReadRegLang(saved, settings);
        QCOMPARE(QString::fromWCharArray(saved), QString("-"));
        CLangPage page(settings);
        auto *choice = page.findChild<QComboBox *>("languageChoice");
        const int russian = choice->findText(QStringLiteral("Русский"), Qt::MatchContains);
        QVERIFY(russian >= 0);
        choice->setCurrentIndex(russian);
        QCOMPARE(page.OnApply(), 0L);
        QVERIFY(!page.IsModified());
        QVERIFY(page.LangWasChanged);
        ReadRegLang(saved, settings);
        QCOMPARE(QString::fromWCharArray(saved), QString("ru"));
        QCOMPARE(QString::fromWCharArray(LangString(IDS_CONTEXT_OPEN)),
                 QStringLiteral("Открыть архив"));
        CLangPage restarted(settings);
        QCOMPARE(restarted.findChild<QComboBox *>("languageChoice")->currentText(),
                 choice->currentText());
        page.show();
        for (QSize size : {QSize(600, 320), QSize(480, 280)}) {
            page.resize(size);
            QApplication::processEvents();
            auto *choice = page.findChild<QWidget *>("languageChoice");
            QVERIFY(UiInspection::fullyVisible(*choice, page));
            QVERIFY(UiInspection::writeReport(page, QString("language-page-%1").arg(size.width())));
        }
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
};
QTEST_MAIN(LanguageTests)
#include "language_tests.moc"
