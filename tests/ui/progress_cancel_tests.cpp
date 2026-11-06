#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/qtcallbackprogresstransport.h"
#include "ui/support/layout_inspection.h"
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <atomic>

namespace {
class CWorker : public CProgressThreadVirt {
  public:
    std::atomic<bool> Started{false};
    std::atomic<bool> Finish{false};
    std::atomic<bool> Returned{false};
    bool HonorPauseStop = true;
    bool HoldStopped = false;
    unsigned GuiFinishes = 0;
    HRESULT ProcessVirt() override {
        Sync.Set_NumBytesTotal(100);
        Started = true;
        while (!Finish) {
            if (HonorPauseStop) {
                const HRESULT result = Sync.CheckStop();
                if (result != S_OK) {
                    while (HoldStopped && !Finish)
                        QThread::msleep(5);
                    Returned = true;
                    return result;
                }
            }
            QThread::msleep(5);
        }
        Returned = true;
        return S_OK;
    }
    void ProcessWasFinished_GuiVirt() override { ++GuiFinishes; }
};
QPushButton *Button(CWorker &worker, const char *name) {
    return worker.findChild<QPushButton *>(name);
}
QMessageBox *Question(const WidgetDialogFixture &fixture) {
    auto *box = qobject_cast<QMessageBox *>(fixture.activeDialog());
    return box && box->objectName() == "progressCancelQuestion" ? box : nullptr;
}
void Reply(QMessageBox &box, int answer) {
    if (answer == 0)
        QTest::keyClick(&box, Qt::Key_Escape);
    else
        box.button(QMessageBox::StandardButton(answer))->click();
}
} // namespace

class ProgressCancelTests : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void callbackWorkerHasHonestPauseAndConfirmedCancel() {
        CProgressDialog dialog;
        unsigned cancellations = 0;
        QtCallbackProgressTransport transport([&] { ++cancellations; });
        dialog.AttachTransport(&transport);
        QVERIFY(dialog.OnInit());
        dialog.show();
        auto *pause = dialog.findChild<QPushButton *>("IDB_PAUSE");
        auto *cancel = dialog.findChild<QPushButton *>("IDCANCEL");
        QVERIFY(pause && cancel);
        QVERIFY(!pause->isVisible());
        widgetDialogs_.respond(dialog, [this](QDialog &) {
            auto *question = Question(widgetDialogs_);
            QVERIFY(question);
            question->button(QMessageBox::No)->click();
        });
        cancel->click();
        QCOMPARE(cancellations, 0u);
        QVERIFY(!dialog.Sync.Get_Stopped());
        widgetDialogs_.respond(dialog, [this](QDialog &) {
            auto *question = Question(widgetDialogs_);
            QVERIFY(question);
            question->button(QMessageBox::Yes)->click();
        });
        cancel->click();
        QCOMPARE(cancellations, 1u);
        QVERIFY(dialog.Sync.Get_Stopped());
        dialog.Sync.AddError_Message(L"Unreadable source 空 with spaces");
        dialog.ProcessWasFinished();
        QTRY_COMPARE(cancel->text().remove('&'), QStringLiteral("Close"));
        QVERIFY(dialog.isVisible());
        auto *messages = dialog.findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES");
        QVERIFY(messages);
        QTRY_COMPARE(messages->topLevelItemCount(), 1);
        QVERIFY(messages->topLevelItem(0)->text(1).contains("Unreadable source 空 with spaces"));
        cancel->click();
        QTRY_VERIFY(!dialog.isVisible());
        QCOMPARE(cancellations, 1u);
        dialog.AttachTransport(nullptr);
    }

    void sourcePromptRestoresPauseAndStopsOnlyOnYes_data() {
        QTest::addColumn<bool>("paused");
        QTest::addColumn<int>("answer");
        QTest::addColumn<QSize>("size");
        for (bool paused : {false, true})
            for (int answer : {int(QMessageBox::Yes), int(QMessageBox::No), 0})
                for (int width : {560, 360})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(paused).arg(answer).arg(width)))
                        << paused << answer << QSize(width, width == 560 ? 300 : 240);
    }
    void sourcePromptRestoresPauseAndStopsOnlyOnYes() {
        QFETCH(bool, paused);
        QFETCH(int, answer);
        QFETCH(QSize, size);
        CWorker worker;
        worker.HoldStopped = answer == int(QMessageBox::Yes);
        worker.resize(size);
        worker.Sync.Set_Paused(paused);
        bool clicked = false;
        bool observed = false;
        bool promptPaused = false;
        bool restored = false;
        bool stoppingShown = false;
        bool visibleAfterDenial = false;
        bool parentActive = false;
        bool geometry = false;
        QString promptText;
        QMessageBox::Icon promptIcon = QMessageBox::Critical;
        QTimer responder, trigger;
        widgetDialogs_.connectResponder(responder, &worker, [&] {
            if (worker.Returned && widgetDialogs_.activeDialog() == &worker &&
                TestProgressDialog::IsCloseButton(Button(worker, "IDCANCEL"))) {
                responder.stop();
                QVERIFY(!worker.findChild<QWidget *>("IDC_PROGRESS1")->isVisible());
                Button(worker, "IDCANCEL")->click();
                return;
            }
            if (observed)
                return;
            if (auto *box = Question(widgetDialogs_)) {
                observed = true;
                const bool promptActive = QTest::qWaitForWindowActive(box);
                const bool controlsExposed = QTest::qWaitFor(
                    [&] {
                        return UiInspection::fullyVisible(*box->button(QMessageBox::Yes), *box) &&
                               UiInspection::fullyVisible(*box->button(QMessageBox::No), *box);
                    },
                    1000);
                promptPaused = worker.Sync.Get_Paused();
                promptText = box->text();
                promptIcon = box->icon();
                geometry = parentActive && promptActive && controlsExposed &&
                           worker.size() == size.expandedTo(worker.minimumSizeHint()) &&
                           UiInspection::fullyVisible(*Button(worker, "IDCANCEL"), worker) &&
                           UiInspection::fullyVisible(*box->button(QMessageBox::Yes), *box) &&
                           UiInspection::fullyVisible(*box->button(QMessageBox::No), *box) &&
                           box->buttons().size() == 2 &&
                           box->defaultButton() == box->button(QMessageBox::No) &&
                           box->escapeButton() == box->button(QMessageBox::No) &&
                           !box->button(QMessageBox::Yes)->text().isEmpty() &&
                           !box->button(QMessageBox::No)->text().isEmpty() &&
                           UiInspection::writeReport(*box, QString("progress-cancel-%1-%2-%3")
                                                               .arg(size.width())
                                                               .arg(paused)
                                                               .arg(answer));
                Reply(*box, answer);
            }
        });
        connect(
            &trigger, &QTimer::timeout, &worker,
            [&] {
                if (clicked || !worker.Started || !widgetDialogs_.ready(worker))
                    return;
                clicked = true;
                parentActive = QTest::qWaitForWindowActive(&worker);
                Button(worker, "IDCANCEL")->click();
                restored = worker.Sync.Get_Paused() == paused;
                if (answer == int(QMessageBox::Yes)) {
                    const auto finish = qScopeGuard([&] { worker.Finish = true; });
                    stoppingShown = !Button(worker, "IDCANCEL")->isEnabled() &&
                                    !Button(worker, "IDB_PAUSE")->isVisible();
                    QVERIFY(UiInspection::writeReport(
                        worker, QString("progress-stopping-%1").arg(QTest::currentDataTag())));
                }
                if (answer != int(QMessageBox::Yes)) {
                    visibleAfterDenial = worker.isVisible() && !worker.Sync.Get_Stopped();
                    if (paused)
                        Button(worker, "IDB_PAUSE")->click();
                    worker.Finish = true;
                }
            },
            Qt::QueuedConnection);
        responder.start(5);
        trigger.start(5);
        QCOMPARE(worker.Create(L"Обновление архива с пробелом"), S_OK);
        QVERIFY(observed);
        QVERIFY(promptPaused);
        QVERIFY(restored);
        if (answer == int(QMessageBox::Yes))
            QVERIFY(stoppingShown);
        QVERIFY(geometry);
        QCOMPARE(promptText, QStringLiteral("Stop this operation?"));
        // Original MB_YESNOCANCEL adds no icon flags.
        QCOMPARE(promptIcon, QMessageBox::NoIcon);
        QCOMPARE(worker.Sync.Get_Stopped(), answer == int(QMessageBox::Yes));
        QCOMPARE(worker.Result, answer == int(QMessageBox::Yes) ? E_ABORT : S_OK);
        QCOMPARE(worker.GuiFinishes, 1u);
        QVERIFY(worker.Returned);
        QVERIFY(worker.ThreadFinishedOK);
        QVERIFY(worker.MessagesDisplayed);
        QVERIFY(!worker.isVisible());
        if (answer != int(QMessageBox::Yes))
            QVERIFY(visibleAfterDenial);
    }
    void completionDismissesObsoleteStopQuestion_data() {
        QTest::addColumn<bool>("paused");
        for (bool paused : {false, true})
            QTest::newRow(paused ? "paused" : "running") << paused;
    }
    void completionDismissesObsoleteStopQuestion() {
        QFETCH(bool, paused);
        CWorker worker;
        worker.HonorPauseStop = false;
        worker.Sync.Set_Paused(paused);
        bool clicked = false;
        bool observed = false;
        QPointer<QMessageBox> question;
        QTimer responder, trigger;
        widgetDialogs_.connectResponder(responder, &worker, [&] {
            auto *box = Question(widgetDialogs_);
            if (!box || observed)
                return;
            question = box;
            observed = true;
            // The worker completes while its question is live. No answer is
            // sent: completion must retire the obsolete request itself.
            worker.Finish = true;
        });
        connect(
            &trigger, &QTimer::timeout, &worker,
            [&] {
                if (!clicked && worker.Started && widgetDialogs_.ready(worker)) {
                    clicked = true;
                    Button(worker, "IDCANCEL")->click();
                }
            },
            Qt::QueuedConnection);
        responder.start(5);
        trigger.start(5);
        QCOMPARE(worker.Create(L"Завершение во время вопроса"), S_OK);
        QVERIFY(observed);
        QVERIFY(!question || !question->isVisible());
        QCOMPARE(worker.GuiFinishes, 1u);
        QCOMPARE(worker.Result, S_OK);
        QCOMPARE(worker.Sync.Get_Paused(), paused);
        QVERIFY(!worker.Sync.Get_Stopped());
        QVERIFY(worker.ThreadFinishedOK);
        QVERIFY(worker.MessagesDisplayed);
        QVERIFY(!worker.isVisible());
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ProgressCancelTests)
#include "progress_cancel_tests.moc"
