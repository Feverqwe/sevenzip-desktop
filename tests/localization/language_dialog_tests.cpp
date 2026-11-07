#include "7zip/UI/FileManager/resource.h"
#include "dialogs/combo/ComboDialog.h"
#include "dialogs/copy/CopyDialog.h"
#include "dialogs/extract/ExtractDialog.h"
#include "dialogs/listview/ListViewDialog.h"
#include "dialogs/mem/MemDialog.h"
#include "dialogs/messages/MessagesDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/ExtractCallback.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {
class LanguageProgressTransport final : public IQtProgressTransport {
  public:
    bool Paused = false;
    int Requests = 0;
    HRESULT SetPaused(bool value) override {
        Paused = value;
        ++Requests;
        return S_OK;
    }
};
class LanguageProgressDialog final : public CProgressDialog {
  public:
    using CProgressDialog::CProgressDialog;
    void attach(IQtProgressTransport &transport) { AttachTransport(&transport); }
};
} // namespace

class LanguageDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir _settings;
    void language(const char *name) {
        const QString path = _settings.filePath("language.ini");
        SaveRegLang(UString(name), path);
        ReloadLang(path);
        QtNotifyLanguageChanged();
        QApplication::processEvents();
    }
    void inspect(QWidget &dialog, const QString &name, int width, int height) {
        const bool wasVisible = dialog.isVisible();
        if (!wasVisible)
            dialog.resize(width, height);
        dialog.show();
        if (QGuiApplication::platformName() != "offscreen") {
            dialog.raise();
            dialog.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        }
        if (wasVisible)
            dialog.resize(width, height);
        QApplication::processEvents();
        // Retain current-build evidence even when a fitting assertion fails.
        QVERIFY(UiInspection::writeReport(dialog, name + '-' + QString::number(width)));
        QTRY_COMPARE(dialog.size(), QSize(width, height).expandedTo(dialog.minimumSizeHint()));
        // Publish the settled geometry after asynchronous native configure events.
        QVERIFY(UiInspection::writeReport(dialog, name + '-' + QString::number(width)));
        for (auto *label : dialog.findChildren<QLabel *>())
            if (label->isVisible() && !label->text().isEmpty()) {
                if (auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
                    scroll && scroll->widget()->isAncestorOf(label))
                    scroll->ensureWidgetVisible(label);
                QTRY_VERIFY2(UiInspection::fullyVisible(*label, dialog),
                             qPrintable(label->objectName()));
                QVERIFY2(UiInspection::labelFits(*label), qPrintable(label->objectName()));
            }
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->isVisible()) {
                if (auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
                    scroll && scroll->widget()->isAncestorOf(button))
                    scroll->ensureWidgetVisible(button);
                QTRY_VERIFY(UiInspection::fullyVisible(*button, dialog));
                // Source Browse controls can be narrower than the style's
                // preferred footer button width. Check the displayed caption.
                QStyleOptionButton option;
                option.initFrom(button);
                option.text = button->text();
                const QRect contents =
                    button->style()->subElementRect(QStyle::SE_PushButtonContents, &option, button);
                QVERIFY2(button->fontMetrics().size(Qt::TextShowMnemonic, button->text()).width() <=
                             contents.width(),
                         qPrintable(button->objectName()));
            }
    }

  private slots:
    void init() { language("-"); }
    void cleanup() { language("-"); }
    void externalReadPhaseUsesSelectedLanguage() {
        for (const char *code : {"ru", "-"}) {
            language(code);
            CProgressDialog progress;
            CExtractCallbackImp callback;
            callback.ProgressDialog = &progress;
            callback.Init();
            QCOMPARE(
                static_cast<IFolderArchiveExtractCallback &>(callback).PrepareOperation(
                    L"Reading %1 空.part", 0, NArchive::NExtract::NAskMode::kReadExternal, nullptr),
                HRESULT(S_OK));
            progress.show();
            QTRY_COMPARE(progress.findChild<QLabel *>("IDT_PROGRESS_STATUS")->text(),
                         QString(code) == "ru" ? QStringLiteral("Чтение") : QString("Reading"));
        }
    }
    void archiveDiagnosticValuesKeepPathsAndSelection() {
        const UString missing(L"Missing volume : 32-bit overflow in headers %1 空.part");
        const UString warning(
            "32-bit overflow in headers 16-bit overflow for number of files in headers");
        UString unknown(L"plugin filename 32-bit overflow in headers %1 空.part");
        unknown.Add_Char(wchar_t(0));
        unknown += L"retained diagnostic tail 空";
        CListViewDialog dialog;
        dialog.NumColumns = 2;
        dialog.DeleteIsAllowed = true;
        dialog.Strings.Add(L"Discard");
        dialog.Values.Add(L"unused");
        dialog.Strings.Add(L"Error");
        dialog.Values.Add(QtArchiveDiagnosticText(missing));
        dialog.QtBindArchiveDiagnosticValue(1, missing);
        dialog.Strings.Add(L"Warning");
        dialog.Values.Add(QtArchiveDiagnosticText(warning));
        dialog.QtBindArchiveDiagnosticValue(2, warning);
        dialog.Strings.Add(L"Plugin");
        dialog.Values.Add(unknown);
        dialog.QtBindArchiveDiagnosticValue(3, unknown);
        dialog.OnInit();
        dialog.show();
        auto *list = dialog.findChild<QTreeWidget *>("propertiesList");
        list->setCurrentItem(list->topLevelItem(0));
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(list->topLevelItemCount(), 3);
        auto *selected = list->topLevelItem(0);
        list->setCurrentItem(selected);
        for (const char *code : {"ru", "-"}) {
            language(code);
            const QString expected =
                QString(code) == "ru"
                    ? QStringLiteral("Отсутствует том: 32-bit overflow in headers %1 空.part")
                    : QStringLiteral("Missing volume: 32-bit overflow in headers %1 空.part");
            QCOMPARE(list->currentItem(), selected);
            QCOMPARE(selected->text(1), expected);
            QCOMPARE(ToQString(dialog.Values[0]), expected);
            QCOMPARE(list->topLevelItem(2)->text(1), ToQString(unknown));
            QCOMPARE(ToQString(dialog.Values[2]), ToQString(unknown));
            QVERIFY(list->topLevelItem(1)->text(1).contains(
                QString(code) == "ru" ? QStringLiteral("16-разрядное переполнение")
                                      : QStringLiteral("16-bit overflow")));
            QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
            QVERIFY(QApplication::clipboard()->text().contains(expected));
            inspect(dialog, QString("language-archive-diagnostic-%1").arg(code), 540, 360);
        }
    }
    void passwordPreservesPendingSecretAndFocus() {
        CPasswordDialog dialog;
        dialog.OnInit();
        dialog.show();
        auto *edit = dialog.findChild<QLineEdit *>("passwordEdit");
        auto *show = dialog.findChild<QCheckBox *>("passwordShow");
        edit->setText(QStringLiteral("密碼 pending secret"));
        show->click();
        edit->setFocus();
        edit->setSelection(2, 4);
        const QString selected = edit->selectedText();
        QApplication::processEvents();
        QCOMPARE(dialog.windowTitle(), QString("Enter password"));
        language("ru");
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Ввод пароля"));
        QCOMPARE(edit->text(), QStringLiteral("密碼 pending secret"));
        QCOMPARE(edit->selectedText(), selected);
        QVERIFY(edit->hasFocus());
        QVERIFY(show->isChecked());
        QCOMPARE(edit->echoMode(), QLineEdit::Normal);
        QVERIFY(!UiInspection::dump(dialog).contains("pending secret"));
        for (int width : {360, 260})
            inspect(dialog, "language-password-ru", width, 170);
        language("-");
        QCOMPARE(dialog.windowTitle(), QString("Enter password"));
        for (int width : {360, 260})
            inspect(dialog, "language-password-en", width, 170);
    }
    void copyAndComboPreserveRawValues() {
        CCopyDialog copy(false, "/initial", {"Unicode file 密碼"});
        CComboDialog folder(QtLangText(IDS_CREATE_FOLDER), QtLangText(IDS_CREATE_FOLDER_NAME),
                            QtLangText(IDS_CREATE_FOLDER_DEFAULT_NAME));
        folder.BindLanguage(IDS_CREATE_FOLDER, IDS_CREATE_FOLDER_NAME);
        auto *destination = copy.findChild<QLineEdit *>("copyDestination");
        auto *name = folder.findChild<QComboBox *>("comboValue");
        destination->setText(QStringLiteral(" /pending path 密碼 "));
        name->setEditText(QStringLiteral(" pending folder 密碼 "));
        language("ru");
        QCOMPARE(copy.windowTitle(), QStringLiteral("Копировать"));
        QCOMPARE(destination->text(), QStringLiteral(" /pending path 密碼 "));
        QCOMPARE(name->currentText(), QStringLiteral(" pending folder 密碼 "));
        for (int width : {480, 300}) {
            inspect(copy, "language-copy-ru", width, 280);
            copy.hide();
            inspect(folder, "language-folder-ru", width, 170);
            folder.hide();
        }
        language("-");
        for (int width : {480, 300}) {
            inspect(copy, "language-copy-en", width, 280);
            copy.hide();
            inspect(folder, "language-folder-en", width, 170);
            folder.hide();
        }
        copy.accept();
        QCOMPARE(ToQString(copy.Value), QStringLiteral(" /pending path 密碼 "));
    }
    void comboActionLanguageAndConfirmation_data() {
        QTest::addColumn<quint32>("titleID");
        QTest::addColumn<quint32>("promptID");
        QTest::addColumn<QString>("english");
        QTest::addColumn<QString>("russian");
        QTest::newRow("folder") << quint32(IDS_CREATE_FOLDER) << quint32(IDS_CREATE_FOLDER_NAME)
                                << QString("Create") << QStringLiteral("Создать");
    }
    void comboActionLanguageAndConfirmation() {
        QFETCH(quint32, titleID);
        QFETCH(quint32, promptID);
        QFETCH(QString, english);
        QFETCH(QString, russian);
        language("-");
        const QString itemTitle = QStringLiteral("файл 空.txt : Comment");
        CComboDialog dialog(titleID ? QtLangText(titleID) : itemTitle, QtLangText(promptID),
                            "initial");
        dialog.BindLanguage(titleID, promptID);
        auto *combo = dialog.findChild<QComboBox *>("comboValue");
        const QString draft = QStringLiteral("  pending 空 value  ");
        combo->setEditText(draft);
        auto *buttons = dialog.findChild<QDialogButtonBox *>("comboButtons");
        auto *accept = buttons->button(QDialogButtonBox::Ok);
        QCOMPARE(accept->text(), english);
        language("ru");
        QCOMPARE(accept->text(), russian);
        QCOMPARE(combo->currentText(), draft);
        if (!titleID)
            QCOMPARE(dialog.windowTitle(), itemTitle);
        for (int width : {360, 260})
            inspect(dialog, QString("combo-action-%1-ru").arg(QTest::currentDataTag()), width, 150);
        language("-");
        QCOMPARE(accept->text(), english);
        QCOMPARE(combo->currentText(), draft);
        for (int width : {360, 260})
            inspect(dialog, QString("combo-action-%1-en").arg(QTest::currentDataTag()), width, 150);
        accept->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), draft);
    }
    void overwriteMemoryPreserveDecisions() {
        COverwriteDialog overwrite;
        overwrite.DefaultButton_is_NO = true;
        overwrite.OldFileInfo.Path = L"/folder 密碼/old.bin";
        overwrite.OldFileInfo.SetSize(12345);
        overwrite.NewFileInfo.Path = L"/folder 密碼/new.bin";
        overwrite.OnInit();
        const QString oldInfo = overwrite.findChild<QLabel *>("overwriteOldFile")->text();
        CMemDialog memory;
        memory.Required_GB = 0;
        memory.FilePath = L"File: %1 файл 空 with spaces.bin";
        memory.OnInit();
        const QString sourceMessage = memory.findChild<QLabel *>("memMessage")->text();
        const QString sourceUnits = memory.findChild<QLabel *>("memGB")->text();
        memory.findChild<QCheckBox *>("memSaveLimit")->click();
        auto *limit = memory.findChild<QLineEdit *>("memLimit");
        limit->setText(" 007 pending ");
        memory.findChild<QRadioButton *>("memSkip")->click();
        memory.findChild<QCheckBox *>("memRemember")->click();
        language("ru");
        QCOMPARE(overwrite.windowTitle(), QStringLiteral("Файл уже существует"));
        QVERIFY(overwrite.findChild<QPushButton *>("overwriteNo")->isDefault());
        QVERIFY(overwrite.findChild<QLabel *>("overwriteOldFile")->text() != oldInfo);
        QVERIFY(overwrite.findChild<QLabel *>("overwriteOldFile")->text().contains("old.bin"));
        QCOMPARE(limit->text(), QString(" 007 pending "));
        QVERIFY(limit->isEnabled());
        QVERIFY(memory.findChild<QRadioButton *>("memSkip")->isChecked());
        QVERIFY(memory.findChild<QCheckBox *>("memRemember")->isChecked());
        QVERIFY(memory.findChild<QLabel *>("memMessage")
                    ->text()
                    .contains(QString::fromUtf8("Файл: File: %1 файл 空 with spaces.bin")));
        QVERIFY(memory.findChild<QLabel *>("memGB")->text().startsWith(QString::fromUtf8("ГБ")));
        for (int width : {700, 560}) {
            inspect(overwrite, "language-overwrite-ru", width, 440);
            inspect(memory, "language-memory-ru", width, 460);
        }
        language("-");
        QCOMPARE(overwrite.findChild<QLabel *>("overwriteOldFile")->text(), oldInfo);
        QCOMPARE(memory.findChild<QLabel *>("memMessage")->text(), sourceMessage);
        QCOMPARE(memory.findChild<QLabel *>("memGB")->text(), sourceUnits);
        for (int width : {700, 560}) {
            inspect(overwrite, "language-overwrite-en", width, 440);
            inspect(memory, "language-memory-en", width, 460);
        }
    }
    void modalMessagesPreserveRowsAcrossLanguageChanges() {
        UStringVector values;
        values.Add(L"message 密碼\nnext row");
        CMessagesDialog messages;
        messages.Messages = &values;
        bool visited = false;
        QTimer::singleShot(0, &messages, [&] {
            const auto close = qScopeGuard([&] { messages.accept(); });
            visited = true;
            auto *list = messages.findChild<QTreeWidget *>("IDL_MESSAGE");
            const int rows = list->topLevelItemCount();
            QCOMPARE(rows, 2);
            for (const char *name : {"ru", "-"}) {
                language(name);
                QCOMPARE(list->topLevelItemCount(), rows);
                QCOMPARE(list->topLevelItem(0)->text(1), QString("message 密碼"));
                QCOMPARE(list->headerItem()->text(1),
                         QString(name) == "ru" ? QStringLiteral("Сообщение") : QString("Message"));
                for (int width : {700, 560})
                    inspect(messages, "language-messages-" + QString(name), width, 400);
            }
        });
        QCOMPARE(messages.Create(), INT_PTR(IDOK));
        QVERIFY(visited);
        QVERIFY(!messages.isVisible());
    }
    void extractPreservesModesHistoryAndSecret() {
        CExtractDialog dialog("/initial path", {"/history"});
        const QString archive = QStringLiteral("/archives/исходный 空.zip");
        dialog.ArcPath = archive.toStdWString().c_str();
        dialog.show();
        dialog.SetInitialOptions(NExtract::NPathMode::kNoPaths, NExtract::NOverwriteMode::kRename,
                                 QStringLiteral("密碼 pending secret"));
        auto *destination = dialog.findChild<QComboBox *>("extractDestination");
        auto *pathMode = dialog.findChild<QComboBox *>("extractPathMode");
        auto *overwriteMode = dialog.findChild<QComboBox *>("extractOverwriteMode");
        auto *name = dialog.findChild<QLineEdit *>("extractPathName");
        dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(true);
        destination->setEditText("/pending path");
        name->setText(QStringLiteral(" pending name 密碼 "));
        QCOMPARE(dialog.windowTitle(), QString("Extract : ") + archive);
        QVERIFY(dialog.findChild<QCheckBox *>("extractElimDup")->isEnabled());
        const int pathIndex = pathMode->currentIndex();
        const int overwriteIndex = overwriteMode->currentIndex();
        const QString englishMode = pathMode->currentText();
        for (const char *lang : {"ru", "-"}) {
            language(lang);
            QCOMPARE(destination->currentText(), QString("/pending path"));
            QCOMPARE(destination->count(), 1);
            QCOMPARE(name->text(), QStringLiteral(" pending name 密碼 "));
            QCOMPARE(dialog.password(), QStringLiteral("密碼 pending secret"));
            QCOMPARE(pathMode->currentIndex(), pathIndex);
            QCOMPARE(overwriteMode->currentIndex(), overwriteIndex);
            if (QString(lang) == "ru") {
                QCOMPARE(dialog.windowTitle(), QStringLiteral("Извлечь : ") + archive);
                QVERIFY(pathMode->currentText() != englishMode);
                QCOMPARE(name->placeholderText(), QStringLiteral("Имя папки назначения"));
            } else {
                QCOMPARE(dialog.windowTitle(), QString("Extract : ") + archive);
                QCOMPARE(pathMode->currentText(), englishMode);
                QCOMPARE(name->placeholderText(), QString("Destination folder name"));
            }
            for (int width : {600, 470})
                inspect(dialog, "language-extract-" + QString(lang), width, 330);
        }
    }
    void progressStopQuestionPreservesItsRequestAcrossLanguages() {
        language("-");
        CProgressDialog dialog(nullptr, L"operation 密碼");
        dialog.show();
        bool inspected = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(question);
            auto *continueButton = question->button(QMessageBox::No);
            const auto resume = qScopeGuard([&] { continueButton->click(); });
            QSignalSpy finished(question, &QDialog::finished);
            for (const char *locale : {"ru", "-"}) {
                language(locale);
                QCOMPARE(QApplication::activeModalWidget(), question);
                QCOMPARE(finished.size(), 0);
                QCOMPARE(question->text(),
                         QCoreApplication::translate("ProgressRedesign", "Stop this operation?"));
                QTRY_COMPARE(question->button(QMessageBox::Yes)->text(),
                             QCoreApplication::translate("ProgressRedesign", "Stop"));
                QCOMPARE(continueButton->text(),
                         QCoreApplication::translate("ProgressRedesign", "Continue working"));
                QCOMPARE(question->defaultButton(), continueButton);
                QCOMPARE(question->escapeButton(), continueButton);
                QVERIFY(UiInspection::fullyVisible(*continueButton, *question));
                QVERIFY(UiInspection::fullyVisible(*question->button(QMessageBox::Yes), *question));
                QVERIFY(UiInspection::writeReport(*question,
                                                  "progress-stop-language-" + QString(locale)));
            }
            inspected = true;
        });
        dialog.findChild<QPushButton *>("IDCANCEL")->click();
        QVERIFY(inspected);
        QVERIFY(!dialog.Sync.Get_Stopped());
        QVERIFY(!dialog.Sync.Get_Paused());
        dialog.done(0);
    }
    void progressPreservesPausedOperationAndFinishedClose() {
        LanguageProgressTransport transport;
        LanguageProgressDialog dialog(nullptr, L"operation 密碼");
        dialog.attach(transport);
        dialog.Sync.Set_NumBytesTotal(100);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(25), S_OK);
        dialog.Sync.AddError_Message(L"diagnostic 密碼");
        dialog.show();
        auto *pause = dialog.findChild<QPushButton *>("IDB_PAUSE");
        auto *cancel = dialog.findChild<QPushButton *>("IDCANCEL");
        QTRY_VERIFY(dialog.windowTitle().startsWith("25% "));
        pause->click();
        QVERIFY(transport.Paused);
        QVERIFY(dialog.Sync.Get_Paused());
        auto *list = dialog.findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES");
        const int rows = list->topLevelItemCount();
        QVERIFY(rows > 0);
        const QString englishPause = pause->text();
        for (const char *lang : {"ru", "-"}) {
            language(lang);
            QCOMPARE(transport.Requests, 1);
            QVERIFY(transport.Paused);
            QVERIFY(dialog.Sync.Get_Paused());
            QCOMPARE(list->topLevelItemCount(), rows);
            QCOMPARE(list->topLevelItem(0)->text(1), QString("diagnostic 密碼"));
            QVERIFY(dialog.windowTitle().endsWith("operation 密碼"));
            QVERIFY(dialog.windowTitle().contains("25%"));
            if (QString(lang) == "ru") {
                QVERIFY(pause->text() != englishPause);
                QVERIFY(dialog.windowTitle().startsWith(QStringLiteral("На паузе ")));
            } else {
                QCOMPARE(pause->text(), englishPause);
                QVERIFY(dialog.windowTitle().startsWith("Paused "));
            }
            for (int width : {760, 640})
                inspect(dialog, "language-progress-" + QString(lang), width,
                        width == 760 ? 450 : 380);
        }
        pause->click();
        QVERIFY(!transport.Paused);
        QVERIFY(!dialog.Sync.Get_Paused());
        dialog.ProcessWasFinished();
        QTRY_COMPARE(cancel->text(), QString("&Close"));
        QVERIFY(!pause->isVisible());
        language("ru");
        QCOMPARE(cancel->text(), QStringLiteral("&Закрыть"));
        QVERIFY(!pause->isVisible());
        QCOMPARE(list->topLevelItemCount(), rows);
        QVERIFY(!dialog.windowTitle().contains('%'));
        QVERIFY(UiInspection::writeReport(dialog, "language-progress-result-ru"));
        language("-");
        QCOMPARE(cancel->text(), QString("&Close"));
        QVERIFY(UiInspection::writeReport(dialog, "language-progress-result-en"));
        cancel->click();
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LanguageDialogTests)
#include "language_dialog_tests.moc"
