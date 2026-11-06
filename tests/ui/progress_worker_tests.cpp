#include "Common/StringConvert.h"
#include "support/progress_dialog.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <stdexcept>

namespace {
class CShowObserver : public QObject {
  public:
    unsigned Shows = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Show)
            ++Shows;
        return false;
    }
};
class CWorker : public CProgressThreadVirt {
  public:
    int Mode = 0;
    bool RequestCreation = false;
    bool GuiFinished = false;
    bool NativeWorker = false;
    bool GuiHookOnOwnerThread = false;
    unsigned FinishCalls = 0;
    HRESULT ProcessVirt() override {
        NativeWorker = QThread::currentThread() != thread();
        if (RequestCreation)
            WaitCreating();
        switch (Mode) {
        case 1:
            return E_ABORT;
        case 2:
            return E_FAIL;
        case 3:
            throw L"Ошибка wide <файл с пробелом>";
        case 4:
            throw UString(L"Ошибка Unicode");
        case 5:
            throw "Error narrow";
        case 6:
            throw 17;
        case 7:
            throw std::runtime_error("source generic exception");
        case 8:
            Sync.AddError_Message(L"Ошибка файла с пробелом\nПодробность");
            return S_OK;
        case 9:
            GetMessagePair(true).Message = L"Итоговая ошибка";
            GetMessagePair(true).Title = L"Исходный title не копируется";
            for (unsigned i = 0; i < 40; ++i) {
                UString path = L"путь с пробелом ";
                path.Add_UInt32(i);
                AddErrorPath(us2fs(path));
            }
            return S_OK;
        case 10:
            GetMessagePair(false).Title = L"Готово";
            GetMessagePair(false).Message = L"Архив с пробелом <готов>";
            return S_OK;
        case 11:
            QThread::msleep(650);
            WaitCreating();
            Sync.Set_NumBytesTotal(100);
            return Sync.Set_NumBytesCur(100);
        case 12:
            FinalMessage.Partial = true;
            return S_OK;
        case 13:
            FinalMessage.HadErrors = true;
            Sync.AddError_Message(L"callback error <file with spaces>");
            return S_OK;
        }
        return S_OK;
    }
    void ProcessWasFinished_GuiVirt() override {
        GuiFinished = true;
        GuiHookOnOwnerThread = QThread::currentThread() == thread();
        ++FinishCalls;
    }
};
} // namespace

class ProgressWorkerTests : public QObject {
    Q_OBJECT
  private slots:
    void sourceResultAndExceptionComposition_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<HRESULT>("result");
        QTest::addColumn<QString>("message");
        QTest::newRow("success") << 0 << HRESULT(S_OK) << QString();
        QTest::newRow("abort-result") << 1 << HRESULT(E_ABORT) << QString();
        QTest::newRow("native-failure")
            << 2 << HRESULT(E_FAIL) << QString::fromWCharArray(HResultToMessage(E_FAIL).Ptr());
        QTest::newRow("wide-exception")
            << 3 << HRESULT(E_FAIL) << QStringLiteral("Ошибка wide <файл с пробелом>");
        QTest::newRow("native-string-exception")
            << 4 << HRESULT(E_FAIL) << QStringLiteral("Ошибка Unicode");
        QTest::newRow("narrow-exception") << 5 << HRESULT(E_FAIL) << QStringLiteral("Error narrow");
        QTest::newRow("integer-exception") << 6 << HRESULT(E_FAIL) << QStringLiteral("Error #17");
        QTest::newRow("generic-exception") << 7 << HRESULT(E_FAIL) << QStringLiteral("Error");
    }
    void sourceResultAndExceptionComposition() {
        QFETCH(int, mode);
        QFETCH(HRESULT, result);
        QFETCH(QString, message);
        CWorker worker;
        worker.Mode = mode;
        worker.DisableUserQuestions = true;
        QCOMPARE(worker.Create(L"Проверка архива"), S_OK);
        QVERIFY(worker.NativeWorker);
        QVERIFY(worker.ThreadFinishedOK);
        QVERIFY(worker.GuiFinished);
        QVERIFY(worker.GuiHookOnOwnerThread);
        QCOMPARE(worker.FinishCalls, 1u);
        QCOMPARE(worker.Result, result);
        QCOMPARE(QString::fromWCharArray(worker.Sync.FinalMessage.ErrorMessage.Message.Ptr()),
                 message);
        QVERIFY(worker.MessagesDisplayed);
        QVERIFY(!worker.isVisible());
    }
    void waitModeFastRequestedAndDelayed_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("request");
        QTest::addColumn<bool>("visible");
        QTest::newRow("fast-complete-no-dialog") << 0 << false << false;
        QTest::newRow("worker-requests-creation") << 0 << true << true;
        QTest::newRow("timeout-then-finish") << 11 << false << true;
        QTest::newRow("fast-error-needs-dialog") << 3 << false << true;
    }
    void waitModeFastRequestedAndDelayed() {
        QFETCH(int, mode);
        QFETCH(bool, request);
        QFETCH(bool, visible);
        QWidget parent;
        CWorker worker;
        worker.Mode = mode;
        worker.RequestCreation = request;
        worker.WaitMode = true;
        worker.DisableUserQuestions = true;
        CShowObserver observer;
        worker.installEventFilter(&observer);
        QCOMPARE(worker.Create(L"Wait mode", &parent), S_OK);
        QCOMPARE(worker.parentWidget(), visible ? &parent : nullptr);
        QVERIFY(worker.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(worker.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!worker.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
        QVERIFY(worker.NativeWorker);
        QVERIFY(worker.ThreadFinishedOK);
        QCOMPARE(observer.Shows > 0, visible);
        QCOMPARE(worker.GuiFinished, visible);
        QVERIFY(!worker.isVisible());
        QVERIFY(QApplication::overrideCursor() == nullptr);
    }
    void sourceFinalErrorPathsAndSuccessMessage() {
        CWorker failed;
        failed.Mode = 9;
        failed.DisableUserQuestions = true;
        QCOMPARE(failed.Create(L"Итог"), S_OK);
        QCOMPARE(failed.Result, E_FAIL);
        QStringList expected{QStringLiteral("Итоговая ошибка")};
        for (unsigned i = 0; i < 32; ++i)
            expected.append(QStringLiteral("путь с пробелом %1").arg(i));
        QCOMPARE(QString::fromWCharArray(failed.Sync.FinalMessage.ErrorMessage.Message.Ptr()),
                 expected.join('\n'));
        QVERIFY(failed.Sync.FinalMessage.ErrorMessage.Title.IsEmpty());
        CWorker success;
        success.Mode = 10;
        success.DisableUserQuestions = true;
        QCOMPARE(success.Create(L"Итог"), S_OK);
        QCOMPARE(success.Result, S_OK);
        QCOMPARE(QString::fromWCharArray(success.Sync.FinalMessage.OkMessage.Title.Ptr()),
                 QStringLiteral("Готово"));
        QCOMPARE(QString::fromWCharArray(success.Sync.FinalMessage.OkMessage.Message.Ptr()),
                 QStringLiteral("Архив с пробелом <готов>"));
    }
    void sourceErrorListCloseState_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(760, 450);
        QTest::newRow("compact") << QSize(640, 380);
    }
    void sourceErrorListCloseState() {
        QFETCH(QSize, size);
        CWorker worker;
        worker.Mode = 8;
        worker.resize(size);
        bool inspected = false;
        bool geometry = false;
        bool closeDefault = false;
        QString closeState;
        int focusAttempts = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &worker, [&] {
            auto *close = worker.findChild<QPushButton *>("IDCANCEL");
            if (!TestProgressDialog::IsCloseButton(close))
                return;
            // A worker can finish before the modal window's activation event.
            // Observe its settled focus before sending Enter; keep a bounded
            // failure path so a real focus regression still closes and fails.
            if (!close->hasFocus() && ++focusAttempts < 50)
                return;
            inspected = true;
            closeDefault = close->isEnabled() && close->isDefault() && close->hasFocus();
            closeState = QString("enabled=%1 default=%2 focus=%3 owner=%4")
                             .arg(close->isEnabled())
                             .arg(close->isDefault())
                             .arg(close->hasFocus())
                             .arg(worker.focusWidget() ? worker.focusWidget()->objectName()
                                                       : QString("none"));
            const bool report = UiInspection::writeReport(
                worker, QString("progress-worker-%1-errors-close").arg(size.width()));
            geometry = worker.size() == size.expandedTo(worker.minimumSizeHint()) &&
                       UiInspection::fullyVisible(*close, worker) &&
                       close->sizeHint().width() <= close->width() &&
                       !worker.findChild<QPushButton *>("IDB_PAUSE")->isVisible() &&
                       worker.findChild<QWidget *>("progressDiagnostics")->isVisible() && report;
            QTest::keyClick(close, Qt::Key_Return);
        });
        timer.start(10);
        QCOMPARE(worker.Create(L"Ошибки обновления"), S_OK);
        QVERIFY(inspected);
        QVERIFY(geometry);
        QVERIFY2(closeDefault, qPrintable(closeState));
        QVERIFY(worker.ThreadFinishedOK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(worker.MessagesDisplayed);
        QCOMPARE(worker.result(), 8); // Original IDCLOSE.
        QCOMPARE(worker.Sync.Messages.Size(), 1u);
    }
    void finalOutcomeRemainsVisibleOnlyWhenNeeded_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("retained");
        QTest::newRow("error") << 3 << QStringLiteral("Ошибка wide <файл с пробелом>") << true;
        QTest::newRow("cancelled") << 1 << QString() << true;
        QTest::newRow("skipped") << 12 << QString() << true;
        QTest::newRow("callback-error")
            << 13 << QString("callback error <file with spaces>") << true;
        QTest::newRow("success") << 10 << QString() << false;
    }
    void finalOutcomeRemainsVisibleOnlyWhenNeeded() {
        QFETCH(int, mode);
        QFETCH(QString, message);
        QFETCH(bool, retained);
        CWorker worker;
        worker.Mode = mode;
        bool observed = false;
        bool geometry = true;
        QString actualMessage;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &worker, [&] {
            auto *close = worker.findChild<QPushButton *>("IDCANCEL");
            if (worker.isVisible() && TestProgressDialog::IsCloseButton(close)) {
                timer.stop();
                const auto dismiss = qScopeGuard([&] { close->click(); });
                observed = true;
                const char *summary = mode == 1
                                          ? "Operation cancelled. Some changes may have completed."
                                      : mode == 12 ? "Completed with skipped items or messages"
                                                   : "Operation failed";
                QCOMPARE(worker.findChild<QLabel *>("IDT_PROGRESS_STATUS")->text(),
                         QCoreApplication::translate("ProgressRedesign", summary));
                QVERIFY(!worker.findChild<QWidget *>("IDC_PROGRESS1")->isVisible());
                auto *diagnostics = dynamic_cast<QtDisclosure *>(
                    worker.findChild<QWidget *>("progressDiagnostics"));
                diagnostics->setExpanded(true);
                actualMessage = TestProgressDialog::ReportText(worker);
                for (const QSize size : {QSize(560, 260), QSize(360, 240)}) {
                    worker.resize(size);
                    worker.layout()->activate();
                    QTRY_VERIFY(UiInspection::fullyVisible(*close, worker));
                    geometry &=
                        UiInspection::writeReport(worker, QString("progress-final-message-%1-%2")
                                                              .arg(QTest::currentDataTag())
                                                              .arg(size.width()));
                }
            }
        });
        timer.start(10);
        QCOMPARE(worker.Create(L"Итоговый результат"), S_OK);
        QCOMPARE(observed, retained);
        QCOMPARE(actualMessage, message);
        QVERIFY(geometry);
        QVERIFY(worker.ThreadFinishedOK);
        QVERIFY(worker.MessagesDisplayed);
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ProgressWorkerTests)
#include "progress_worker_tests.moc"
