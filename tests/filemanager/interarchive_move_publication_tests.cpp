#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "settings/RegistryUtils.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QStandardPaths>

class InterarchiveMovePublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static bool run(const QString &engine, const QString &directory, const QStringList &args) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(engine, args);
        return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
               process.exitCode() == 0;
    }
  private slots:
    void diskMoveCanRetryAfterArchiveWriteFailure() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        NativePropertyFailureRuntime runtime;
        QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime.enableWriter());
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        QVERIFY(QDir().mkpath(input));
        const QString name = QStringLiteral("moved 空.txt");
        const QString diskFile = QDir(input).filePath(name);
        QVERIFY(SourceExtractFixture::writeFile(diskFile, "payload"));
        const QString archive = fixture.filePath("target.fixture7z");
        QVERIFY(runtime.createArchive(archive, 1));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        QVERIFY(!before.isEmpty());
        ArchiveBackend backend(nullptr, runtime.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        MainWindow window(&backend);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        for (unsigned pane : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            QVERIFY(ui);
            views[pane] = ui->findChild<FileTable *>("archiveTable");
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            address->setText(pane == 0 ? input : archive);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(views[pane]->isEnabled() && views[pane]->rowCount() == 1);
        }
        auto *source = views[0];
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        source->selectRow(0);
        QVERIFY(runtime.armWriter(E_FAIL, false));
        int reports = 0, warnings = 0;
        QString diagnostics;
        QTimer driver;
        dialogs_.connectResponder(driver, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal))
                copy->accept();
            else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                ++warnings;
                box->accept();
            } else if (auto *report = TestProgressDialog::ResultWindow(modal)) {
                ++reports;
                diagnostics = TestProgressDialog::ReportText(*report);
                TestProgressDialog::CloseResult(*report);
            }
        });
        driver.start(2);
        QTest::keyClick(source, Qt::Key_F6);
        QTRY_COMPARE(reports, 1);
        QCOMPARE(warnings, 0);
        QVERIFY(!diagnostics.isEmpty());
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(!runtime.writerReceipts().isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(SourceExtractFixture::readFile(diskFile), QByteArray("payload"));
        QCOMPARE(source->selectionModel()->selectedRows().size(), 1);
        QVERIFY(runtime.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(source->hasFocus());
        QTest::keyClick(source, Qt::Key_F6);
        QTRY_VERIFY(!QFileInfo::exists(diskFile));
        QTRY_COMPARE(views[1]->rowCount(), 2);
        const QString output = fixture.filePath("verified");
        QVERIFY(run(QStandardPaths::findExecutable("7z"), fixture.path(),
                    {"x", "-y", "-o" + output, "--", archive, name}));
        QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(name)),
                 QByteArray("payload"));
        QCOMPARE(reports, 1);
        QCOMPARE(warnings, 0);
    }

    void moveRetainsTheOnlyPublishedCopy_data() {
        QTest::addColumn<int>("failure");
        QTest::addColumn<bool>("russian");
        QTest::newRow("success") << 0 << false;
        QTest::newRow("refused-deletion") << 1 << false;
        QTest::newRow("source-reopen-failure") << 2 << false;
        QTest::newRow("source-write-failure") << 3 << false;
        QTest::newRow("source-reopen-failure-ru") << 2 << true;
    }
    void moveRetainsTheOnlyPublishedCopy() {
        QFETCH(int, failure);
        QFETCH(bool, russian);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(russian ? L"ru" : L"-", settings);
        ReloadLang(settings);
        NativePropertyFailureRuntime runtime;
        QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime.enableWriter());
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString moved = QStringLiteral("moved 空 with spaces.txt");
        const QString kept = "source-control.txt", targetKept = "target-control.txt";
        for (const auto &name : {moved, kept, targetKept})
            QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), name.toUtf8()));
        const QString source = fixture.filePath("source.fixture7z");
        const QString target = fixture.filePath("target.zip");
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        QVERIFY(run(engine, fixture.path(), {"a", "-t7z", "--", source, moved, kept}));
        QVERIFY(run(engine, fixture.path(), {"a", "-tzip", "--", target, targetKept}));
        const QByteArray sourceBefore = SourceExtractFixture::readFile(source);
        const QByteArray targetBefore = SourceExtractFixture::readFile(target);
        ArchiveBackend backend(nullptr, runtime.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        for (unsigned i : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(i));
            views[i] = ui->findChild<FileTable *>("archiveTable");
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            address->setText(i == 0 ? source : target);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(views[i]->rowCount(), i == 0 ? 2 : 1);
            QTRY_VERIFY(views[i]->isEnabled());
            views[i]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        int row = -1;
        for (int i = 0; i < views[0]->rowCount(); ++i)
            if (views[0]->model()->index(i, 0).data().toString() == moved)
                row = i;
        QVERIFY(row >= 0);
        views[0]->setCurrentCell(row, 0);
        views[0]->selectRow(row);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        views[0]->setFocus();
        QTRY_VERIFY(views[0]->hasFocus());
        QTRY_VERIFY(action(window, "moveAction")->isEnabled());
        bool submitted = false, confirmed = false;
        bool reviewed = false;
        QStringList errors;
        QTimer driver;
        dialogs_.connectResponder(driver, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal); copy && !submitted) {
                const auto dismiss = qScopeGuard([&] {
                    if (copy->isVisible())
                        copy->reject();
                });
                submitted = true;
                QCOMPARE(QDir::cleanPath(copy->destination()),
                         QDir::cleanPath(ToQString(views[1]->sourcePanel->GetFsPath())));
                copy->accept();
            } else if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(modal)) {
                QCOMPARE(confirmation->windowTitle(),
                         QCoreApplication::translate("DeleteRedesign", "Delete from archive?"));
                confirmed = true;
                if (failure == 2)
                    QVERIFY(runtime.arm(UInt32(-1), E_FAIL, 1));
                if (failure == 3)
                    QVERIFY(runtime.armWriter(E_FAIL, false));
                ReplyDelete(*confirmation, failure != 1);
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                const auto dismiss = qScopeGuard([&] {
                    if (box->isVisible())
                        box->reject();
                });
                errors << box->text();
                for (const QSize size : {QSize(440, 220), QSize(340, 220)}) {
                    box->resize(size);
                    QApplication::processEvents();
                    auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                    auto *ok = box->button(QMessageBox::Ok);
                    QVERIFY(label && ok);
                    QVERIFY(UiInspection::fullyVisible(*label, *box));
                    QVERIFY(UiInspection::fullyVisible(*ok, *box));
                }
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close)) {
                    const auto dismiss = qScopeGuard([&] { close->click(); });
                    reviewed = true;
                    if (failure == 1 || failure == 3) {
                        QCOMPARE(SourceExtractFixture::readFile(source), sourceBefore);
                        QCOMPARE(SourceExtractFixture::readFile(target), targetBefore);
                    }
                    if (progress->Sync.FinalMessage.Result != E_ABORT)
                        errors << TestProgressDialog::ReportText(*progress);
                }
            }
        });
        driver.start(2);
        action(window, "moveAction")->trigger();
        if (failure)
            QTRY_VERIFY(reviewed);
        driver.stop();
        QVERIFY(submitted);
        QVERIFY(confirmed);
        if (failure == 2) {
            QVERIFY(!runtime.openReceipts().isEmpty());
            QVERIFY(!errors.isEmpty());
            QVERIFY(errors.last().contains(russian ? QStringLiteral("Копия в целевом архиве")
                                                   : QStringLiteral("destination copy")));
        } else if (failure == 3) {
            QVERIFY(!runtime.writerReceipts().isEmpty());
        } else
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QVERIFY(runtime.clear());
        const QString sourceOut = fixture.filePath("source output");
        const QString targetOut = fixture.filePath("target output");
        QVERIFY(run(engine, fixture.path(), {"x", "-y", "-o" + sourceOut, "--", source}));
        QVERIFY(run(engine, fixture.path(), {"x", "-y", "-o" + targetOut, "--", target}));
        QCOMPARE(SourceExtractFixture::readFile(QDir(sourceOut).filePath(kept)), kept.toUtf8());
        QCOMPARE(SourceExtractFixture::readFile(QDir(targetOut).filePath(targetKept)),
                 targetKept.toUtf8());
        const bool rolledBack = failure == 1 || failure == 3;
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        if (rolledBack) {
            const auto selected = views[0]->selectionModel()->selectedRows();
            QCOMPARE(selected.size(), 1);
            QCOMPARE(selected.front().data().toString(), moved);
        } else {
            QVERIFY(views[0]->selectionModel()->selectedRows().isEmpty());
        }
        QCOMPARE(QFileInfo::exists(QDir(sourceOut).filePath(moved)), rolledBack);
        QCOMPARE(QFileInfo::exists(QDir(targetOut).filePath(moved)), !rolledBack);
        QCOMPARE(SourceExtractFixture::readFile(
                     QDir(rolledBack ? sourceOut : targetOut).filePath(moved)),
                 moved.toUtf8());
        if (rolledBack) {
            QCOMPARE(SourceExtractFixture::readFile(source), sourceBefore);
            QCOMPARE(SourceExtractFixture::readFile(target), targetBefore);
        }
    }
};

QTEST_MAIN(InterarchiveMovePublicationTests)
#include "interarchive_move_publication_tests.moc"
