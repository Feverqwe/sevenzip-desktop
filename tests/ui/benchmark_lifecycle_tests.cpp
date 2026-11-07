#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "benchmark/BenchmarkDialog.h"
#include "benchmark/qtbenchmarkcontrols.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/benchmark_services_fixture.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QtTest>
#include <memory>

class BenchmarkLifecycleTests final : public QObject {
    Q_OBJECT
    static CObjectVector<CProperty> properties(bool total = false) {
        CObjectVector<CProperty> props;
        for (const auto &pair : {qMakePair(L"mt", L"1"), qMakePair(L"d18", L"")}) {
            CProperty &prop = props.AddNew();
            prop.Name = pair.first;
            prop.Value = pair.second;
        }
        if (total) {
            CProperty &prop = props.AddNew();
            prop.Name = "m";
            prop.Value = "*";
        }
        return props;
    }
    static QDialog *dialog() {
        auto *view = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        return view && view->objectName().startsWith("IDD_BENCH") ? view : nullptr;
    }

  private slots:
    void sourceInitialPresentation_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("total");
        for (const char *language : {"-", "ru"})
            for (bool total : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(total)))
                    << QString::fromLatin1(language) << total;
    }
    void sourceInitialPresentation() {
        QFETCH(QString, language);
        QFETCH(bool, total);
        SaveRegLang(UString(language.toStdWString().c_str()),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        FixtureBenchmarkServices service;
        service.Hold = true;
        bool inspected = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *view = dialog();
            if (!view || !service.Active || inspected)
                return;
            inspected = true;
            const auto close = qScopeGuard([view] { view->reject(); });
            QVERIFY(UiInspection::writeReport(
                *view, QString("benchmark-source-initial-%1-%2").arg(language).arg(total)));
            QVERIFY(view->windowFlags().testFlag(Qt::WindowMinMaxButtonsHint));
            if (total) {
                QCOMPARE(view->focusWidget(), view->findChild<QPlainTextEdit *>("IDE_BENCH2_EDIT"));
                return;
            }
            auto *log = view->findChild<QPlainTextEdit *>("IDT_BENCH_LOG");
            QVERIFY(log);
            auto *scroll = view->findChild<QScrollArea *>("benchmarkControlsScroll");
            QVERIFY(scroll);
            QVERIFY(log->isReadOnly());
            for (const char *name : {"IDB_RESTART", "IDB_STOP", "IDCANCEL"}) {
                auto *button = view->findChild<QPushButton *>(name);
                QVERIFY(button);
                QVERIFY2(UiInspection::fullyVisible(*button, *view), name);
                QVERIFY(UiInspection::separate(*button, *scroll, *view));
            }
            for (const QString &name : {QStringLiteral("IDT_BENCH_SIZE"),
                                        QString("benchmarkValue%1").arg(IDT_BENCH_COMPRESS_SIZE1),
                                        QStringLiteral("IDT_BENCH_TOTAL_RATING_VAL")}) {
                auto *value = view->findChild<QLabel *>(name);
                QVERIFY(value);
                QVERIFY2(value->alignment().testFlag(Qt::AlignRight), qPrintable(name));
            }
            QCOMPARE(view->focusWidget(), view->findChild<QPushButton *>("IDB_RESTART"));
            QWidget *focused = view->focusWidget();
            for (const char *name :
                 {"IDB_STOP", "IDCANCEL", "IDC_BENCH_DICTIONARY", "IDC_BENCH_NUM_THREADS",
                  "IDC_BENCH_NUM_PASSES", "benchmarkControlsScroll", "IDT_BENCH_LOG",
                  "copyReportButton", "IDB_RESTART"}) {
                if (!view->findChild<QWidget *>(name)->isVisible())
                    continue;
                QWidget *next = focused->nextInFocusChain();
                while (next != focused &&
                       (!(next->focusPolicy() & Qt::TabFocus) || !next->isVisible()))
                    next = next->nextInFocusChain();
                QCOMPARE(next, view->findChild<QWidget *>(name));
                focused = next;
            }
        });
        responder.start(1);
        QCOMPARE(Benchmark(nullptr, properties(total), 1, nullptr, &service), S_OK);
        QVERIFY(inspected);
        QCOMPARE(service.Active.load(), 0u);
    }

    void detachedWorkerCompletionIsSuccessfulNoOp() {
        class Window final : public CQtBenchmarkWindow {
          public:
            using CQtBenchmarkWindow::Attach;
            unsigned Deliveries = 0;
            void OnMessage(unsigned, quintptr) override { ++Deliveries; }
        } window;
        QVERIFY(window.PostMsg(0x8001, 0));
        QCOMPARE(window.Deliveries, 0u);
        QApplication::processEvents();
        QCOMPARE(window.Deliveries, 1u);
        window.Attach(nullptr);
        QVERIFY(window.PostMsg(0x8001, 0));
        QApplication::processEvents();
        QCOMPARE(window.Deliveries, 1u);
    }
    void affinityFallback_data() {
        QTest::addColumn<bool>("querySucceeds");
        QTest::newRow("failed-query") << false;
        QTest::newRow("empty-affinity") << true;
    }
    void affinityFallback() {
#ifdef Q_OS_WIN
        QSKIP("Windows uses the original native normalized affinity helper");
#else
        QFETCH(bool, querySucceeds);
        class AffinityService final : public FixtureBenchmarkServices {
          public:
            bool Succeeds = false;
            bool GetProcessAffinity(NWindows::NSystem::CProcessAffinity &affinity) const override {
                if (!Succeeds)
                    return false;
                affinity.CpuZero();
                affinity.numSysThreads = 0;
                return Succeeds;
            }
        } service;
        service.Succeeds = querySucceeds;
        service.Hold = true;
        QString hardware;
        bool closed = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *view = dialog();
            if (!view || !service.Active || closed)
                return;
            closed = true;
            hardware = view->findChild<QLabel *>("IDT_BENCH_HARDWARE_THREADS")->text();
            view->reject();
        });
        responder.start(1);
        HRESULT completion = S_OK;
        QCOMPARE(Benchmark(nullptr, properties(), 1, nullptr, &service, &completion), S_OK);
        QCOMPARE(completion, E_ABORT);
        QVERIFY(closed);
        const UInt32 cpus = qMax(1u, NWindows::NSystem::GetNumberOfProcessors());
        QVERIFY2(hardware.startsWith("/ " + QString::number(cpus)), qPrintable(hardware));
        QCOMPARE(service.Canceled.load(), 1u);
        QCOMPARE(service.Active.load(), 0u);
#endif
    }
    void cleanup() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void layoutAndLiveLanguage_data() {
        QTest::addColumn<bool>("total");
        QTest::newRow("normal") << false;
        QTest::newRow("total") << true;
    }
    void layoutAndLiveLanguage() {
        QFETCH(bool, total);
        FixtureBenchmarkServices service;
        service.Hold = true;
        bool inspected = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *view = dialog();
            if (!view || !service.Active || inspected)
                return;
            inspected = true;
            struct CloseOnExit {
                QDialog *View;
                ~CloseOnExit() { View->reject(); }
            } close{view};
            for (const char *language : {"-", "ru", "-"}) {
                SaveRegLang(UString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
                ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
                QtNotifyLanguageChanged();
                QCOMPARE(view->windowTitle(),
                         QString(language) == "ru"
                             ? QStringLiteral("Тестирование производительности")
                             : QString("Benchmark"));
                if (!total)
                    QCOMPARE(view->findChild<QLabel *>("IDT_BENCH_SPEED")->text(),
                             QString(language) == "ru" ? QStringLiteral("Скорость")
                                                       : QString("Speed"));
                for (const QSize size : {QSize(960, 650), QSize(640, 400)}) {
                    view->resize(size);
                    QApplication::processEvents();
                    auto *close = view->findChild<QPushButton *>("IDCANCEL");
                    QVERIFY(close && UiInspection::fullyVisible(*close, *view));
                    QVERIFY(close->width() >= close->sizeHint().width());
                    QCOMPARE(close->text(), QString(language) == "ru" ? QStringLiteral("&Закрыть")
                                                                      : QString("&Close"));
                    if (total) {
                        QVERIFY(UiInspection::fullyVisible(
                            *view->findChild<QPlainTextEdit *>("IDE_BENCH2_EDIT"), *view));
                    } else {
                        auto *scroll = view->findChild<QScrollArea *>("benchmarkControlsScroll");
                        for (QWidget *control : scroll->widget()->findChildren<QWidget *>()) {
                            if (control->objectName().isEmpty() || !control->isVisible())
                                continue;
                            scroll->ensureWidgetVisible(control, 5, 10);
                            QApplication::processEvents();
                            if (!UiInspection::fullyVisible(*control, *view))
                                QVERIFY(UiInspection::writeReport(*view,
                                                                  "benchmark-unreachable-control"));
                            QVERIFY2(UiInspection::fullyVisible(*control, *view),
                                     qPrintable(control->objectName()));
                            if (qobject_cast<QLabel *>(control)) {
                                if (!UiInspection::labelFits(*control))
                                    QVERIFY(UiInspection::writeReport(*view,
                                                                      "benchmark-clipped-label"));
                                QVERIFY2(UiInspection::labelFits(*control),
                                         qPrintable(control->objectName()));
                            }
                        }
                        scroll->verticalScrollBar()->setValue(0);
                        scroll->horizontalScrollBar()->setValue(0);
                        if (scroll->horizontalScrollBar()->maximum() > 0) {
                            scroll->setFocus();
                            QTest::keyClick(scroll, Qt::Key_Right);
                            QVERIFY(scroll->horizontalScrollBar()->value() > 0);
                            scroll->horizontalScrollBar()->setValue(0);
                        }
                    }
                    QVERIFY(UiInspection::writeReport(*view, QString("benchmark-%1-%2-%3")
                                                                 .arg(total ? "total" : "normal")
                                                                 .arg(language)
                                                                 .arg(size.width())));
                }
            }
            view->reject();
        });
        responder.start(1);
        QCOMPARE(Benchmark(nullptr, properties(total), 1, nullptr, &service), S_OK);
        QVERIFY(inspected);
        QCOMPARE(service.Active.load(), 0u);
    }
    void fixedSourceMetricsAndTotalConsole_data() {
        QTest::addColumn<bool>("total");
        QTest::newRow("normal") << false;
        QTest::newRow("total") << true;
    }
    void fixedSourceMetricsAndTotalConsole() {
        QFETCH(bool, total);
        auto *clipboard = QApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        if (clipboard->mimeData())
            for (const QString &format : clipboard->mimeData()->formats())
                saved->setData(format, clipboard->mimeData()->data(format));
        const auto restore = qScopeGuard([&] { clipboard->setMimeData(saved.release()); });
        FixtureBenchmarkServices service;
        const QString longOutput = QStringLiteral("Fixture native benchmark output ") +
                                   QString(2000, QChar('x')) +
                                   QStringLiteral(" файл 😀\nnext line");
        if (total)
            service.Output = longOutput.toUtf8().toStdString();
        QString size, speed, usage, console;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *view = dialog();
            if (!view || service.Active || service.Calls == 0)
                return;
            if (total) {
                console = view->findChild<QPlainTextEdit *>("IDE_BENCH2_EDIT")->toPlainText();
                if (!console.contains("Fixture native benchmark output"))
                    return; // Worker completion precedes its queued GUI delivery.
            } else {
                if (view->findChild<QPushButton *>("IDB_STOP")->isEnabled())
                    return;
                size = view->findChild<QLabel *>("benchmarkValue171")->text();
                speed = view->findChild<QLabel *>("benchmarkValue111")->text();
                usage = view->findChild<QLabel *>("IDT_BENCH_TOTAL_USAGE_VAL")->text();
            }
            responder.stop();
            const auto close = qScopeGuard([view] { view->reject(); });
            auto *copy = view->findChild<QPushButton *>("copyReportButton");
            copy->click();
            if (total) {
                QCOMPARE(clipboard->text(), console);
                auto *edit = view->findChild<QPlainTextEdit *>("IDE_BENCH2_EDIT");
                QTRY_VERIFY(edit->horizontalScrollBar()->maximum() > 0);
                auto cursor = edit->textCursor();
                cursor.setPosition(0);
                cursor.setPosition(10, QTextCursor::KeepAnchor);
                edit->setTextCursor(cursor);
                copy->click();
                QCOMPARE(clipboard->text(), console.left(10));
                QCOMPARE(edit->textCursor().selectedText(), console.left(10));
            } else {
                QVERIFY(clipboard->text().contains(size));
                QVERIFY(clipboard->text().contains(speed));
                QVERIFY(clipboard->text().contains(usage));
                const QString title = view->findChild<QLabel *>("IDG_BENCH_TOTAL_RATING")->text();
                bool foundTotal = false;
                for (const QString &line : clipboard->text().split('\n')) {
                    if (!line.startsWith(title + '\t'))
                        continue;
                    const QStringList cells = line.split('\t');
                    QCOMPARE(cells.size(), 7);
                    QCOMPARE(cells[4], usage);
                    foundTotal = true;
                }
                QVERIFY(foundTotal);
            }
            QVERIFY(view->isVisible());
            QVERIFY(UiInspection::writeReport(*view, total ? "benchmark-completed-total"
                                                           : "benchmark-completed-normal"));
        });
        responder.start(1);
        QCOMPARE(Benchmark(nullptr, properties(total), 1, nullptr, &service), S_OK);
        QCOMPARE(service.Calls.load(), 1u);
        QCOMPARE(service.Active.load(), 0u);
        if (total)
            QVERIFY(console.contains(longOutput));
        else {
            QCOMPARE(size, QString("1 MB"));
            QCOMPARE(speed, QString("1024 KB/s"));
            QCOMPARE(usage, QString("50%"));
        }
    }
    void restartThenStopAndCloseJoinBothWorkers() {
        FixtureBenchmarkServices service;
        service.Hold = true;
        bool restarted = false, stopped = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *view = dialog();
            if (!view)
                return;
            if (service.Calls == 1 && !restarted) {
                restarted = true;
                view->findChild<QPushButton *>("IDB_RESTART")->click();
            } else if (service.Calls >= 2 && !stopped) {
                stopped = true;
                view->findChild<QPushButton *>("IDB_STOP")->click();
                view->reject();
            }
        });
        responder.start(1);
        QCOMPARE(Benchmark(nullptr, properties(), 1, nullptr, &service), S_OK);
        QVERIFY(restarted && stopped);
        QCOMPARE(service.Calls.load(), 2u);
        QCOMPARE(service.Canceled.load(), 2u);
        QCOMPARE(service.Active.load(), 0u);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
    }
    void memoryRefusalAndEngineFailure_data() {
        QTest::addColumn<bool>("memory");
        QTest::addColumn<QString>("language");
        QTest::addColumn<HRESULT>("result");
        QTest::addColumn<QString>("expected");
        QTest::newRow("memory-refusal") << true << QString("-") << S_OK << QString();
        QTest::newRow("engine-failure") << false << QString("-") << E_FAIL << QString();
        QTest::newRow("decoding-en")
            << false << QString("-") << S_FALSE << QString("ERROR: Decoding error");
        QTest::newRow("decoding-ru") << false << QString("ru") << S_FALSE
                                     << QString::fromUtf8("ОШИБКА: Ошибка декодирования");
    }
    void memoryRefusalAndEngineFailure() {
        QFETCH(bool, memory);
        QFETCH(QString, language);
        QFETCH(HRESULT, result);
        QFETCH(QString, expected);
        SaveRegLang(UString(language.toStdWString().c_str()),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        FixtureBenchmarkServices service;
        if (memory)
            service.Ram = size_t(1) << 20;
        else
            service.Result = result;
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
            } else if (auto *view = dialog(); view && !errors.isEmpty())
                view->reject();
        });
        responder.start(1);
        HRESULT completion = S_OK;
        QCOMPARE(Benchmark(nullptr, properties(), 1, nullptr, &service, &completion), S_OK);
        QCOMPARE(completion, memory ? E_OUTOFMEMORY : result);
        QVERIFY(!errors.isEmpty());
        // The legacy return contract is retained; 7zG consumes the operation result.
        if (!expected.isEmpty())
            QVERIFY2(errors.contains(expected), qPrintable(errors.join('\n')));
        QCOMPARE(service.Calls.load(), memory ? 0u : 1u);
        QCOMPARE(service.Active.load(), 0u);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(BenchmarkLifecycleTests)
#include "benchmark_lifecycle_tests.moc"
