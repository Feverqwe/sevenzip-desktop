#include "window_operations.h"

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
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
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
    answered = false;
    table(window)->selectRow(0);
    window.extractTo(temp.filePath("destination"), true, {}, options);
    QVERIFY(answered);
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("preserved"));
    old.close();

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
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("preserved"));
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
        summary = dialog->findChild<QPlainTextEdit *>("copySelectionInfo")->toPlainText();
        dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(copyOutput);
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
    connect(&cancelPrompt, &QTimer::timeout, &window, [&] {
        if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            dialog && dialog->isVisible()) {
            dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(moveOutput);
            dialog->accept();
        }
        if (auto *dialog = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget()))
            dialog->findChild<QPushButton *>("overwriteCancel")->click();
    });
    cancelPrompt.start(1);
    action(window, "moveAction")->trigger();
    cancelPrompt.stop();
    QVERIFY(moveConflict.open(QIODevice::ReadOnly));
    QCOMPARE(moveConflict.readAll(), QByteArray("keep bytes"));
    moveConflict.close();
    QVERIFY(!PanelMatches(*table(window), fileName).isEmpty());
    QVERIFY(QFile::remove(moveConflict.fileName()));
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(moveOutput);
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
            if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                if (message->objectName() == "progressFinalMessage") {
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
    QVERIFY(occupied.readAll() != QByteArray("new secret"));
    occupied.close();
    QVERIFY(archiveInput.open(QIODevice::ReadOnly));
    QCOMPARE(archiveInput.readAll(), archiveBytes);
    archiveInput.close();
    // The source ZIP callback can create/truncate the destination before
    // detecting a bad password. Restore the occupied-file retry fixture.
    QVERIFY(occupied.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(occupied.write("old secret"), qint64(10));
    occupied.close();
    answered = false;
    window.extractTo(output, false, password, options);
    answer.stop();
    QVERIFY(answered);
    QCOMPARE(preflight.size(), 0);
    QVERIFY(occupied.open(QIODevice::ReadOnly));
    QCOMPARE(occupied.readAll(), QByteArray("new secret"));
}
