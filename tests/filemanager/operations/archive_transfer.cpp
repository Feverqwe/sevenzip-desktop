#include "support/delete_reply.h"
#include "window_operations.h"
#include <QLineEdit>

void MainWindowOperationsTests::damagedArchiveTransferPreservesBothArchives_data() {
    QTest::addColumn<bool>("move");
    QTest::newRow("copy") << false;
    QTest::newRow("move") << true;
}

void MainWindowOperationsTests::damagedArchiveTransferPreservesBothArchives() {
    QFETCH(bool, move);
    QTemporaryDir fixture;
    const QString sourceArchive = fixture.filePath(QStringLiteral("источник 空.zip"));
    const QString targetArchive = fixture.filePath(QStringLiteral("назначение 空.zip"));
    const QString name = QStringLiteral("повреждённый файл.txt");
    NativeArchiveFixture::Item incoming;
    incoming.Path = name;
    incoming.Bytes = QByteArray("source payload for CRC failure").repeated(16);
    QVERIFY(NativeArchiveFixture::Write(sourceArchive, {incoming}, "zip", {}, true, true));
    NativeArchiveFixture::Item kept;
    kept.Path = "keep.txt";
    kept.Bytes = "destination sentinel";
    QVERIFY(NativeArchiveFixture::Write(targetArchive, {kept}, "zip"));
    const auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QByteArray damaged = read(sourceArchive);
    const auto payload = damaged.indexOf(incoming.Bytes);
    QVERIFY(payload >= 0);
    damaged[payload] = '!';
    QFile sourceFile(sourceArchive);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(sourceFile.write(damaged), damaged.size());
    sourceFile.close();
    const QByteArray targetBefore = read(targetArchive);
    QVERIFY(!targetBefore.isEmpty());

    ArchiveBackend backend;
    MainWindow window(&backend);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    action(window, "twoPanelsAction")->trigger();
    QVERIFY(action(window, "twoPanelsAction")->isChecked());
    auto *source =
        window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
    auto *target =
        window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>("archiveTable");
    source->setFocus();
    QTRY_VERIFY(source->hasFocus());
    window.openArchive(sourceArchive);
    QVERIFY(WindowActivation::waitForActive(window));
    target->setFocus();
    QTRY_VERIFY(target->hasFocus());
    window.openArchive(targetArchive);
    QVERIFY(WindowActivation::waitForActive(window));
    source->setFocus();
    QTRY_VERIFY(source->hasFocus());
    QTRY_COMPARE(source->rowCount(), 1);
    source->selectRow(0);

    QStringList sourceErrors, warnings;
    QTimer driver;
    connect(&driver, &QTimer::timeout, &window, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (auto *dialog = dynamic_cast<CopyDialog *>(modal))
            dialog->accept();
        else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            warnings.append(box->text());
            box->accept();
        } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
            auto *close = progress->findChild<QPushButton *>("IDCANCEL");
            if (TestProgressDialog::IsCloseButton(close)) {
                FOR_VECTOR(i, progress->Sync.Messages)
                sourceErrors.append(ToQString(progress->Sync.Messages[i]));
                close->click();
            }
        }
    });
    driver.start(1);
    action(window, move ? "moveAction" : "copyAction")->trigger();
    driver.stop();
    QVERIFY2(sourceErrors.join('\n').contains(name), qPrintable(sourceErrors.join('\n')));
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QCOMPARE(read(sourceArchive), damaged);
    QCOMPARE(read(targetArchive), targetBefore);
    QTRY_COMPARE(target->rowCount(), 1);
    QCOMPARE(target->model()->index(0, 0).data().toString(), QString("keep.txt"));
    QTRY_COMPARE(window.focusWidget(), source);
    QVERIFY(WindowActivation::waitForActive(window));
    QTRY_VERIFY(source->hasFocus());
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
}

void MainWindowOperationsTests::archivePanelsCopyAndMoveThroughStaging_data() {
    QTest::addColumn<bool>("russian");
    QTest::newRow("en") << false;
    QTest::newRow("ru") << true;
}

void MainWindowOperationsTests::archivePanelsCopyAndMoveThroughStaging() {
    QFETCH(bool, russian);
    const QString engine = QStandardPaths::findExecutable("7z");
    QVERIFY2(!engine.isEmpty(), "7z required for archive transfer fixture");
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
    const QString settings = fixture.filePath("transfer-settings.ini");
    qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
    SaveRegLang(UString(russian ? "ru" : "-"), settings);
    const auto restoreLanguage = qScopeGuard([settings] {
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    });
    const QString sourceDir = fixture.filePath("source");
    const QString targetDir = fixture.filePath("target");
    QVERIFY(QDir().mkpath(sourceDir));
    QVERIFY(QDir().mkpath(targetDir));
    const QString sourceArchive = fixture.filePath("source.zip");
    const QString targetArchive = fixture.filePath("target.zip");
    for (const QString &name : {QStringLiteral("copy me.txt"), QStringLiteral("move me.txt")}) {
        QFile file(QDir(sourceDir).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(name.toUtf8()), qint64(name.toUtf8().size()));
    }
    QFile keep(QDir(targetDir).filePath("keep.txt"));
    QVERIFY(keep.open(QIODevice::WriteOnly));
    QCOMPARE(keep.write("keep"), qint64(4));
    keep.close();
    QProcess createSource;
    createSource.setWorkingDirectory(sourceDir);
    createSource.start(engine, {"a", "-tzip", "--", sourceArchive, "copy me.txt", "move me.txt"});
    QVERIFY(createSource.waitForFinished(10000));
    QCOMPARE(createSource.exitCode(), 0);
    QProcess createTarget;
    createTarget.setWorkingDirectory(targetDir);
    createTarget.start(engine, {"a", "-tzip", "--", targetArchive, "keep.txt"});
    QVERIFY(createTarget.waitForFinished(10000));
    QCOMPARE(createTarget.exitCode(), 0);

    ArchiveBackend backend(nullptr, engine);
    MainWindow window(&backend);
    window.show();
    auto *left = window.findChild<QWidget *>("filePanel0");
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    action(window, "twoPanelsAction")->trigger();
    QVERIFY(action(window, "twoPanelsAction")->isChecked());
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    window.openArchive(sourceArchive);
    auto *right = window.findChild<QWidget *>("filePanel1");
    QVERIFY(right);
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    window.openArchive(targetArchive);
    QTRY_VERIFY_WITH_TIMEOUT(rightTable->rowCount() == 1 &&
                                 rightTable->model()->index(0, 0).data().toString() == "keep.txt",
                             10000);
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    const auto rowFor = [leftTable](const QString &name) {
        for (int row = 0; row < leftTable->rowCount(); ++row)
            if (leftTable->model()->index(row, 0).data().toString() == name)
                return row;
        return -1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(rowFor("copy me.txt") >= 0, 10000);
    leftTable->selectRow(rowFor("copy me.txt"));
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        QCOMPARE(QDir::cleanPath(dialog->destination()), QDir::cleanPath(targetArchive));
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    QCOMPARE(window.statusBar()->currentMessage(),
             russian ? QStringLiteral("Объекты скопированы в архив.")
                     : QString("Items copied into the archive."));
    QProcess verifyCopy;
    const QString copyOutput = fixture.filePath("copy-output");
    verifyCopy.start(engine, {"x", "-y", "-o" + copyOutput, "--", targetArchive});
    QVERIFY(verifyCopy.waitForFinished(10000));
    QCOMPARE(verifyCopy.exitCode(), 0);
    QFile copied(QDir(copyOutput).filePath("copy me.txt"));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("copy me.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(rowFor("move me.txt") >= 0, 10000);
    leftTable->selectRow(rowFor("move me.txt"));
    QFile targetBefore(targetArchive);
    QVERIFY(targetBefore.open(QIODevice::ReadOnly));
    const QByteArray beforeCanceledMove = targetBefore.readAll();
    targetBefore.close();
    bool approveDelete = false;
    QTimer answerDelete;
    connect(&answerDelete, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(widget);
                confirmation && confirmation->isVisible())
                ReplyDelete(*confirmation, approveDelete);
    });
    answerDelete.start(1);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        dialog->accept();
    });
    action(window, "moveAction")->trigger();
    QFile targetAfterCancel(targetArchive);
    QVERIFY(targetAfterCancel.open(QIODevice::ReadOnly));
    QCOMPARE(targetAfterCancel.readAll(), beforeCanceledMove);
    targetAfterCancel.close();
    QVERIFY(rowFor("move me.txt") >= 0);
    approveDelete = true;
    leftTable->selectRow(rowFor("move me.txt"));
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        dialog->accept();
    });
    action(window, "moveAction")->trigger();
    answerDelete.stop();
    QCOMPARE(window.statusBar()->currentMessage(),
             russian ? QStringLiteral("Объекты перемещены между архивами.")
                     : QString("Items moved between archives."));
    const QString movedOutput = fixture.filePath("move-output");
    QProcess verifyMove;
    verifyMove.start(engine, {"x", "-y", "-o" + movedOutput, "--", targetArchive});
    QVERIFY(verifyMove.waitForFinished(10000));
    QCOMPARE(verifyMove.exitCode(), 0);
    QFile moved(QDir(movedOutput).filePath("move me.txt"));
    QVERIFY(moved.open(QIODevice::ReadOnly));
    QCOMPARE(moved.readAll(), QByteArray("move me.txt"));
    QProcess listSource;
    listSource.start(engine, {"l", "-slt", "--", sourceArchive});
    QVERIFY(listSource.waitForFinished(10000));
    QCOMPARE(listSource.exitCode(), 0);
    QVERIFY(!listSource.readAllStandardOutput().contains("Path = move me.txt"));
}

void MainWindowOperationsTests::protectedArchivePanelsCopyWithStoredPasswords() {
    const QString engine = QStandardPaths::findExecutable("7z");
    QVERIFY2(!engine.isEmpty(), "7z required for protected archive transfer fixture");
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
    const QString sourceDir = fixture.filePath("source");
    const QString targetDir = fixture.filePath("target");
    QVERIFY(QDir().mkpath(sourceDir));
    QVERIFY(QDir().mkpath(targetDir));
    const QString sourceArchive = fixture.filePath("source.7z");
    const QString targetArchive = fixture.filePath("target.7z");
    const QString name = QStringLiteral("секрет с пробелом.txt");
    QFile incoming(QDir(sourceDir).filePath(name));
    QVERIFY(incoming.open(QIODevice::WriteOnly));
    QCOMPARE(incoming.write("secret bytes"), qint64(12));
    incoming.close();
    QFile keep(QDir(targetDir).filePath("keep.txt"));
    QVERIFY(keep.open(QIODevice::WriteOnly));
    QCOMPARE(keep.write("keep"), qint64(4));
    keep.close();
    const QString password = QStringLiteral("fixture-only-password");
    QProcess createSource;
    createSource.setWorkingDirectory(sourceDir);
    createSource.start(engine,
                       {"a", "-t7z", "-p" + password, "-mhe=on", "--", sourceArchive, name});
    QVERIFY(createSource.waitForFinished(10000));
    QCOMPARE(createSource.exitCode(), 0);
    QProcess createTarget;
    createTarget.setWorkingDirectory(targetDir);
    createTarget.start(engine,
                       {"a", "-t7z", "-p" + password, "-mhe=on", "--", targetArchive, "keep.txt"});
    QVERIFY(createTarget.waitForFinished(10000));
    QCOMPARE(createTarget.exitCode(), 0);

    ArchiveBackend backend(nullptr, engine);
    MainWindow window(&backend);
    int passwordPrompts = 0;
    QString unexpectedDialog;
    QTimer answerPassword;
    connect(&answerPassword, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
            unexpectedDialog = box->text();
            box->accept();
            return;
        }
        if (dialog->objectName() != "passwordDialog")
            return;
        ++passwordPrompts;
        dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
        dialog->accept();
    });
    answerPassword.start(1);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    action(window, "twoPanelsAction")->trigger();
    QVERIFY(action(window, "twoPanelsAction")->isChecked());
    auto *left = window.findChild<QWidget *>("filePanel0");
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    window.openArchive(sourceArchive);
    auto *right = window.findChild<QWidget *>("filePanel1");
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    window.openArchive(targetArchive);
    QTRY_VERIFY_WITH_TIMEOUT(rightTable->rowCount() == 1 &&
                                 rightTable->model()->index(0, 0).data().toString() == "keep.txt",
                             10000);
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    QCOMPARE(leftTable->rowCount(), 1);
    leftTable->selectRow(0);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        QCOMPARE(QDir::cleanPath(dialog->destination()), QDir::cleanPath(targetArchive));
        dialog->accept();
    });
    action(window, "copyAction")->trigger();
    answerPassword.stop();
    QVERIFY2(unexpectedDialog.isEmpty(), qPrintable(unexpectedDialog));
    QVERIFY(passwordPrompts >= 2);
    QTRY_COMPARE(rightTable->rowCount(), 2);
    const QString output = fixture.filePath("output");
    QProcess extract;
    extract.start(engine, {"x", "-y", "-p" + password, "-o" + output, "--", targetArchive});
    QVERIFY(extract.waitForFinished(10000));
    QCOMPARE(extract.exitCode(), 0);
    QFile copied(QDir(output).filePath(name));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("secret bytes"));
}

void MainWindowOperationsTests::nestedArchivePanelCannotWriteIntoOppositeArchive() {
    const QString engine = QStandardPaths::findExecutable("7z");
    QVERIFY2(!engine.isEmpty(), "7z required for nested archive transfer fixture");
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
    const QString innerDir = fixture.filePath("inner");
    const QString outerDir = fixture.filePath("outer");
    const QString targetDir = fixture.filePath("target");
    SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    QVERIFY(QDir().mkpath(innerDir));
    QVERIFY(QDir().mkpath(outerDir));
    QVERIFY(QDir().mkpath(targetDir));
    const QString innerArchive = QDir(outerDir).filePath("inner.zip");
    const QString outerArchive = fixture.filePath("outer.zip");
    const QString targetArchive = fixture.filePath("target.zip");
    QFile payload(QDir(innerDir).filePath("payload.txt"));
    QVERIFY(payload.open(QIODevice::WriteOnly));
    QCOMPARE(payload.write("inside"), qint64(6));
    payload.close();
    QFile keep(QDir(targetDir).filePath("keep.txt"));
    QVERIFY(keep.open(QIODevice::WriteOnly));
    QCOMPARE(keep.write("keep"), qint64(4));
    keep.close();
    const auto create = [&](const QString &root, const QString &archive, const QString &entry) {
        QProcess process;
        process.setWorkingDirectory(root);
        process.start(engine, {"a", "-tzip", "--", archive, entry});
        return process.waitForFinished(10000) && process.exitCode() == 0;
    };
    QVERIFY(create(innerDir, innerArchive, "payload.txt"));
    QVERIFY(create(outerDir, outerArchive, "inner.zip"));
    QVERIFY(create(targetDir, targetArchive, "keep.txt"));
    QFile before(targetArchive);
    QVERIFY(before.open(QIODevice::ReadOnly));
    const QByteArray originalTarget = before.readAll();
    before.close();

    ArchiveBackend backend(nullptr, engine);
    MainWindow window(&backend);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    action(window, "twoPanelsAction")->trigger();
    QVERIFY(action(window, "twoPanelsAction")->isChecked());
    auto *left = window.findChild<QWidget *>("filePanel0");
    auto *leftTable = left->findChild<FileTable *>("archiveTable");
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    window.openArchive(outerArchive);
    QTRY_VERIFY(leftTable->rowCount() == 1 &&
                leftTable->model()->index(0, 0).data().toString() == "inner.zip");
    auto *right = window.findChild<QWidget *>("filePanel1");
    auto *rightTable = right->findChild<FileTable *>("archiveTable");
    QVERIFY(WindowActivation::waitForActive(window));
    rightTable->setFocus();
    QTRY_VERIFY(rightTable->hasFocus());
    window.openArchive(targetArchive);
    QTRY_VERIFY(rightTable->rowCount() == 1 &&
                rightTable->model()->index(0, 0).data().toString() == "keep.txt");
    QVERIFY(WindowActivation::waitForActive(window));
    leftTable->setFocus();
    QTRY_VERIFY(leftTable->hasFocus());
    leftTable->selectRow(0);
    action(window, "openInsideAction")->trigger();
    QTRY_VERIFY(leftTable->rowCount() == 1 &&
                leftTable->model()->index(0, 0).data().toString() == "payload.txt");
    leftTable->selectRow(0);
    QString warning;
    QTimer answerWarning;
    connect(&answerWarning, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                warning = box->text();
                box->accept();
            }
    });
    answerWarning.start(1);
    bool copyPresented = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<CopyDialog *>("copyDialog");
        QVERIFY(dialog);
        const auto rejectUnanswered = qScopeGuard([&] {
            if (dialog->isVisible())
                dialog->reject();
        });
        copyPresented = true;
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(dialog->destination())),
                 QDir::cleanPath(targetArchive));
        dialog->accept();
    });
    QVERIFY(action(window, "copyAction")->isEnabled());
    action(window, "copyAction")->trigger();
    answerWarning.stop();
    QVERIFY(copyPresented);
    QCOMPARE(warning, QString("Transfer between these archives is unavailable."));
    QFile after(targetArchive);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), originalTarget);
}

void MainWindowOperationsTests::movesRealArchiveEntryToDirectory() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir("destination"));
    const QString source = temp.filePath(QStringLiteral("файл с пробелом.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("archive move"), 12);
    file.close();
    const QString archive = temp.filePath("move.7z");
    const QString destination = temp.filePath("destination");
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
    MainWindow window(&backend);
    window.openArchive(archive);
    QTRY_COMPARE(table(window)->rowCount(), 1);
    table(window)->selectRow(0);
    QTRY_VERIFY(action(window, "moveAction")->isEnabled());
    QTimer::singleShot(0, &window, [&destination] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<CopyDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
                dialog->accept();
            }
        }
    });
    action(window, "moveAction")->trigger();
    const QString extracted = QDir(destination).filePath(QFileInfo(source).fileName());
    QTRY_VERIFY(QFileInfo::exists(extracted));
    QTRY_COMPARE(table(window)->rowCount(), 0);
    QFile extractedFile(extracted);
    QVERIFY(extractedFile.open(QIODevice::ReadOnly));
    QCOMPARE(extractedFile.readAll(), QByteArray("archive move"));
}
