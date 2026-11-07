#include "archive/operations/ZipRegistry.h"
#include "archive/resources/virtualextractfilesystem.h"
#include "dialogs/messages/MessagesDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/ExtractCallback.h"
#include "support/overwrite_reply.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

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
    void stoppedMemoryRequestPreservesReports_data() {
        QTest::addColumn<bool>("report");
        QTest::newRow("interactive-request") << false;
        QTest::newRow("diagnostic-report") << true;
    }
    void stoppedMemoryRequestPreservesReports() {
        QFETCH(bool, report);
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        CProgressDialog progress;
        CExtractCallbackImp callback;
        callback.ProgressDialog = &progress;
        callback.SettingsPath = settings.filePath("options.ini");
        callback.Init();
        progress.Sync.Set_Stopped(true);
        bool prompted = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                prompted = true;
                dialog->reject();
            }
        });
        responder.start(5);
        UInt64 allowed = 1ULL << 30;
        UInt32 answer = NRequestMemoryAnswerFlags::k_SkipArc;
        auto &memory = static_cast<IArchiveRequestMemoryUseCallback &>(callback);
        const HRESULT result = memory.RequestMemoryUse(
            report ? NRequestMemoryUseFlags::k_IsReport : 0, NArchive::NEventIndexType::kNoIndex, 0,
            L"данные с пробелом.bin", 2ULL << 30, &allowed, &answer);
        responder.stop();
        QVERIFY(!prompted);
        QCOMPARE(result, report ? S_OK : E_ABORT);
        QCOMPARE(allowed, UInt64(1) << 30);
        QCOMPARE(answer, UInt32(report ? NRequestMemoryAnswerFlags::k_SkipArc
                                       : NRequestMemoryAnswerFlags::k_Stop));
        QCOMPARE(callback.ThereAreMessageErrors, report);
    }
    void overwriteResultsAndDefaults() {
        for (int result : {IDYES, IDNO, IDB_YES_TO_ALL, IDB_NO_TO_ALL, IDB_AUTO_RENAME,
                           NOverwriteDialog::kAutoRenameOnceResult, IDCANCEL}) {
            COverwriteDialog dialog;
            dialog.OnInit();
            dialog.show();
            QVERIFY(dialog.findChild<QPushButton *>("overwriteNo")->isDefault());
            ReplyOverwrite(dialog, result);
            QCOMPARE(dialog.result(), result);
        }
        COverwriteDialog dialog;
        dialog.ShowExtraButtons = false;
        dialog.OnInit();
        dialog.show();
        QVERIFY(!dialog.findChild<QCheckBox *>("overwriteApplyToRemaining")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton *>("overwriteRename")->isEnabled());
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
        QVERIFY(old.contains(name));
        QVERIFY(old.contains(" : 10 GiB"));
        QVERIFY(!dialog.findChild<QLabel *>("overwriteNewFile")->text().contains("GiB"));
    }
    void nativeWriteKeepBothIsOnlyForCurrentConflict() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const QString &name :
             {QString("first.txt"), QString("first_1.txt"), QString("second.txt")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("existing"), qint64(8));
        }
        CProgressDialog progress;
        CMyComPtr2<IFolderArchiveExtractCallback, CExtractCallbackImp> callback;
        callback.Create_if_Empty();
        callback->ProgressDialog = &progress;
        callback->Src_Is_IO_FS_Folder = true;
        callback->OverwriteMode = NExtract::NOverwriteMode::kAsk;
        auto *writer = static_cast<IFolderOperationsExtractCallback *>(callback.operator->());
        int questions = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &progress, [&] {
            auto *dialog = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget());
            if (dialog)
                ReplyOverwrite(*dialog,
                               questions++ == 0 ? NOverwriteDialog::kAutoRenameOnceResult : IDNO);
        });
        responder.start(5);
        for (const QString &name : {QString("first.txt"), QString("second.txt")}) {
            CMyComBSTR chosen;
            Int32 write = 0;
            const std::wstring path = fixture.filePath(name).toStdWString();
            QCOMPARE(writer->AskWrite(L"incoming.txt", false, nullptr, nullptr, path.c_str(),
                                      &chosen, &write),
                     S_OK);
            QCOMPARE(write, name == "first.txt" ? 1 : 0);
            if (write) {
                const QString target = QString::fromWCharArray(chosen);
                QVERIFY(target != fixture.filePath("first.txt"));
                QVERIFY(target != fixture.filePath("first_1.txt"));
                QVERIFY(!QFileInfo::exists(target));
            }
        }
        responder.stop();
        QCOMPARE(questions, 2);
        for (const QString &name :
             {QString("first.txt"), QString("first_1.txt"), QString("second.txt")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("existing"));
        }
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
                         QStringLiteral("папка с пробелом"));
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
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ExtractPromptTests)
#include "extract_prompt_tests.moc"
