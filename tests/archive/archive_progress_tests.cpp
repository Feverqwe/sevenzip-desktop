#include "archive/backend/archivebackend.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include "ui/support/layout_inspection.h"
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QtTest>

namespace {
class CControlledTransport : public ArchiveBackend {
  public:
    bool Active = false;
    bool Paused = false;
    bool Cancelled = false;
    bool CancelledWhilePaused = false;
    HRESULT PauseResult = S_OK;
    bool busy() const override { return Active; }
    HRESULT setPaused(bool value) override {
        if (PauseResult == S_OK)
            Paused = value;
        return PauseResult;
    }
    void cancel() override {
        Cancelled = true;
        CancelledWhilePaused = Paused;
    }
    void Start() {
        Active = true;
        emit progress(42, QStringLiteral("папка/файл с пробелом.txt"));
    }
    void Finish() {
        Active = false;
        emit finished(true, QStringLiteral("Готово"));
    }
};
} // namespace

class ArchiveProgressTests : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void confirmingStopCancelsThePausedEngine() {
        CControlledTransport backend;
        CProgressDialog dialog;
        ArchiveProgressTransport transport(dialog, backend);
        QVERIFY(dialog.OnInit());
        dialog.show();
        backend.Start();
        widgetDialogs_.respond(dialog, [this](QDialog &) {
            auto *question = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog());
            QVERIFY(question);
            question->button(QMessageBox::Yes)->click();
        });
        dialog.findChild<QPushButton *>("IDCANCEL")->click();
        QVERIFY(backend.Cancelled);
        QVERIFY(backend.CancelledWhilePaused);
    }

    void unknownTransportProgressClearsThePreviousPhase() {
        CControlledTransport backend;
        CProgressDialog dialog;
        ArchiveProgressTransport transport(dialog, backend);
        dialog.show();
        backend.Start();
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        auto *percent = dialog.findChild<QLabel *>("progressPercent");
        auto *file = dialog.findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
        QTRY_COMPARE(percent->text(), QString("42%"));
        emit backend.progress(-1, QStringLiteral("чтение метаданных 空"));
        QTRY_COMPARE(bar->maximum(), 0);
        QTRY_VERIFY(percent->text().isEmpty());
        emit backend.progress(73, QStringLiteral("запись файла 空"));
        QTRY_COMPARE(percent->text(), QString("73%"));
        QTRY_COMPARE(file->text(), QStringLiteral("запись файла 空"));
        dialog.Sync.Set_NumBytesTotal(100);
        dialog.Sync.Set_NumBytesCur(100);
        emit backend.phaseStarted();
        QTRY_COMPARE(bar->maximum(), 0);
        QTRY_VERIFY(percent->text().isEmpty());
        QTRY_VERIFY(!file->isVisible());
        QVERIFY(file->accessibleDescription().isEmpty());
        auto *status = dialog.findChild<QLabel *>("IDT_PROGRESS_STATUS");
        QCOMPARE(status->text(), QCoreApplication::translate("ProgressRedesign", "Preparing…"));
        emit backend.progress(12, QStringLiteral("новая папка/новый файл 空"));
        QTRY_COMPARE(percent->text(), QString("12%"));
        QCOMPARE(file->text(), QStringLiteral("новый файл 空"));
    }
    void newEngineRetainsPauseOrReportsFailure_data() {
        QTest::addColumn<bool>("failure");
        QTest::newRow("paused") << false;
        QTest::newRow("pause-failure") << true;
    }
    void newEngineRetainsPauseOrReportsFailure() {
        QFETCH(bool, failure);
        CControlledTransport backend;
        CProgressDialog dialog;
        ArchiveProgressTransport transport(dialog, backend);
        backend.Active = true;
        dialog.Sync.Set_Paused(true);
        backend.PauseResult = failure ? E_FAIL : S_OK;
        emit backend.phaseStarted();
        QCOMPARE(backend.Paused, !failure);
        QCOMPARE(backend.Cancelled, failure);
        QCOMPARE(dialog.Sync.Messages.Size(), unsigned(failure ? 1 : 0));
        QVERIFY(dialog.Sync.Get_Paused());
        if (!failure) {
            QCOMPARE(transport.SetPaused(false), S_OK);
            QVERIFY(!backend.Paused);
        }
    }
    void transportControlsAndFinishWhilePaused_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(760, 450);
        QTest::newRow("compact") << QSize(640, 380);
    }
    void transportControlsAndFinishWhilePaused() {
        QFETCH(QSize, size);
        CControlledTransport backend;
        CProgressDialog operation(nullptr, L"Добавление");
        ArchiveProgressTransport transport(operation, backend);
        connect(&backend, &ArchiveBackend::finished, &operation,
                &CProgressDialog::ProcessWasFinished);
        QVERIFY(operation.OnInit());
        operation.resize(size);
        backend.Start();
        unsigned stage = 0;
        bool geometry = false;
        QTimer responder;
        responder.setInterval(5);
        widgetDialogs_.connectResponder(responder, &operation, [&] {
            auto *pause = operation.findChild<QPushButton *>("IDB_PAUSE");
            auto *close = operation.findChild<QPushButton *>("IDCANCEL");
            if (stage == 0 && widgetDialogs_.ready(operation) &&
                operation.findChild<QProgressBar *>("IDC_PROGRESS1")->value() == 42 &&
                operation.windowTitle().contains(QStringLiteral("42%"))) {
                stage = 1;
                QCOMPARE(operation.findChild<QLabel *>("progressPercent")->text(), QString("42%"));
                QVERIFY(!operation.findChild<QPushButton *>("IDB_PROGRESS_BACKGROUND"));
                QCOMPARE(operation.Sync.Messages.Size(), 0u);
                backend.PauseResult = E_FAIL;
                pause->click();
                QVERIFY(!operation.Sync.Get_Paused());
                QVERIFY(!backend.Paused);
                QCOMPARE(pause->text(), QStringLiteral("&Pause"));
                backend.PauseResult = S_OK;
                pause->click();
                QVERIFY(operation.Sync.Get_Paused());
                QVERIFY(backend.Paused);
                // External I/O may finish despite a preceding pause request.
                backend.Finish();
            } else if (stage == 1 && TestProgressDialog::IsCloseButton(close)) {
                stage = 2;
                geometry =
                    operation.size() == size && close->isDefault() &&
                    UiInspection::fullyVisible(*close, operation) && !pause->isVisible() &&
                    UiInspection::writeReport(
                        operation, QString("archive-progress-%1-controls-close").arg(size.width()));
                close->click();
            }
        });
        responder.start();
        operation.exec();
        QCOMPARE(stage, unsigned(2));
        QVERIFY(geometry);
        QVERIFY(!operation.isVisible());
        QCOMPARE(operation.Sync.Messages.Size(), 1u);
        QVERIFY(!backend.busy());
    }
};

QTEST_MAIN(ArchiveProgressTests)
#include "archive_progress_tests.moc"
