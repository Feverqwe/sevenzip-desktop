#include "window_operations.h"

void MainWindowOperationsTests::stage7ArchiveCommandStates() {
    ControlledBackend backend;
    MainWindow window(&backend);
    QVERIFY(backend.prepareArchiveFixture("first.7z"));
    backend.queueListAction([&] {
        const auto finish = qScopeGuard(
            [&] { backend.completeList({ArchiveEntry{QStringLiteral("file.txt"), 4, 4}}); });
        QVERIFY(!action(window, "copyAction")->isEnabled());
        QVERIFY(!action(window, "moveAction")->isEnabled());
    });
    window.openArchive("first.7z");
    table(window)->selectRow(0);
    QVERIFY(action(window, "copyAction")->isEnabled());
    QVERIFY(action(window, "moveAction")->isEnabled());
    QVERIFY(action(window, "deleteAction")->isEnabled());
    QVERIFY(action(window, "renameAction")->isEnabled());
    QVERIFY(backend.prepareArchiveFixture("second.zip"));
    backend.queueListAction([&] {
        const auto finish = qScopeGuard([&] { backend.completeList({}); });
        QVERIFY(!action(window, "copyAction")->isEnabled());
    });
    window.openArchive("second.zip");
    QVERIFY(action(window, "copyAction")->isEnabled());
    QVERIFY(action(window, "moveAction")->isEnabled());
    const int extractCalls = backend.extractCalls;

    action(window, "copyAction")->trigger();
    action(window, "moveAction")->trigger();
    QCOMPARE(backend.extractCalls, extractCalls);

    QVERIFY(!action(window, "deleteAction")->isEnabled());
    QVERIFY(!action(window, "renameAction")->isEnabled());
    QVERIFY(action(window, "testArchiveAction")->isEnabled());
    const auto retained = ToQString(table(window)->sourcePanel->GetCurrentFolderPrefix());
    QVERIFY(backend.prepareArchiveFixture("cancel.7z"));
    backend.queueListAction([&] { backend.cancel(); });
    window.openArchive("cancel.7z");
    QTRY_COMPARE(ToQString(table(window)->sourcePanel->GetCurrentFolderPrefix()), retained);
    QVERIFY(table(window)->sourcePanel->IsArcFolder());
    QTRY_VERIFY(action(window, "copyAction")->isEnabled());
    QCOMPARE(backend.extractCalls, extractCalls);
}

void MainWindowOperationsTests::stage7ArchiveMutations() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("исходный файл.txt"));
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("payload"), qint64(7));
    input.close();
    const QString archive = temp.filePath("mutate.7z");
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
    MainWindow window(&backend);
    window.openArchive(archive);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
    QTimer::singleShot(0, &window, [&window] {
        auto *dialog = window.findChild<CComboDialog *>("comboDialog");
        QVERIFY(dialog);
        dialog->findChild<QComboBox *>("comboValue")->setCurrentText(QStringLiteral("новая папка"));
        dialog->accept();
    });
    action(window, "createFolderAction")->trigger();
    const auto archiveRow = [&window](const QString &name) {
        for (int row = 0; row < table(window)->rowCount(); ++row)
            if (table(window)->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
                return row;
        return -1;
    };
    QTRY_VERIFY(archiveRow(QStringLiteral("новая папка")) >= 0);
    table(window)->selectRow(archiveRow(QStringLiteral("исходный файл.txt")));
    QTRY_VERIFY(action(window, "renameAction")->isEnabled());
    action(window, "renameAction")->trigger();
    auto *editor = table(window)->findChild<QLineEdit *>();
    QVERIFY(editor);
    editor->setFocus();
    editor->setText(QStringLiteral("новое имя.txt"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_VERIFY(archiveRow(QStringLiteral("новое имя.txt")) >= 0);
    table(window)->selectRow(archiveRow(QStringLiteral("новое имя.txt")));
    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->button(QMessageBox::No)->click();
    });
    action(window, "deleteAction")->trigger();
    QVERIFY(archiveRow(QStringLiteral("новое имя.txt")) >= 0);
    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->button(QMessageBox::Yes)->click();
    });
    action(window, "deleteAction")->trigger();
    QTRY_VERIFY(archiveRow(QStringLiteral("новое имя.txt")) < 0);
    QVERIFY(archiveRow(QStringLiteral("новая папка")) >= 0);
}

void MainWindowOperationsTests::encryptedArchiveMutationReusesSourcePassword() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("секретный файл.txt"));
    const QString archive = temp.filePath("protected.7z");
    const QString password = QStringLiteral("fixture-only-password");
    QFile input(source);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write("payload"), qint64(7));
    input.close();
    ArchiveBackend backend;
    QSignalSpy finished(&backend, &ArchiveBackend::finished);
    QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, {}, password, true));
    MainWindow window(&backend);
    int passwordPrompts = 0;
    QTimer passwordAnswer;
    connect(&passwordAnswer, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "passwordDialog")
            return;
        ++passwordPrompts;
        dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
        dialog->accept();
    });
    passwordAnswer.start(1);
    window.openArchive(archive);
    window.show();
    QVERIFY(WindowActivation::waitForActive(window));
    QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
    QTimer::singleShot(0, &window, [&window] {
        auto *dialog = window.findChild<CComboDialog *>("comboDialog");
        QVERIFY(dialog);
        dialog->findChild<QComboBox *>("comboValue")
            ->setCurrentText(QStringLiteral("закрытая папка"));
        dialog->accept();
    });
    action(window, "createFolderAction")->trigger();
    const auto archiveRow = [&window](const QString &name) {
        for (int row = 0; row < table(window)->rowCount(); ++row)
            if (table(window)->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
                return row;
        return -1;
    };
    QTRY_VERIFY(archiveRow(QStringLiteral("закрытая папка")) >= 0);
    table(window)->selectRow(archiveRow(QStringLiteral("секретный файл.txt")));
    QTRY_VERIFY(action(window, "renameAction")->isEnabled());
    action(window, "renameAction")->trigger();
    auto *editor = table(window)->findChild<QLineEdit *>();
    QVERIFY(editor);
    editor->setFocus();
    editor->setText(QStringLiteral("новый секрет.txt"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_VERIFY(archiveRow(QStringLiteral("новый секрет.txt")) >= 0);
    table(window)->selectRow(archiveRow(QStringLiteral("новый секрет.txt")));
    QTimer::singleShot(0, &window, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                box->button(QMessageBox::Yes)->click();
    });
    action(window, "deleteAction")->trigger();
    QTRY_VERIFY(archiveRow(QStringLiteral("новый секрет.txt")) < 0);
    QCOMPARE(passwordPrompts, 1);
    passwordAnswer.stop();
}

void MainWindowOperationsTests::addToOpenArchiveState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("новый файл.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("data");
    file.close();
    ControlledBackend backend;
    MainWindow window(&backend);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    QVERIFY(action(window, "toolbarAddAction")->isEnabled());
    const QString archive = temp.filePath("existing.zip");
    QVERIFY(backend.prepareArchiveFixture(archive));
    backend.queueListAction([&] {
        const auto finish = qScopeGuard([&] { backend.completeList({}); });
        QVERIFY(!action(window, "toolbarAddAction")->isEnabled());
    });
    window.openArchive(archive);
    QVERIFY(action(window, "toolbarAddAction")->isEnabled());

    bool dialogSeen = false;
    bool unsupported = false;
    QTimer answer;
    connect(&answer, &QTimer::timeout, &window, [&] {
        if (auto *dialog = qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget())) {
            dialogSeen = true;
            dialog->reject();
        } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            unsupported = true;
            box->accept();
        }
    });
    answer.start(1);
    action(window, "toolbarAddAction")->trigger();
    QVERIFY(!dialogSeen);
    QVERIFY(unsupported);

    QVERIFY(action(window, "toolbarAddAction")->isEnabled());
}

void MainWindowOperationsTests::createDialogAndStateTransitions() {
#ifndef SEVENZIP_CODEC_MODULE
    QSKIP("Source Add requires the official native module");
#endif
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString file =
        QFileInfo(temp.filePath(QStringLiteral("файл с пробелом.txt"))).absoluteFilePath();
    QFile source(file);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("data");
    source.close();
    const QString folder = temp.filePath(QStringLiteral("папка Unicode"));
    QVERIFY(QDir().mkpath(folder));
    const QString canonicalFile = QFileInfo(file).canonicalFilePath();
    const QString canonicalFolder = QFileInfo(folder).canonicalFilePath();
    ControlledBackend backend;
#ifdef SEVENZIP_CODEC_MODULE
    ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
    QCOMPARE(probe.result(), S_OK);
    QCOMPARE(backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), probe.version()),
             S_OK);
#endif
    MainWindow window(&backend);
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(temp.path());
    QTest::keyClick(address, Qt::Key_Return);
    QVERIFY(action(window, "toolbarAddAction")->isEnabled());
    for (int row = 0; row < table(window)->rowCount(); ++row) {
        if (table(window)->model()->index(row, 1).data().toString() == canonicalFile)
            table(window)->selectRow(row);
    }

    action(window, "toolbarAddAction")->trigger();
    QCOMPARE(backend.GuiRequests.size(), 1);
    QCOMPARE(backend.GuiRequests.last().Operation, QtOperationJob::Command::Compress);
    QVERIFY(backend.GuiRequests.last().ShowDialog);
    QCOMPARE(backend.GuiRequests.last().Paths.size(), 1);
    auto chooseSources = [&] {
        table(window)->clearSelection();
        for (int row = 0; row < table(window)->rowCount(); ++row) {
            const QString path = table(window)->model()->index(row, 1).data().toString();
            if (path == canonicalFile || path == canonicalFolder)
                table(window)->selectionModel()->select(table(window)->model()->index(row, 0),
                                                        QItemSelectionModel::Select |
                                                            QItemSelectionModel::Rows);
        }
        action(window, "toolbarAddAction")->trigger();
        QCOMPARE(backend.GuiRequests.size(), 2);
        QStringList paths = backend.GuiRequests.last().Paths;
        for (QString &path : paths)
            path = QDir::cleanPath(QDir::fromNativeSeparators(path))
                       .normalized(QString::NormalizationForm_C);
        QCOMPARE(paths.size(), 2);
        QVERIFY(paths.contains(canonicalFile.normalized(QString::NormalizationForm_C)));
        QVERIFY(paths.contains(canonicalFolder.normalized(QString::NormalizationForm_C)));
    };
    chooseSources();
    QVERIFY(table(window)->isEnabled());
    QVERIFY(action(window, "toolbarAddAction")->isEnabled());
    QCOMPARE(table(window)->rowCount(), 2);
}
