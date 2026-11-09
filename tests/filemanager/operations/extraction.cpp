#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "ui/support/layout_inspection.h"
#include "window_operations.h"
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QStatusBar>

void MainWindowOperationsTests::backendOverwriteDisplaysNativeMetadata_data() {
    QTest::addColumn<bool>("defined");
    QTest::newRow("native snapshot and full unsigned size") << true;
    QTest::newRow("unknown metadata stays unknown") << false;
}

void MainWindowOperationsTests::backendOverwriteDisplaysNativeMetadata() {
    QFETCH(bool, defined);
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QString target = fixture.filePath(QStringLiteral("старый 空.txt"));
    QFile original(target);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("keep"), qint64(4));
    original.close();
    OverwriteQuestion question;
    question.Existing.Path = target;
    question.Existing.Filesystem = true;
    question.Incoming.Path = QStringLiteral("входящий файл/new.txt");
    if (defined) {
        question.Existing.Size = 42; // The native question is a snapshot, not a new stat().
        question.Incoming.Size = UInt64(-1) - 4;
        const UInt64 ticks = 116444736000000000ULL + 1234567;
        question.Existing.Modified = FILETIME{UInt32(ticks), UInt32(ticks >> 32)};
        question.Incoming.Modified = question.Existing.Modified;
    }
    ArchiveBackend backend;
    MainWindow window(&backend);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QScopedValueRollback<bool> utc(g_Timestamp_Show_UTC, true);
    bool inspected = false;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        COverwriteDialog *dialog = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *candidate = dynamic_cast<COverwriteDialog *>(widget);
                candidate && candidate->isVisible())
                dialog = candidate;
        if (!dialog)
            return;
        answer.stop();
        const auto close = qScopeGuard([&] { dialog->reject(); });
        inspected = true;
        auto *oldText = dialog->findChild<QLabel *>("overwriteOldFile");
        auto *newText = dialog->findChild<QLabel *>("overwriteNewFile");
        QVERIFY(oldText && newText);
        QCOMPARE(dialog->OldFileInfo.Size_IsDefined, defined);
        QCOMPARE(dialog->NewFileInfo.Size_IsDefined, defined);
        QCOMPARE(dialog->OldFileInfo.Time_IsDefined, defined);
        QCOMPARE(dialog->NewFileInfo.Time_IsDefined, defined);
        if (defined) {
            QVERIFY(oldText->text().contains("42 bytes"));
            QVERIFY(newText->text().contains(QLocale().toString(*question.Incoming.Size)));
            for (const auto *label : {oldText, newText})
                QVERIFY(label->text().contains("1970-01-01 00:00:00"));
            QCOMPARE(dialog->NewFileInfo.Time.dwLowDateTime,
                     question.Incoming.Modified->dwLowDateTime);
            QCOMPARE(dialog->NewFileInfo.Time.dwHighDateTime,
                     question.Incoming.Modified->dwHighDateTime);
        } else {
            QVERIFY(!oldText->text().contains(" bytes"));
            QVERIFY(!newText->text().contains(" bytes"));
            QVERIFY(!newText->text().contains("1970"));
        }
        for (int width : {700, 560}) {
            dialog->resize(width, 440);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*oldText, *dialog));
            QVERIFY(UiInspection::fullyVisible(*newText, *dialog));
            QVERIFY(UiInspection::labelFits(*oldText));
            QVERIFY(UiInspection::labelFits(*newText));
            QVERIFY(UiInspection::writeReport(
                *dialog, QString("backend-overwrite-%1-%2").arg(defined).arg(width)));
        }
    });
    answer.start(1);
    backend.overwriteRequired(question);
    answer.stop();
    QVERIFY(inspected);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("keep"));
}

void MainWindowOperationsTests::extractAskUsesSourceOverwriteDialog() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir("destination"));
    const QString source = temp.filePath(QStringLiteral("архивный файл.txt"));
    QFile incoming(source);
    QVERIFY(incoming.open(QIODevice::WriteOnly));
    QCOMPARE(incoming.write("archive"), qint64(7));
    incoming.close();
    const QString archive = temp.filePath("ask.7z");
    const QString target = temp.filePath(QStringLiteral("destination/архивный файл.txt"));
    ArchiveBackend backend;
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
    MainWindow window(&backend);
    window.openArchive(archive);
    QTRY_COMPARE(table(window)->rowCount(), 1);
    for (bool selected : {false, true}) {
        const QString freshOutput =
            temp.filePath(QStringLiteral("новая папка/вложенный output %1").arg(selected));
        if (selected)
            table(window)->selectRow(0);
        window.extractTo(freshOutput, selected);
        QFile extracted(QDir(freshOutput).filePath(QStringLiteral("архивный файл.txt")));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("archive"));
    }
    QFile old(target);
    QVERIFY(old.open(QIODevice::WriteOnly));
    QCOMPARE(old.write("existing"), qint64(8));
    old.close();
    QSignalSpy preflight(&backend, &ArchiveBackend::overwriteRequired);
    bool answered = false;
    QByteArray choice("overwriteYes");
    QString report;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *result = TestProgressDialog::ResultWindow(widget);
                result && result->isVisible()) {
                report = TestProgressDialog::ReportText(*result);
                TestProgressDialog::CloseResult(*result);
                continue;
            }
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible()) {
                answered = true;
                QVERIFY(!dialog->findChild<QLabel *>("overwriteOldFile")->text().isEmpty());
                dialog->findChild<QPushButton *>(choice.constData())->click();
            }
        }
    });
    answer.start(1);
    ExtractionOptions options;
    options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
    window.extractTo(temp.filePath("destination"), false, {}, options);
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0); // The source callback owns this prompt.
    QFile result(target);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("archive"));
    result.close();

    QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(old.write("preserved"), qint64(9));
    old.close();
    choice = "overwriteNo";
    table(window)->selectRow(0);
    for (bool selected : {false, true}) {
        answered = false;
        window.extractTo(temp.filePath("destination"), selected, {}, options);
        QVERIFY(answered);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("preserved"));
        old.close();
        QCOMPARE(window.statusBar()->currentMessage(),
                 QCoreApplication::translate("ProgressRedesign",
                                             "Completed with skipped items or messages"));
    }

    choice = "overwriteRename";
    answered = false;
    window.extractTo(temp.filePath("destination"), true, {}, options);
    QVERIFY(answered);
    QStringList files =
        QDir(temp.filePath("destination")).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(files.size(), 2);
    QVERIFY(files.removeOne(QStringLiteral("архивный файл.txt")));
    const QString renamed = files.first();
    QFile renamedFile(temp.filePath("destination/" + renamed));
    QVERIFY(renamedFile.open(QIODevice::ReadOnly));
    QCOMPARE(renamedFile.readAll(), QByteArray("archive"));

    choice = "overwriteCancel";
    answered = false;
    window.extractTo(temp.filePath("destination"), true, {}, options);
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QCOMPARE(window.statusBar()->currentMessage(),
             QCoreApplication::translate("mainwindow_archivecommands", "Operation canceled."));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("preserved"));
    old.close();

    answered = false;
    report.clear();
    ExtractionOptions skip = options;
    skip.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
    window.extractTo(temp.filePath("destination"), true, {}, skip);
    QVERIFY(!answered);
    QVERIFY(report.contains(QStringLiteral("архивный файл.txt")));
    QCOMPARE(preflight.size(), 0);
    QCOMPARE(window.statusBar()->currentMessage(),
             QCoreApplication::translate("ProgressRedesign",
                                         "Completed with skipped items or messages"));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("preserved"));
    old.close();

    choice = "overwriteYes";
    answered = false;
    window.extractTo(temp.filePath("destination"), true, {}, options);
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(window.statusBar()->currentMessage(),
             QCoreApplication::translate("mainwindow_archivecommands", "Extraction completed."));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("archive"));
}

void MainWindowOperationsTests::skippedArchiveMoveKeepsSourceAndShowsPartialResult() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QString name = QStringLiteral("retained 空.txt");
    const QString input = fixture.filePath(name);
    const QString archive = fixture.filePath("source.7z");
    const QString output = fixture.filePath("output");
    QVERIFY(QDir().mkpath(output));
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QVERIFY(write(input, "incoming bytes"));
    QVERIFY(write(QDir(output).filePath(name), "kept bytes"));
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {input}));
    const auto before = read(archive);
    ArchiveBackend backend;
    MainWindow window(&backend);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    window.openArchive(archive);
    QTRY_COMPARE(table(window)->rowCount(), 1);
    table(window)->selectRow(0);
    bool skipped = false, reviewed = false;
    QTimer driver;
    connect(&driver, &QTimer::timeout, &window, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (auto *copy = dynamic_cast<CopyDialog *>(modal)) {
            copy->findChild<QLineEdit *>("copyDestination")->setText(output);
            copy->accept();
        } else if (auto *question = dynamic_cast<COverwriteDialog *>(modal)) {
            skipped = true;
            question->findChild<QPushButton *>("overwriteNo")->click();
        } else if (auto *result = TestProgressDialog::ResultWindow(modal)) {
            const auto close = qScopeGuard([&] { TestProgressDialog::CloseResult(*result); });
            reviewed = true;
            QCOMPARE(result->findChild<QLabel *>("IDT_PROGRESS_STATUS")->text(),
                     QCoreApplication::translate("ProgressRedesign",
                                                 "Completed with skipped items or messages"));
        }
    });
    driver.start(5);
    action(window, "moveAction")->trigger();
    QTRY_VERIFY(reviewed);
    QVERIFY(skipped);
    QCOMPARE(read(archive), before);
    QCOMPARE(read(QDir(output).filePath(name)), QByteArray("kept bytes"));
    QCOMPARE(table(window)->rowCount(), 1);
}

void MainWindowOperationsTests::sourceNoPathsAskFlattensSelectedFolder() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("folder with space"));
    const QString name = QStringLiteral("δ name.txt");
    QVERIFY(QDir().mkpath(source));
    QFile input(QDir(source).filePath(name));
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("new bytes"), qint64(9));
    input.close();
    const QString archive = temp.filePath("flat.zip");
    const QString output = temp.filePath("output");
    QVERIFY(QDir().mkpath(output));
    QFile occupied(QDir(output).filePath(name));
    QVERIFY(occupied.open(QIODevice::WriteOnly));
    QCOMPARE(occupied.write("old bytes"), qint64(9));
    occupied.close();
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
    MainWindow window(&backend);
    window.openArchive(archive);
    QTRY_COMPARE(table(window)->rowCount(), 1);
    table(window)->selectRow(0);
    QSignalSpy preflight(&backend, &ArchiveBackend::overwriteRequired);
    bool answered = false;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible()) {
                answered = true;
                dialog->findChild<QPushButton *>("overwriteYes")->click();
            }
        }
    });
    answer.start(1);
    ExtractionOptions options;
    options.pathMode = ExtractionOptions::PathMode::NoPaths;
    options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
    window.extractTo(output, true, {}, options);
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QByteArray("new bytes"));
    // The source callback retains the explicit archive directory as empty.
    const QDir explicitDirectory(QDir(output).filePath("folder with space"));
    QVERIFY(explicitDirectory.exists());
    QVERIFY(explicitDirectory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void MainWindowOperationsTests::encryptedAskUsesSourceCallback_data() {
    QTest::addColumn<QString>("format");
    QTest::newRow("7z") << QStringLiteral("7z");
    QTest::newRow("zip") << QStringLiteral("zip");
}

void MainWindowOperationsTests::nestedFolderAskUsesSourceRelativePath() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString folderName = QStringLiteral("внутренняя папка");
    const QString fileName = QStringLiteral("δ name.txt");
    const QString source = temp.filePath(folderName);
    const QString archive = temp.filePath("nested.zip");
    const QString output = temp.filePath("output");
    const QString target = QDir(output).filePath(fileName);
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(output));
    QFile input(QDir(source).filePath(fileName));
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("new bytes"), qint64(9));
    input.close();
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
    MainWindow window(&backend);
    window.openArchive(archive);
    QTRY_VERIFY(table(window)->rowCount() >= 1);
    int folderRow = -1;
    for (int row = 0; row < table(window)->rowCount(); ++row)
        if (table(window)->model()->index(row, 0).data().toString() == folderName)
            folderRow = row;
    QVERIFY(folderRow >= 0);
    table(window)->selectRow(folderRow);
    action(window, "openItemAction")->trigger();
    QTRY_VERIFY(!PanelMatches(*table(window), fileName).isEmpty());
    QFile occupied(target);
    QVERIFY(occupied.open(QIODevice::WriteOnly));
    QCOMPARE(occupied.write("old bytes"), qint64(9));
    occupied.close();
    QSignalSpy preflight(&backend, &ArchiveBackend::overwriteRequired);
    bool answered = false;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible()) {
                answered = true;
                dialog->findChild<QPushButton *>("overwriteYes")->click();
            }
        }
    });
    answer.start(1);
    ExtractionOptions options;
    options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
    window.extractTo(output, false, {}, options);
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QByteArray("new bytes"));
    QVERIFY(!QFileInfo::exists(QDir(output).filePath(folderName + '/' + fileName)));

    const QString copyOutput = temp.filePath(QStringLiteral("F5 назначение"));
    QVERIFY(QDir().mkpath(copyOutput));
    table(window)->selectRow(filesystemRow(window, fileName));
    QString summary;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        summary = ToQString(static_cast<CopyDialog *>(dialog)->Info);
        dialog->findChild<QLineEdit *>("copyDestination")->setText(copyOutput);
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    QVERIFY(summary.contains(fileName));
    QFile copied(QDir(copyOutput).filePath(fileName));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("new bytes"));
    QVERIFY(!QFileInfo::exists(QDir(copyOutput).filePath(folderName + '/' + fileName)));

    const QString moveOutput = temp.filePath(QStringLiteral("F6 назначение"));
    QVERIFY(QDir().mkpath(moveOutput));
    QFile moveConflict(QDir(moveOutput).filePath(fileName));
    QVERIFY(moveConflict.open(QIODevice::WriteOnly));
    QCOMPARE(moveConflict.write("keep bytes"), qint64(10));
    moveConflict.close();
    QTimer cancelPrompt;
    bool cancellationReviewed = false;
    connect(&cancelPrompt, &QTimer::timeout, &window, [&] {
        if (TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget()))
            cancellationReviewed = true;
        if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            dialog && dialog->isVisible()) {
            dialog->findChild<QLineEdit *>("copyDestination")->setText(moveOutput);
            dialog->accept();
        }
        if (auto *dialog = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget()))
            dialog->findChild<QPushButton *>("overwriteCancel")->click();
    });
    cancelPrompt.start(1);
    action(window, "moveAction")->trigger();
    QTRY_VERIFY(cancellationReviewed);
    cancelPrompt.stop();
    QVERIFY(moveConflict.open(QIODevice::ReadOnly));
    QCOMPARE(moveConflict.readAll(), QByteArray("keep bytes"));
    moveConflict.close();
    QVERIFY(!PanelMatches(*table(window), fileName).isEmpty());
    QVERIFY(QFile::remove(moveConflict.fileName()));
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>("copyDestination")->setText(moveOutput);
        dialog->accept();
    });
    action(window, "moveAction")->trigger();
    QFile moved(QDir(moveOutput).filePath(fileName));
    QVERIFY(moved.open(QIODevice::ReadOnly));
    QCOMPARE(moved.readAll(), QByteArray("new bytes"));
    QVERIFY(!QFileInfo::exists(QDir(moveOutput).filePath(folderName + '/' + fileName)));
    QTRY_VERIFY(PanelMatches(*table(window), fileName).isEmpty());
}

void MainWindowOperationsTests::encryptedAskUsesSourceCallback() {
    QFETCH(QString, format);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("секрет с пробелом.txt"));
    const QString archive = temp.filePath("encrypted." + format);
    const QString output = temp.filePath("output");
    const QString target = QDir(output).filePath(QFileInfo(source).fileName());
    const QString password = QStringLiteral("test-only-password");
    QVERIFY(QDir().mkpath(output));
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("new secret"), qint64(10));
    input.close();
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, {}, password));
    QFile archiveInput(archive);
    QVERIFY(archiveInput.open(QIODevice::ReadOnly));
    const QByteArray archiveBytes = archiveInput.readAll();
    archiveInput.close();
    MainWindow window(&backend);
    QTimer openPassword;
    connect(&openPassword, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "passwordDialog")
            return;
        auto *edit = dialog->findChild<QLineEdit *>("passwordEdit");
        QVERIFY(edit);
        edit->setText(password);
        dialog->accept();
    });
    openPassword.start(1);
    window.openArchive(archive);
    openPassword.stop();
    QTRY_COMPARE(table(window)->rowCount(), 1);
    finished.clear();
    QFile occupied(target);
    QVERIFY(occupied.open(QIODevice::WriteOnly));
    QCOMPARE(occupied.write("old secret"), qint64(10));
    occupied.close();
    QSignalSpy preflight(&backend, &ArchiveBackend::overwriteRequired);
    bool answered = false;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = dynamic_cast<COverwriteDialog *>(widget);
                dialog && dialog->isVisible()) {
                answered = true;
                dialog->findChild<QPushButton *>("overwriteYes")->click();
            }
        }
    });
    answer.start(1);
    ExtractionOptions options;
    options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
    bool sawFailure = false;
    bool closeScheduled = false;
    QTimer dismissFailure;
    connect(&dismissFailure, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            if (auto *result = TestProgressDialog::ResultWindow(widget)) {
                sawFailure = true;
                if (!closeScheduled) {
                    closeScheduled = true;
                    QTimer::singleShot(50, result,
                                       [result] { TestProgressDialog::CloseResult(*result); });
                }
            } else if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                message->accept();
            }
        }
        auto *progress = window.findChild<QDialog *>("progressDialog");
        auto *close = progress ? progress->findChild<QPushButton *>("IDCANCEL") : nullptr;
        if (TestProgressDialog::IsCloseButton(close)) {
            sawFailure = true;
            if (!closeScheduled) {
                closeScheduled = true;
                QTimer::singleShot(50, &window, [&window] {
                    auto *progress = window.findChild<QDialog *>("progressDialog");
                    auto *close =
                        progress ? progress->findChild<QPushButton *>("IDCANCEL") : nullptr;
                    if (TestProgressDialog::IsCloseButton(close))
                        close->click();
                });
            }
        }
    });
    dismissFailure.start(1);
    window.extractTo(output, false, QStringLiteral("wrong-password"), options);
    dismissFailure.stop();
    QVERIFY(sawFailure);
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QByteArray("old secret"));
    occupied.close();
    QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden),
             QStringList{QFileInfo(target).fileName()});
    QVERIFY(archiveInput.open(QIODevice::ReadOnly));
    QCOMPARE(archiveInput.readAll(), archiveBytes);
    archiveInput.close();
    answered = false;
    window.extractTo(output, false, password, options);
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QByteArray("new secret"));
    QCOMPARE(QDir(output).entryList(QDir::Files | QDir::Hidden),
             QStringList{QFileInfo(target).fileName()});
}
