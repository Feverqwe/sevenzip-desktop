#include "window_operations.h"
#include <QLineEdit>

void MainWindowOperationsTests::twoPanelFilesystemCopyRefreshesDestination() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("источник")));
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("назначение")));
    const QString sourceDir = root.filePath(QStringLiteral("источник"));
    const QString destinationDir = root.filePath(QStringLiteral("назначение"));
    QFile original(QDir(sourceDir).filePath(QStringLiteral("файл с пробелом.txt")));
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("source bytes"), qint64(12));
    original.close();

    ControlledBackend backend;
    MainWindow window(&backend);
    auto *left = window.findChild<QWidget *>("filePanel0");
    QVERIFY(left);
    auto *leftPath = left->findChild<QLineEdit *>("archivePath");
    leftPath->setFocus();
    leftPath->setText(sourceDir);
    QTest::keyClick(leftPath, Qt::Key_Return);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    QTRY_VERIFY(leftTable->rowCount() > 0);

    action(window, "twoPanelsAction")->trigger();
    const auto restorePanels = qScopeGuard([&] {
        if (action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
    });
    auto *right = window.findChild<QWidget *>("filePanel1");
    QVERIFY(right);
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(rightTable);
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    auto *rightPath = right->findChild<QLineEdit *>("archivePath");
    rightPath->setFocus();
    rightPath->setText(destinationDir);
    QTest::keyClick(rightPath, Qt::Key_Return);
    QTRY_COMPARE(QFileInfo(rightPath->text()).canonicalFilePath(),
                 QFileInfo(destinationDir).canonicalFilePath());
    QTRY_COMPARE(rightTable->rowCount(), 0);

    leftTable->selectRow(0);
    leftTable->setFocus();
    QCoreApplication::processEvents();
    QVERIFY(action(window, "copyAction")->isEnabled());
    QString dialogDefault;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
        QVERIFY(destination);
        dialogDefault = destination->text();
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    QCOMPARE(QFileInfo(dialogDefault).canonicalFilePath(),
             QFileInfo(destinationDir).canonicalFilePath());
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
    const QString copied = QDir(destinationDir).filePath(QStringLiteral("файл с пробелом.txt"));
    QFile result(copied);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("source bytes"));
    QTRY_COMPARE(rightTable->rowCount(), 1);
    QCOMPARE(rightTable->model()->index(0, 0).data().toString(),
             QStringLiteral("файл с пробелом.txt"));
    QCOMPARE(leftTable->rowCount(), 1);
    QVERIFY(QFileInfo::exists(original.fileName()));
}

void MainWindowOperationsTests::twoPanelArchiveCopyRefreshesFilesystemDestination() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("назначение")));
    const QString destination = root.filePath(QStringLiteral("назначение"));
    const QString retainedName = QStringLiteral("keep focused δ.txt");
    const QString retainedPath = QDir(destination).filePath(retainedName);
    QFile retained(retainedPath);
    QVERIFY(retained.open(QIODevice::WriteOnly));
    QCOMPARE(retained.write("destination control"), qint64(19));
    retained.close();
    const QString source = root.filePath(QStringLiteral("архивный файл.txt"));
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("archive bytes"), qint64(13));
    input.close();
    const QString movedSource = root.filePath(QStringLiteral("для перемещения.txt"));
    QFile moveInput(movedSource);
    QVERIFY(moveInput.open(QIODevice::WriteOnly));
    QCOMPARE(moveInput.write("move bytes"), qint64(10));
    moveInput.close();
    const QString archive = root.filePath("source.zip");
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source, movedSource}));

    MainWindow window(&backend);
    window.openArchive(archive);
    window.show();
    auto *left = window.findChild<QWidget *>("filePanel0");
    QVERIFY(left);
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    QTRY_COMPARE(leftTable->rowCount(), 2);
    leftTable->sourcePanel->Set_AutoRefresh_Mode(false);
    action(window, "twoPanelsAction")->trigger();
    const auto restorePanels = qScopeGuard([&] {
        if (action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
    });
    auto *right = window.findChild<QWidget *>("filePanel1");
    QVERIFY(right);
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    auto *rightPath = right->findChild<QLineEdit *>("archivePath");
    rightPath->setText(destination);
    QTest::keyClick(rightPath, Qt::Key_Return);
    QTRY_COMPARE(rightTable->rowCount(), 1);
    rightTable->sourcePanel->Set_AutoRefresh_Mode(false);
    rightTable->selectRow(0);
    const auto verifyDestinationSelection = [&] {
        CSelectedState selection;
        rightTable->sourcePanel->SaveSelectedState(selection);
        QCOMPARE(ToQString(selection.FocusedName), retainedName);
        QCOMPARE(selection.SelectedNames.Size(), 1u);
        QCOMPARE(ToQString(selection.SelectedNames[0]), retainedName);
    };

    const auto leftRow = [leftTable](const QString &name) {
        for (int row = 0; row < leftTable->rowCount(); ++row)
            if (leftTable->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
                return row;
        return -1;
    };
    const int copyRow = leftRow(QFileInfo(source).fileName());
    QStringList leftNames;
    for (int row = 0; row < leftTable->rowCount(); ++row)
        leftNames << leftTable->model()->index(row, 0).data().toString();
    QVERIFY2(copyRow >= 0, qPrintable(leftNames.join(QStringLiteral(", "))));
    leftTable->selectRow(copyRow);
    leftTable->setFocus();
    QCoreApplication::processEvents();
    QVERIFY(action(window, "copyAction")->isEnabled());
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        QCOMPARE(QFileInfo(dialog->destination()).canonicalFilePath(),
                 QFileInfo(destination).canonicalFilePath());
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    const QString copied = QDir(destination).filePath(QFileInfo(source).fileName());
    QFile result(copied);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("archive bytes"));
    QTRY_COMPARE(rightTable->rowCount(), 2);
    verifyDestinationSelection();
    QTRY_VERIFY(left->isAncestorOf(window.focusWidget()));
    QCOMPARE(leftTable->rowCount(), 2);

    const int moveRow = leftRow(QFileInfo(movedSource).fileName());
    QVERIFY(moveRow >= 0);
    leftTable->selectRow(moveRow);
    rightTable->sourcePanel->SetAlternativeSelection(true);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        dialog->accept();
    });
    action(window, "moveAction")->trigger();
    QTRY_COMPARE(rightTable->rowCount(), 3);
    verifyDestinationSelection();
    QTRY_VERIFY(left->isAncestorOf(window.focusWidget()));
    QFile moved(QDir(destination).filePath(QFileInfo(movedSource).fileName()));
    QVERIFY(moved.open(QIODevice::ReadOnly));
    QCOMPARE(moved.readAll(), QByteArray("move bytes"));
    QTRY_COMPARE(leftTable->rowCount(), 1);
    QCOMPARE(
        leftTable->model()->index(0, 0).data().toString().normalized(QString::NormalizationForm_C),
        QFileInfo(source).fileName().normalized(QString::NormalizationForm_C));
    QVERIFY(retained.open(QIODevice::ReadOnly));
    QCOMPARE(retained.readAll(), QByteArray("destination control"));
}

void MainWindowOperationsTests::stage7ArchiveCopyAndDropCancellation() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir("in"));
    QVERIFY(QDir(temp.path()).mkdir("out"));
    const QString source = temp.filePath(QStringLiteral("drop ü.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("dropped");
    file.close();
    ControlledBackend backend;
    MainWindow window(&backend);
    window.show();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(source)});
    // Real filesystem-to-Add modals use native factories and are covered
    // by source_drop_add; this controlled archive fixture has no real file.
    openListedArchive(window, backend, "items.zip",
                      {ArchiveEntry{QStringLiteral("nested/file.txt"), 4, 4}});
    const int row = filesystemRow(window, "nested");
    QVERIFY(row >= 0);
    table(window)->selectRow(row);
    QString copyInfo;
    QTimer::singleShot(0, &window, [&temp, &copyInfo] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
                if (auto *destination = dialog->findChild<QLineEdit *>("copyDestination")) {
                    copyInfo = ToQString(static_cast<CopyDialog *>(dialog)->Info);
                    destination->setText(temp.filePath("out"));
                    dialog->accept();
                }
            }
        }
    });
    action(window, "copyAction")->trigger();
    QVERIFY(QDir::fromNativeSeparators(copyInfo).contains(QStringLiteral("nested/")));
    QCOMPARE(backend.extractCalls, 1);
    QCOMPARE(backend.lastExtractionOptions.pathMode, ExtractionOptions::PathMode::CurrentPaths);
    QCOMPARE(backend.lastSelected.size(), 1);
    QCOMPARE(QDir::fromNativeSeparators(backend.lastSelected.front()),
             QStringLiteral("nested/file.txt"));
    QVERIFY(!action(window, "copyAction")->isEnabled());
    backend.cancel();
    QTRY_VERIFY(TestProgressDialog::ResultWindow(QApplication::activeModalWidget()));
    TestProgressDialog::CloseResult(
        *TestProgressDialog::ResultWindow(QApplication::activeModalWidget()));
    QVERIFY(action(window, "copyAction")->isEnabled());
    QDragEnterEvent archiveEnter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                                 Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(table(window)->viewport(), &archiveEnter);
    QDropEvent archiveDrop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QTimer cancelAdd;
    cancelAdd.setInterval(5);
    bool cancelRequested = false;
    connect(&cancelAdd, &QTimer::timeout, &window, [&] {
        if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            cancelRequested = true;
            question->button(QMessageBox::No)->click();
        }
    });
    cancelAdd.start();
    QCoreApplication::sendEvent(table(window)->viewport(), &archiveDrop);
    cancelAdd.stop();

    QVERIFY(cancelRequested);
    QVERIFY(action(window, "copyAction")->isEnabled());
}

void MainWindowOperationsTests::fallbackMoveRetainsOneSettledResult_data() {
    QTest::addColumn<int>("outcome");
    QTest::newRow("failure") << 0;
    QTest::newRow("cancel") << 1;
    QTest::newRow("unconfirmed extraction") << 2;
}

void MainWindowOperationsTests::fallbackMoveRetainsOneSettledResult() {
    QFETCH(int, outcome);
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    ControlledBackend backend;
    MainWindow window(&backend);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    const QString name = QStringLiteral("retained 空.txt");
    openListedArchive(window, backend, fixture.filePath("source.zip"), {ArchiveEntry{name, 4, 4}});
    table(window)->selectRow(0);
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = window.findChild<CopyDialog *>("copyDialog")) {
            dialog->findChild<QLineEdit *>("copyDestination")->setText(fixture.path());
            dialog->accept();
        }
    });
    action(window, "moveAction")->trigger();
    QCOMPARE(backend.extractCalls, 1);
    auto *working = window.findChild<QDialog *>("progressDialog");
    QVERIFY(working);
    QCOMPARE(working->findChild<QLabel *>("progressSource")->accessibleDescription(),
             backend.lastArchive);
    QCOMPARE(working->findChild<QLabel *>("progressDestination")->accessibleDescription(),
             backend.lastExtractDestination);
    QVERIFY(working->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME")->text().isEmpty());
    if (outcome == 2)
        backend.completeExtraction();
    else
        backend.fail(QStringLiteral("fixture extraction result"), outcome == 1 ? E_ABORT : E_FAIL);
    QTRY_VERIFY(TestProgressDialog::ResultWindow(QApplication::activeModalWidget()));
    QApplication::processEvents();
    QList<CProgressDialog *> results;
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *result = TestProgressDialog::ResultWindow(widget);
            result && result->isVisible() && result->parentWidget() == &window)
            results.append(result);
    const auto dismiss = qScopeGuard([&] {
        for (auto *result : results)
            TestProgressDialog::CloseResult(*result);
    });
    QCOMPARE(results.size(), 1);
    QVERIFY(action(window, "moveAction")->isEnabled());
    QCOMPARE(table(window)->rowCount(), 1);
    QCOMPARE(table(window)->model()->index(0, 0).data().toString(), name);
}

void MainWindowOperationsTests::diskPanelCopiesIntoOppositeArchive() {
    const QString engine = QStandardPaths::findExecutable("7z");
    QVERIFY2(!engine.isEmpty(), "7z required for disk-to-archive fixture");
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const bool hadSettings = qEnvironmentVariableIsSet("SEVENZIP_SETTINGS_PATH");
    const QByteArray previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
    const auto restoreSettings = qScopeGuard([hadSettings, previousSettings] {
        if (hadSettings)
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings);
        else
            qunsetenv("SEVENZIP_SETTINGS_PATH");
    });
    qputenv("SEVENZIP_SETTINGS_PATH", fixture.filePath("transfer-settings.ini").toUtf8());
    SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    const QString disk = fixture.filePath("disk");
    const QString seed = fixture.filePath("seed");
    QVERIFY(QDir().mkpath(disk));
    QVERIFY(QDir().mkpath(seed));
    const QString archive = fixture.filePath("target.zip");
    const QString source = QDir(disk).filePath(QStringLiteral("новый файл.txt"));
    QFile incoming(source);
    QVERIFY(incoming.open(QIODevice::WriteOnly));
    QCOMPARE(incoming.write("source bytes"), qint64(12));
    incoming.close();
    QFile keep(QDir(seed).filePath("keep.txt"));
    QVERIFY(keep.open(QIODevice::WriteOnly));
    QCOMPARE(keep.write("keep"), qint64(4));
    keep.close();
    QProcess create;
    create.setWorkingDirectory(seed);
    create.start(engine, {"a", "-tzip", "--", archive, "keep.txt"});
    QVERIFY(create.waitForFinished(10000));
    QCOMPARE(create.exitCode(), 0);

    ArchiveBackend backend(nullptr, engine);
    MainWindow window(&backend);
    window.show();
    auto *left = window.findChild<QWidget *>("filePanel0");
    auto *leftAddress = left->findChild<QLineEdit *>("archivePath");
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    leftAddress->setText(disk);
    QTest::keyClick(leftAddress, Qt::Key_Return);
    int sourceRow = -1;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 for (int row = 0; row < leftTable->rowCount(); ++row)
                                     if (leftTable->model()->index(row, 0).data().toString() ==
                                         QStringLiteral("новый файл.txt")) {
                                         sourceRow = row;
                                         return true;
                                     }
                                 return false;
                             })(),
                             10000);
    action(window, "twoPanelsAction")->trigger();
    QVERIFY(action(window, "twoPanelsAction")->isChecked());
    auto *right = window.findChild<QWidget *>("filePanel1");
    QVERIFY(right);
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    window.openArchive(archive);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 for (int row = 0; row < rightTable->rowCount(); ++row)
                                     if (rightTable->model()->index(row, 0).data().toString() ==
                                         "keep.txt")
                                         return true;
                                 return false;
                             })(),
                             10000);
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    QVERIFY(sourceRow >= 0);
    leftTable->setFocus();
    leftTable->selectRow(sourceRow);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        QCOMPARE(QDir::cleanPath(dialog->destination()), QDir::cleanPath(archive));
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    QFile copied(source);
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("source bytes"));
    const QString extracted = fixture.filePath("extracted");
    QProcess extract;
    extract.start(engine, {"x", "-y", "-o" + extracted, "--", archive});
    QVERIFY(extract.waitForFinished(10000));
    QCOMPARE(extract.exitCode(), 0);
    QFile archived(QDir(extracted).filePath(QStringLiteral("новый файл.txt")));
    QVERIFY(archived.open(QIODevice::ReadOnly));
    QCOMPARE(archived.readAll(), QByteArray("source bytes"));
    QTRY_VERIFY(rightTable->rowCount() > 1);

    const QString movedSource = QDir(disk).filePath(QStringLiteral("move me.txt"));
    QFile moving(movedSource);
    QVERIFY(moving.open(QIODevice::WriteOnly));
    QCOMPARE(moving.write("move bytes"), qint64(10));
    moving.close();
    action(window, "refreshAction")->trigger();
    int moveRow = -1;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 for (int row = 0; row < leftTable->rowCount(); ++row)
                                     if (leftTable->model()->index(row, 0).data().toString() ==
                                         "move me.txt") {
                                         moveRow = row;
                                         return true;
                                     }
                                 return false;
                             })(),
                             10000);
    leftTable->selectRow(moveRow);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        QCOMPARE(QDir::cleanPath(dialog->destination()), QDir::cleanPath(archive));
        dialog->accept();
    });
    action(window, "moveAction")->trigger();
    QVERIFY(!QFileInfo::exists(movedSource));
    const QString movedExtracted = fixture.filePath("moved-extracted");
    QProcess verifyMove;
    verifyMove.start(engine, {"x", "-y", "-o" + movedExtracted, "--", archive});
    QVERIFY(verifyMove.waitForFinished(10000));
    QCOMPARE(verifyMove.exitCode(), 0);
    QFile moved(QDir(movedExtracted).filePath("move me.txt"));
    QVERIFY(moved.open(QIODevice::ReadOnly));
    QCOMPARE(moved.readAll(), QByteArray("move bytes"));
    QTRY_VERIFY(rightTable->rowCount() > 2);
}
