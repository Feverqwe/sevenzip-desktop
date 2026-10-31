#include "archive/operations/ZipRegistry.h"
#include "dialogs/mem/MemDialog.h"
#include "dialogs/messages/MessagesDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/ExtractCallback.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

extern bool g_DisableUserQuestions;

class ExtractPromptTests final : public QObject {
    Q_OBJECT
  private slots:
    void virtualFileMemoryAndSpill() {
        for (bool spill : {false, true}) {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            CMyComPtr2<ISequentialOutStream, CVirtFileSystem> system;
            system.Create_if_Empty();
            system->DirPrefix = qUtf8Printable(directory.path() + "/");
            system->MaxTotalAllocSize = spill ? 2 : 1024;
            auto &file = system->Files.AddNew();
            file.BaseName = L"../密碼 file.bin";
            file.WrittenSize = 0;
            file.ExpectedSize = 8;
            const QByteArray first("abcd"), second("efgh");
            UInt32 written = 0;
            QCOMPARE(system.Interface()->Write(first.constData(), first.size(), &written), S_OK);
            QCOMPARE(written, UInt32(4));
            QCOMPARE(system.Interface()->Write(second.constData(), second.size(), &written), S_OK);
            QCOMPARE(written, UInt32(4));
            QCOMPARE(system->CloseMemFile(), S_OK);
            QCOMPARE(system->WasStreamFlushedToFS(), spill);
            if (!spill) {
                QCOMPARE(file.WrittenSize, size_t(8));
                QCOMPARE(
                    QByteArray(reinterpret_cast<const char *>(static_cast<const Byte *>(file.Data)),
                               file.WrittenSize),
                    QByteArray("abcdefgh"));
                QCOMPARE(system->FlushToDisk(true), S_OK);
            }
            QDir dir(directory.path());
            const auto names = dir.entryList(QDir::Files | QDir::Hidden);
            QCOMPARE(names.size(), 1);
            QVERIFY(!names.front().contains('/'));
            QFile output(dir.filePath(names.front()));
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll(), QByteArray("abcdefgh"));
        }
    }
    void extractionCallbackProgressAndLimitSettings() {
        CProgressDialog progress;
        CMyComPtr2<IFolderArchiveExtractCallback, CExtractCallbackImp> callback;
        callback.Create_if_Empty();
        callback->ProgressDialog = &progress;
        callback->Init();
        QCOMPARE(callback.Interface()->SetTotal(100), S_OK);
        UInt64 current = 20;
        QCOMPARE(callback.Interface()->SetCompleted(&current), S_OK);
        progress.Sync.Set_Stopped(true);
        QCOMPARE(callback.Interface()->SetCompleted(&current), E_ABORT);
        QTemporaryDir directory;
        const QString settings = directory.filePath("test.ini");
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(-1));
        NExtract::Save_LimitGB(settings, 3);
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(3));
        callback->SettingsPath = settings;
        CMyComPtr<IArchiveRequestMemoryUseCallback> memory;
        QCOMPARE(callback.Interface()->QueryInterface(IID_IArchiveRequestMemoryUseCallback,
                                                      reinterpret_cast<void **>(&memory)),
                 S_OK);
        UInt64 allowed = 1ULL << 30;
        UInt32 answer = 0;
        QCOMPARE(memory->RequestMemoryUse(0, NArchive::NEventIndexType::kNoIndex, 0, nullptr,
                                          2ULL << 30, &allowed, &answer),
                 S_OK);
        QCOMPARE(allowed, 3ULL << 30);
        QCOMPARE(answer, UInt32(NRequestMemoryAnswerFlags::k_Allow));
    }
    void overwriteResultsAndDefaults() {
        for (const auto &item : {qMakePair("overwriteYes", IDYES), qMakePair("overwriteNo", IDNO),
                                 qMakePair("overwriteYesAll", IDB_YES_TO_ALL),
                                 qMakePair("overwriteNoAll", IDB_NO_TO_ALL),
                                 qMakePair("overwriteRename", IDB_AUTO_RENAME),
                                 qMakePair("overwriteCancel", IDCANCEL)}) {
            COverwriteDialog dialog;
            dialog.DefaultButton_is_NO = true;
            dialog.OnInit();
            dialog.show();
            QVERIFY(dialog.findChild<QPushButton *>("overwriteNo")->isDefault());
            dialog.findChild<QPushButton *>(item.first)->click();
            QCOMPARE(dialog.result(), item.second);
        }
        COverwriteDialog dialog;
        dialog.ShowExtraButtons = false;
        dialog.OnInit();
        dialog.show();
        for (const auto *name : {"overwriteYesAll", "overwriteNoAll", "overwriteRename"})
            QVERIFY(!dialog.findChild<QPushButton *>(name)->isEnabled());
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(dialog.result(), IDCANCEL);
    }
    void overwriteNativeFormatting() {
        COverwriteDialog dialog;
        const QString name = QStringLiteral("密碼 ") + QString(100, QChar('x')) + " ";
        dialog.OldFileInfo.Path = (QString("/temporary folder/") + name).toStdWString().c_str();
        dialog.OldFileInfo.SetSize(10ULL << 30);
        dialog.NewFileInfo.Path = L"new file.bin";
        dialog.OnInit();
        const auto old = dialog.findChild<QLabel *>("overwriteOldFile")->text();
        QVERIFY(old.contains("temporary folder/\n"));
        QVERIFY(old.contains(" ... "));
        QVERIFY(old.contains("\"密碼 "));
        QVERIFY(old.contains(" : 10 GiB"));
        QVERIFY(!dialog.findChild<QLabel *>("overwriteNewFile")->text().contains("GiB"));
    }
    void memoryStateAndCommit() {
        CMemDialog dialog;
        dialog.Required_GB = 0;
        dialog.Limit_GB = 4;
        dialog.ShowRemember = false;
        dialog.OnInit();
        dialog.show();
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-memory-no-remember"));
        auto *edit = dialog.findChild<QLineEdit *>("memLimit");
        auto *save = dialog.findChild<QCheckBox *>("memSaveLimit");
        QVERIFY(!edit->isEnabled());
        QVERIFY(!dialog.findChild<QCheckBox *>("memRemember")->isVisible());
        QVERIFY(dialog.findChild<QRadioButton *>("memAllow")->isChecked());
        save->click();
        QVERIFY(edit->isEnabled());
        edit->setText("0"); // The upstream text parser accepts zero.
        dialog.findChild<QRadioButton *>("memSkip")->click();
        dialog.OnContinue();
        QCOMPARE(dialog.result(), IDCONTINUE);
        QCOMPARE(dialog.Limit_GB, UInt32(0));
        QVERIFY(dialog.NeedSave);
        QVERIFY(dialog.SkipArc);
        CMemDialog cancelled;
        cancelled.OnInit();
        cancelled.findChild<QCheckBox *>("memSaveLimit")->click();
        cancelled.findChild<QLineEdit *>("memLimit")->setText("99");
        cancelled.reject();
        QCOMPARE(cancelled.result(), IDCANCEL);
        QCOMPARE(cancelled.Limit_GB, UInt32(4));
        QVERIFY(!cancelled.NeedSave);
    }
    void memorySpinnerKeysUseTheSourceBuddyEdit_data() {
        QTest::addColumn<bool>("up");
        QTest::newRow("up") << true;
        QTest::newRow("down") << false;
    }
    void memorySpinnerKeysUseTheSourceBuddyEdit() {
        QFETCH(bool, up);
        CMemDialog dialog;
        dialog.OnInit();
        dialog.show();
        auto *edit = dialog.findChild<QLineEdit *>("memLimit");
        const QString disabledValue = edit->text();
        QTest::keyClick(edit, up ? Qt::Key_Up : Qt::Key_Down);
        QCOMPARE(edit->text(), disabledValue);
        dialog.findChild<QCheckBox *>("memSaveLimit")->click();
        auto *button = dialog.findChild<QToolButton *>(up ? "memSpinUp" : "memSpinDown");
        edit->setText("2");
        button->click();
        const QString expected = edit->text();
        edit->setText("2");
        edit->setFocus();
        QTest::keyClick(edit, up ? Qt::Key_Up : Qt::Key_Down);
        QCOMPARE(edit->text(), expected);
        QCOMPARE(button->focusPolicy(), Qt::NoFocus);
    }
    void memoryInvalidInput_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("disabled");
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("trailing") << QString("2x") << false;
        QTest::newRow("over-source-limit") << QString("1073741825") << false;
        QTest::newRow("suppressed-empty") << QString() << true;
    }
    void memoryInvalidInput() {
        QFETCH(QString, text);
        QFETCH(bool, disabled);
        QScopedValueRollback<bool> questions(g_DisableUserQuestions, disabled);
        CMemDialog dialog;
        dialog.OnInit();
        dialog.resize(text.isEmpty() ? QSize(560, 460) : QSize(700, 460));
        dialog.show();
        dialog.findChild<QCheckBox *>("memSaveLimit")->click();
        auto *edit = dialog.findChild<QLineEdit *>("memLimit");
        edit->setText(text);
        bool errorShown = false;
        bool report = false;
        QMessageBox::Icon errorIcon = QMessageBox::NoIcon;
        QString errorTitle;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errorShown = true;
                errorIcon = error->icon();
                errorTitle = error->windowTitle();
                report = UiInspection::writeReport(
                    *error, QString("mem-input-error-%1").arg(QTest::currentDataTag()));
                error->accept();
            }
        });
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(errorShown, !disabled);
        if (errorShown) {
            QCOMPARE(errorIcon, QMessageBox::Critical);
#ifndef Q_OS_MACOS
            QCOMPARE(errorTitle, QString("7-Zip"));
#endif
            QVERIFY(report);
        }
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.result(), 0);
        QCOMPARE(dialog.Limit_GB, UInt32(4));
        QCOMPARE(edit->text(), text);
        QVERIFY(dialog.NeedSave); // Source reads action fields before validating the limit.
        edit->setText("2");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), IDCONTINUE);
        QCOMPARE(dialog.Limit_GB, UInt32(2));
        QVERIFY(!dialog.isVisible());
    }
    void sourceMessagesModalLifetime_data() {
        QTest::addColumn<int>("decision");
        QTest::newRow("close") << 0;
        QTest::newRow("enter") << 1;
        QTest::newRow("escape") << 2;
    }
    void sourceMessagesModalLifetime() {
        QFETCH(int, decision);
        UStringVector lines;
        lines.Add(L"diagnostic 密碼\nnext row\n");
        QWidget owner;
        owner.show();
        CMessagesDialog dialog;
        dialog.Messages = &lines;
        bool visited = false;
        QTimer::singleShot(0, &dialog, [&] {
            const auto close = qScopeGuard([&] {
                if (dialog.isVisible())
                    dialog.reject();
            });
            visited = true;
            QCOMPARE(QApplication::activeModalWidget(), &dialog);
            QCOMPARE(dialog.parentWidget(), &owner);
            QVERIFY(dialog.isVisible());
            auto *list = dialog.findChild<QTreeWidget *>("IDL_MESSAGE");
            QCOMPARE(list->topLevelItemCount(), 3);
            const QStringList expected{QStringLiteral("diagnostic 密碼"), "next row", ""};
            for (int i = 0; i < expected.size(); ++i) {
                QCOMPARE(list->topLevelItem(i)->text(0), QString::number(i));
                QCOMPARE(list->topLevelItem(i)->text(1), expected[i]);
            }
            dialog.resize(decision == 0 ? QSize(760, 450) : QSize(640, 380));
            QApplication::processEvents();
            auto *button = dialog.findChild<QPushButton *>("IDOK");
            QVERIFY(UiInspection::fullyVisible(*list, dialog));
            QVERIFY(UiInspection::fullyVisible(*button, dialog));
            QVERIFY(UiInspection::separate(*list, *button, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog, QString("messages-modal-%1").arg(QTest::currentDataTag())));
            if (decision == 0)
                button->click();
            else
                QTest::keyClick(list, decision == 1 ? Qt::Key_Return : Qt::Key_Escape);
            QVERIFY(!dialog.isVisible());
        });
        const INT_PTR result = dialog.Create(&owner);
        QVERIFY2(visited, "Source Create must retain the stack-owned diagnostic until dismissal");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(result, INT_PTR(decision == 2 ? IDCANCEL : IDOK));
        QCOMPARE(lines.Size(), unsigned(1));
        QCOMPARE(QString::fromWCharArray(lines[0]), QStringLiteral("diagnostic 密碼\nnext row\n"));
    }
    void sourceMessagesRefreshOnEachOpening() {
        UStringVector first, next;
        first.Add(L"old diagnostic");
        next.Add(L"new diagnostic 文件\ncontinued\n");
        const UStringVector *inputs[]{&first, &next, nullptr};
        const QList<QStringList> expected{
            {"old diagnostic"}, {QStringLiteral("new diagnostic 文件"), "continued", ""}, {}};
        CMessagesDialog dialog;
        for (int opening = 0; opening < expected.size(); ++opening) {
            dialog.Messages = inputs[opening];
            bool visited = false;
            QTimer::singleShot(0, &dialog, [&] {
                const auto close = qScopeGuard([&] { dialog.accept(); });
                visited = true;
                auto *list = dialog.findChild<QTreeWidget *>("IDL_MESSAGE");
                QCOMPARE(list->columnCount(), 2);
                QCOMPARE(list->topLevelItemCount(), expected[opening].size());
                for (int row = 0; row < expected[opening].size(); ++row)
                    QCOMPARE(list->topLevelItem(row)->text(1), expected[opening][row]);
            });
            QCOMPARE(dialog.Create(), INT_PTR(IDOK));
            QVERIFY(visited);
        }
    }
    void geometry() {
        for (int width : {700, 560}) {
            {
                COverwriteDialog directory;
                directory.OldFileInfo.Path = L"/назначение/папка с пробелом";
                directory.NewFileInfo.Path = L"/источник/папка с пробелом";
                directory.OldFileInfo.Is_Directory = true;
                directory.NewFileInfo.Is_Directory = true;
                directory.OnInit();
                directory.resize(width, 440);
                directory.show();
                QApplication::processEvents();
                QCOMPARE(directory.findChild<QLabel *>("overwriteHeader")->text(),
                         QStringLiteral("Destination folder already contains processed file."));
                for (auto *label : directory.findChildren<QLabel *>())
                    if (label->isVisible()) {
                        QVERIFY(UiInspection::fullyVisible(*label, directory));
                        QVERIFY(UiInspection::labelFits(*label));
                    }
                QVERIFY(UiInspection::writeReport(directory,
                                                  QString("overwrite-folder-%1").arg(width)));
            }
            for (bool extra : {true, false}) {
                COverwriteDialog dialog;
                dialog.ShowExtraButtons = extra;
                dialog.OldFileInfo.Path = L"/каталог с пробелом/旧文件.bin";
                dialog.NewFileInfo.Path = L"новый файл.bin";
                dialog.OldFileInfo.SetSize(12345);
                dialog.NewFileInfo.SetSize(67890);
                dialog.OnInit();
                dialog.resize(width, 440);
                dialog.show();
                QApplication::processEvents();
                for (auto *label : dialog.findChildren<QLabel *>()) {
                    if (!label->isVisible())
                        continue;
                    QVERIFY(UiInspection::fullyVisible(*label, dialog));
                    QVERIFY(UiInspection::labelFits(*label));
                }
                for (auto *button : dialog.findChildren<QPushButton *>())
                    if (button->isVisible()) {
                        QVERIFY(UiInspection::fullyVisible(*button, dialog));
                        QVERIFY(button->width() >= button->sizeHint().width());
                    }
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("overwrite-%1-%2").arg(width).arg(extra)));
            }
            for (bool change : {false, true}) {
                CMemDialog dialog;
                dialog.Required_GB = 4;
                dialog.ArcPath = L"/каталог с пробелом/archive.7z";
                dialog.FilePath = L"密碼.bin";
                dialog.OnInit();
                dialog.resize(width, 460);
                dialog.show();
                if (change)
                    dialog.findChild<QCheckBox *>("memSaveLimit")->click();
                QApplication::processEvents();
                for (const auto *name :
                     {"memMessage", "memSaveLimit", "memLimit", "memSpin", "memGB", "memAction",
                      "memAllow", "memSkip", "memRemember", "memButtons"}) {
                    auto *control = dialog.findChild<QWidget *>(name);
                    QVERIFY(control);
                    QVERIFY(UiInspection::fullyVisible(*control, dialog));
                    if (qobject_cast<QLabel *>(control))
                        QVERIFY(UiInspection::labelFits(*control));
                    else if (qobject_cast<QAbstractButton *>(control))
                        QVERIFY(control->width() >= control->sizeHint().width());
                }
                QVERIFY(
                    UiInspection::writeReport(dialog, QString("mem-%1-%2").arg(width).arg(change)));
            }
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ExtractPromptTests)
#include "extract_prompt_tests.moc"
