#include "archive/support/source_extract_fixture.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "operations/qtoperationregistry.h"
#include "settings/RegistryUtils.h"
#include "support/operation_kill.h"
#include "support/scoped_environment.h"
#include "ui/support/layout_inspection.h"
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
#include <QToolButton>

namespace {
QAction *Entry(QMenu &menu, const QUuid &id) {
    for (auto *action : menu.actions())
        if (action->data().toUuid() == id)
            return action;
    return nullptr;
}
QJsonObject WindowReport(const QString &path) {
    return QJsonDocument::fromJson(SourceExtractFixture::readFile(path)).object();
}
} // namespace

class OperationsMenuTests final : public QObject {
    Q_OBJECT
  private slots:
    void returnsToSelectedLiveWindowAndRetainsSuccess() {
        QTemporaryDir fixture;
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "original bytes");
        QVERIFY(!archive.isEmpty());
        const auto original = SourceExtractFixture::readFile(archive);
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *button = window.findChild<QToolButton *>("operationsButton");
        QVERIFY(button);
        QVERIFY(!button->isVisible());
        QPointer<QtOperationLauncher> operations[2];
        QUuid ids[2];
        const auto cleanup = qScopeGuard([&] {
            qunsetenv("SEVENZIP_OPERATION_WINDOW_REPORT");
            qunsetenv("SEVENZIP_OPERATION_RELEASE_WINDOW");
            for (auto &operation : operations)
                if (operation && operation->active())
                    KillOperationProbe(operation->processId());
        });
        for (int index = 0; index < 2; ++index) {
            qputenv("SEVENZIP_OPERATION_WINDOW_REPORT",
                    fixture.filePath(QString::number(index) + ".json").toUtf8());
            qputenv("SEVENZIP_OPERATION_RELEASE_WINDOW",
                    fixture.filePath(QString::number(index) + ".release").toUtf8());
            QtOperationJob job;
            job.Operation =
                index ? QtOperationJob::Command::Hash : QtOperationJob::Command::Extract;
            job.Paths = {archive};
            job.HashMethod = "SHA256";
            job.Destination = fixture.filePath("not extracted");
            QString error;
            operations[index] =
                QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
            QVERIFY2(operations[index], qPrintable(error));
            ids[index] = QtOperationRegistry::Application().Registration(*operations[index]);
            QTRY_VERIFY_WITH_TIMEOUT(
                WindowReport(fixture.filePath(QString::number(index) + ".json"))
                    .value("minimized")
                    .toBool(),
                15000);
        }
        QTRY_VERIFY(button->isVisible());
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            for (const bool two : {false, true, false}) {
                QTRY_VERIFY(twoPanels->isEnabled());
                if (twoPanels->isChecked() != two)
                    twoPanels->trigger();
                QTRY_COMPARE(twoPanels->isChecked(), two);
                auto *panes = window.findChild<QSplitter *>("filePanelSplitter");
                int visibleContexts = 0;
                for (int pane = 0; pane < panes->count(); ++pane) {
                    auto *context = panes->widget(pane);
                    if (context->isVisible()) {
                        ++visibleContexts;
                        if (!two)
                            QTRY_COMPARE(context->width(), panes->contentsRect().width());
                        auto *control = context->findChild<QToolButton *>("operationsButton");
                        QVERIFY(control);
                        QTRY_VERIFY(control->isVisible());
                        QTRY_VERIFY(UiInspection::fullyVisible(*control, window));
                        if (two)
                            QVERIFY(control->width() <= control->iconSize().width() +
                                                            control->fontMetrics().height() * 2);
                        else
                            QTRY_VERIFY(control->width() >=
                                        control->fontMetrics().horizontalAdvance(control->text()));
                        for (auto *toolbar : context->findChildren<QToolBar *>())
                            if (toolbar->isVisible())
                                for (auto *action : toolbar->actions()) {
                                    auto *command = qobject_cast<QToolButton *>(
                                        toolbar->widgetForAction(action));
                                    if (command && command->isVisible() &&
                                        command->toolButtonStyle() != Qt::ToolButtonIconOnly)
                                        QTRY_VERIFY(command->width() >=
                                                    command->fontMetrics().horizontalAdvance(
                                                        QString(command->text()).remove('&')));
                                }
                    }
                }
                QCOMPARE(visibleContexts, two ? 2 : 1);
                QVERIFY(UiInspection::writeReport(
                    window, QString("operations-live-%1-%2").arg(size.width()).arg(two)));
            }
        }
        ControlledBackend peerBackend;
        MainWindow peer(&peerBackend);
        peer.show();
        auto *peerButton = peer.findChild<QToolButton *>("operationsButton");
        QVERIFY(peerButton);
        QTRY_VERIFY(peerButton->isVisible());
        auto *menu = peerButton->menu();
        menu->popup(peerButton->mapToGlobal(QPoint(0, peerButton->height())));
        QTRY_VERIFY(menu->isVisible());
        auto *selected = Entry(*menu, ids[1]);
        QVERIFY(selected && selected->isEnabled());
        QVERIFY(Entry(*menu, ids[0]));
        QVERIFY(UiInspection::writeReport(window, "operations-two-live"));
        QVERIFY(UiInspection::writeReport(*menu, "operations-menu-two-live"));
        menu->hide();
        selected->trigger();
        QTRY_VERIFY(!WindowReport(fixture.filePath("1.json")).value("minimized").toBool());
        QVERIFY(WindowReport(fixture.filePath("0.json")).value("minimized").toBool());
        menu->popup(peerButton->mapToGlobal(QPoint(0, peerButton->height())));
        QTRY_VERIFY(menu->isVisible());
        auto *remaining = Entry(*menu, ids[0]);
        QVERIFY(remaining && remaining->isEnabled());
        menu->setActiveAction(remaining);
        QSignalSpy finished(operations[1], &QtOperationLauncher::finished);
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath("1.release"), "close result"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QCOMPARE(qvariant_cast<QtOperationResult>(finished[0][0]).Status,
                 QtOperationResult::Outcome::Success);
        QVERIFY(menu->isVisible());
        auto *done = Entry(*menu, ids[1]);
        QVERIFY(done && !done->isEnabled());
        QVERIFY(Entry(*menu, ids[0])->isEnabled());
        QCOMPARE(menu->activeAction(), Entry(*menu, ids[0]));
        menu->hide();
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        QVERIFY(!QFileInfo::exists(fixture.filePath("not extracted")));
        QSignalSpy cancelled(operations[0], &QtOperationLauncher::finished);
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath("0.release"), "cancel draft"));
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 15000);
        QVERIFY(!qvariant_cast<QtOperationResult>(cancelled[0][0]).CancellationNeedsReview);
    }
    void keepsExtractionWarningAfterClosingTheOperation() {
        using namespace SourceExtractFixture;
        QTemporaryDir fixture;
        const auto archive = makeArchive(fixture.path(), "incoming bytes");
        QVERIFY(!archive.isEmpty());
        const auto output = fixture.filePath("output");
        const auto existing = QDir(output).filePath(kName);
        QVERIFY(writeFile(existing, "retained bytes"));
        const ScopedTestEnvironment answer("SEVENZIP_OPERATION_OVERWRITE", "overwriteNo");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = {archive};
        job.Destination = output;
        job.ShowDialog = false;
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto cleanup = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        const auto id = QtOperationRegistry::Application().Registration(*operation);
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QCOMPARE(qvariant_cast<QtOperationResult>(finished[0][0]).Status,
                 QtOperationResult::Outcome::Warning);
        QTRY_VERIFY(!operation);
        QCOMPARE(readFile(existing), QByteArray("retained bytes"));
        auto *button = window.findChild<QToolButton *>("operationsButton");
        QVERIFY(button && button->isVisible());
        auto *menu = button->menu();
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        QTRY_VERIFY(menu->isVisible());
        const auto *warning = Entry(*menu, id);
        QVERIFY(warning && !warning->isEnabled());
        QVERIFY(warning->text().contains(
            QCoreApplication::translate("QtOperationsButton", "Done with warnings")));
        QVERIFY(UiInspection::writeReport(*menu, "operations-extraction-warning"));
        menu->hide();
    }
    void compressionWarningNamesTheWrittenArchive() {
        using namespace SourceExtractFixture;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("input.txt");
        QVERIFY(writeFile(source, "payload"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Compress;
        job.Paths = {source, fixture.filePath("missing.txt")};
        job.ArchivePath = fixture.filePath("result archive");
        job.ArchiveType = "zip";
        job.AddExtension = true;
        job.ShowDialog = false;
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto cleanup = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        const auto id = QtOperationRegistry::Application().Registration(*operation);
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QCOMPARE(qvariant_cast<QtOperationResult>(finished[0][0]).Status,
                 QtOperationResult::Outcome::Warning);
        QVERIFY(QFileInfo(fixture.filePath("result archive.zip")).isFile());
        auto *button = window.findChild<QToolButton *>("operationsButton");
        QVERIFY(button && button->isVisible());
        auto *menu = button->menu();
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        QTRY_VERIFY(menu->isVisible());
        const auto *warning = Entry(*menu, id);
        QVERIFY(warning);
        QVERIFY(warning->text().contains("result archive.zip"));
        QVERIFY(warning->toolTip().contains("result archive.zip"));
        menu->hide();
    }

    void reopensSamePendingResultAfterLanguageSwitch() {
        QTemporaryDir fixture;
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "retained bytes");
        QVERIFY(!archive.isEmpty());
        const auto barrier = fixture.filePath("finish");
        qputenv("SEVENZIP_OPERATION_BARRIER", barrier.toUtf8());
        qputenv("SEVENZIP_OPERATION_FORCE_EXIT", "crash");
        const auto restore = qScopeGuard([] {
            qunsetenv("SEVENZIP_OPERATION_BARRIER");
            qunsetenv("SEVENZIP_OPERATION_FORCE_EXIT");
            SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QtOperationJob job;
        job.Paths = {archive};
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto cleanup = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        const QUuid id = QtOperationRegistry::Application().Registration(*operation);
        QSignalSpy accepted(operation, &QtOperationLauncher::accepted);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        QVERIFY(SourceExtractFixture::writeFile(barrier, "finish"));
        QTRY_VERIFY_WITH_TIMEOUT(!QtOperationRegistry::Application().Reviews().isEmpty(), 15000);
        QtTaskResultDialog *result = nullptr;
        const auto findResult = [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QtTaskResultDialog *>(widget))
                    return dialog;
            return static_cast<QtTaskResultDialog *>(nullptr);
        };
        QTRY_VERIFY((result = findResult()));
        result->hide();
        auto *button = window.findChild<QToolButton *>("operationsButton");
        for (const char *locale : {"-", "ru", "-"}) {
            SaveRegLang(UString(locale), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            QTRY_VERIFY(
                button->text().contains(locale == QByteArray("ru") ? "Результаты" : "Results"));
            auto *menu = button->menu();
            menu->popup(button->mapToGlobal(QPoint(0, button->height())));
            QTRY_VERIFY(menu->isVisible());
            auto *action = Entry(*menu, id);
            QVERIFY(action && action->isEnabled());
            QVERIFY(UiInspection::writeReport(*menu, QString("operations-result-%1").arg(locale)));
            menu->hide();
            action->trigger();
            QTRY_VERIFY(result->isVisible());
            QCOMPARE(findResult(), result);
            result->hide();
        }
        result->show();
        QTRY_VERIFY(result->isVisible());
        result->close();
        QTRY_VERIFY(QtOperationRegistry::Application().Reviews().isEmpty());
        QtOperationRegistry::Application().Show(id);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!findResult());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(OperationsMenuTests)
#include "operations_menu_tests.moc"
