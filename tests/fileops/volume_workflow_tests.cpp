#include "dialogs/split/SplitDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class VolumeWorkflowTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static void navigate(MainWindow &window, const QString &path) {
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
    }
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static void language(bool russian) {
        const QString path = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString(russian ? "ru" : "-"), path);
        ReloadLang(path);
        QtNotifyLanguageChanged();
    }
  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        language(false);
    }
    void cleanup() { language(false); }

    void sourceDestinationUsesOtherFilesystemPanel_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("combine");
        QTest::addColumn<bool>("russian");
        for (unsigned pane : {0u, 1u})
            for (bool combine : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(pane).arg(combine)))
                    << pane << combine << (pane == 1);
    }
    void sourceDestinationUsesOtherFilesystemPanel() {
        QFETCH(unsigned, pane);
        QFETCH(bool, combine);
        QFETCH(bool, russian);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath(QStringLiteral("исходная папка"));
        const QString output = fixture.filePath(QStringLiteral("другая папка"));
        const QString sourcePath = input;
        QVERIFY(QDir().mkpath(sourcePath));
        QVERIFY(QDir().mkpath(output));
        const QString name = combine ? "source.bin.001" : "source.bin";
        QVERIFY(write(QDir(sourcePath).filePath(name), "first"));
        if (combine)
            QVERIFY(write(QDir(sourcePath).filePath("source.bin.002"), "second"));
        QVERIFY(write(QDir(output).filePath("keep.txt"), "peer bytes"));
        ControlledBackend backend;
        MainWindow window(&backend);
        language(russian);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, output);
        QTRY_COMPARE(peer->rowCount(), 1);
        peer->selectRow(0);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        const auto peerSelection = peer->selectionModel()->selection();
        auto *source = openPanel(window, pane, input);
        QTRY_COMPARE(source->rowCount(), combine ? 2 : 1);
        for (int row = 0; row < source->rowCount(); ++row)
            if (source->model()->index(row, 0).data(Qt::UserRole + 3).toString() == name)
                source->selectRow(row);
        QCOMPARE(source->selectionModel()->selectedRows().size(), 1);
        source->setFocus();
        QString defaultPath;
        bool handled = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>(combine ? "combineDialog" : "splitDialog");
            if (!dialog)
                return;
            handled = true;
            defaultPath =
                dialog->findChild<QComboBox *>(combine ? "copyDestination" : "volumeDestination")
                    ->currentText();
            if (!combine)
                dialog->findChild<QComboBox *>("volumeSize")
                    ->setCurrentText("2 9223372036854775808");
            dialog->accept();
        });
        action(window, combine ? "combineFilesAction" : "splitFileAction")->trigger();
        QVERIFY(handled);
        // Join the operation even on the old implementation, then report the
        // wrong default without leaving a running worker during assertion exit.
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        const auto selected = source->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.front().data(Qt::UserRole + 3).toString(), name);
        QCOMPARE(QDir::cleanPath(defaultPath), output);
        QCOMPARE(read(QDir(output).filePath("keep.txt")), QByteArray("peer bytes"));
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->selectionModel()->selection(), peerSelection);
        QCOMPARE(read(QDir(sourcePath).filePath(name)), QByteArray("first"));
        if (combine)
            QCOMPARE(read(QDir(output).filePath("source.bin")), QByteArray("firstsecond"));
        else {
            QByteArray bytes;
            for (int part = 1; part <= 2; ++part)
                bytes += read(QDir(output).filePath(
                    name + QString(".%1").arg(part, 3, 10, QLatin1Char('0'))));
            QCOMPARE(bytes, QByteArray("first"));
        }
    }

    void acceptedPlanKeepsItsOwnerAfterPeerFocus_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("combine");
        for (unsigned pane : {0u, 1u})
            for (bool combine : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(pane).arg(combine)))
                    << pane << combine;
    }
    void acceptedPlanKeepsItsOwnerAfterPeerFocus() {
        QFETCH(unsigned, pane);
        QFETCH(bool, combine);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath(QStringLiteral("источник δ"));
        const QString output = fixture.filePath(QStringLiteral("результат 空"));
        QVERIFY(QDir().mkpath(input) && QDir().mkpath(output));
        const QString name = combine ? "source.bin.001" : "source.bin";
        QVERIFY(write(QDir(input).filePath(name), "first"));
        if (combine)
            QVERIFY(write(QDir(input).filePath("source.bin.002"), "second"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, fixture.path());
        action(window, "openRootAction")->trigger();
        QVERIFY(peer->sourcePanel->IsFSDrivesFolder());
        auto *peerAddress = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane))
                                ->findChild<QLineEdit *>("archivePath");
        const QString peerPath = peerAddress->text();
        const int peerRows = peer->rowCount();
        auto *source = openPanel(window, pane, input);
        QTRY_COMPARE(source->rowCount(), combine ? 2 : 1);
        source->selectRow(0);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        bool peerFocused = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>(combine ? "combineDialog" : "splitDialog");
            QVERIFY(dialog);
            dialog->findChild<QComboBox *>(combine ? "copyDestination" : "volumeDestination")
                ->setCurrentText(output);
            if (!combine)
                dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("2");
            connect(dialog, &QDialog::finished, &window, [&] {
                window.activateWindow();
                peerAddress->setFocus();
                QTRY_VERIFY(peerAddress->hasFocus());
                peerFocused = true;
            });
            dialog->accept();
        });
        action(window, combine ? "combineFilesAction" : "splitFileAction")->trigger();
        QVERIFY(peerFocused);
        QTRY_VERIFY(source->isEnabled() && action(window, "createFolderAction")->isEnabled());
        QCOMPARE(read(QDir(input).filePath(name)), QByteArray("first"));
        if (combine)
            QCOMPARE(read(QDir(output).filePath("source.bin")), QByteArray("firstsecond"));
        else
            QCOMPARE(read(QDir(output).filePath(name + ".001")) +
                         read(QDir(output).filePath(name + ".002")) +
                         read(QDir(output).filePath(name + ".003")),
                     QByteArray("first"));
        QCOMPARE(peer->rowCount(), peerRows);
        QCOMPARE(peerAddress->text(), peerPath);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());
        action(window, "backAction")->trigger();
        QVERIFY(peer->sourcePanel->IsRootFolder());
        QVERIFY(peerAddress->text().isEmpty());
    }

    void splitCreatesNewDestinationAndCombinesIntoAnother_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("English") << false;
        QTest::newRow("Russian") << true;
    }
    void splitCreatesNewDestinationAndCombinesIntoAnother() {
        QFETCH(bool, russian);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QStringLiteral("исходник с пробелом.bin");
        const QByteArray bytes(75, 'x');
        QVERIFY(write(temp.filePath(name), bytes));
        const QString destination = temp.filePath(QStringLiteral("новая папка/части"));
        ControlledBackend backend;
        MainWindow window(&backend);
        language(russian);
        navigate(window, temp.path());
        table(window)->selectRow(filesystemRow(window, name));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CSplitDialog *>("splitDialog");
            if (!dialog)
                return;
            dialog->findChild<QComboBox *>("volumeDestination")->setCurrentText(destination);
            dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("10 20");
            dialog->accept();
        });
        action(window, "splitFileAction")->trigger();
        auto *splitProgress =
            dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(splitProgress);
        QVERIFY(!splitProgress->findChild<QPushButton *>("IDB_PAUSE")->isEnabled());
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        QCOMPARE(window.statusBar()->currentMessage(),
                 russian ? QStringLiteral("Файл разделён.") : QString("File split."));
        QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 5);
        QByteArray restored;
        for (int i = 1; i <= 5; ++i)
            restored +=
                read(QDir(destination)
                         .filePath(name + QStringLiteral(".%1").arg(i, 3, 10, QLatin1Char('0'))));
        QCOMPARE(restored, bytes);
        QCOMPARE(read(temp.filePath(name)), bytes);

        navigate(window, destination);
        table(window)->selectRow(filesystemRow(window, name + ".001"));
        const QString combined = temp.filePath(QStringLiteral("другая новая папка"));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("combineDialog");
            if (!dialog)
                return;
            dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(combined);
            dialog->accept();
        });
        action(window, "combineFilesAction")->trigger();
        auto *combineProgress =
            dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(combineProgress);
        QVERIFY(!combineProgress->findChild<QPushButton *>("IDB_PAUSE")->isEnabled());
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        QCOMPARE(window.statusBar()->currentMessage(),
                 russian ? QStringLiteral("Файлы объединены.") : QString("Files combined."));
        QCOMPARE(read(QDir(combined).filePath(name)), bytes);
        QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 5);
        QVERIFY(QDir(combined).entryList({"*.sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void splitConfirmationAndCancel_data() {
        QTest::addColumn<int>("choice");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<int>("windowWidth");
        const char *names[] = {"cancel-dialog", "decline-100-parts", "confirm-100-parts",
                               "cancel-100-parts-confirmation"};
        for (bool russian : {false, true})
            for (int windowWidth : {1100, 640})
                for (int choice = 0; choice < 4; ++choice)
                    QTest::newRow(qPrintable(QString::fromLatin1(names[choice]) +
                                             (russian ? "-ru-" : "-en-") +
                                             QString::number(windowWidth)))
                        << choice << russian << windowWidth;
    }
    void splitConfirmationAndCancel() {
        QFETCH(int, choice);
        QFETCH(bool, russian);
        QFETCH(int, windowWidth);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(write(temp.filePath("source.bin"), QByteArray(100, 'x')));
        const QString destination = temp.filePath(QStringLiteral("папка назначения"));
        ControlledBackend backend;
        MainWindow window(&backend);
        language(russian);
        window.resize(windowWidth, windowWidth == 640 ? 380 : 640);
        window.show();
        navigate(window, temp.path());
        table(window)->selectRow(filesystemRow(window, "source.bin"));
        bool dialogHandled = false;
        bool confirmationSeen = false;
        QString confirmationTitle, confirmationText;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (!dialogHandled) {
                if (auto *dialog = window.findChild<CSplitDialog *>("splitDialog")) {
                    dialogHandled = true;
                    dialog->findChild<QComboBox *>("volumeDestination")
                        ->setCurrentText(destination);
                    dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("1");
                    if (choice == 0)
                        dialog->reject();
                    else
                        dialog->accept();
                }
            }
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    confirmationSeen = box->text().contains("100");
                    confirmationTitle = box->windowTitle();
                    confirmationText = box->text();
                    auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                    const bool textFits = label && UiInspection::fullyVisible(*label, *box) &&
                                          UiInspection::labelFits(*label);
                    bool buttonsVisible = true;
                    for (auto *button : box->buttons())
                        buttonsVisible =
                            buttonsVisible && UiInspection::fullyVisible(*button, *box);
                    const bool report =
                        UiInspection::writeReport(*box, QString("language-split-confirm-%1-%2-%3")
                                                            .arg(russian ? "ru" : "en")
                                                            .arg(windowWidth)
                                                            .arg(choice));
                    box->button(choice == 2   ? QMessageBox::Yes
                                : choice == 3 ? QMessageBox::Cancel
                                              : QMessageBox::No)
                        ->click();
                    QVERIFY(textFits && buttonsVisible && report);
                }
        });
        timer.start(1);
        action(window, "splitFileAction")->trigger();
        timer.stop();
        QVERIFY(dialogHandled);
        QCOMPARE(confirmationSeen, choice != 0);
        if (confirmationSeen) {
#ifdef Q_OS_MACOS
            // QMessageBox::setWindowTitle ignores titles on macOS.
            QCOMPARE(confirmationTitle, QString());
#else
            QCOMPARE(confirmationTitle, russian ? QStringLiteral("Подтверждение разбиения")
                                                : QString("Confirm Splitting"));
#endif
            QCOMPARE(confirmationText,
                     russian ? QStringLiteral("Вы действительно хотите разбить файл на 100 частей?")
                             : QString("Are you sure you want to split file into 100 volumes?"));
        }
        QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        QCOMPARE(QFileInfo::exists(destination), choice == 2);
        if (choice == 2)
            QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 100);
        QCOMPARE(read(temp.filePath("source.bin")), QByteArray(100, 'x'));
    }

    void invalidDestinationReportsErrorWithoutChangingSource() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(write(temp.filePath("source.bin"), QByteArray(75, 'x')));
        const QString destination = temp.filePath("occupied");
        QVERIFY(write(destination, "keep"));
        ControlledBackend backend;
        MainWindow window(&backend);
        navigate(window, temp.path());
        table(window)->selectRow(filesystemRow(window, "source.bin"));
        bool submitted = false;
        bool errorSeen = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (!submitted)
                if (auto *dialog = window.findChild<CSplitDialog *>("splitDialog")) {
                    submitted = true;
                    dialog->findChild<QComboBox *>("volumeDestination")
                        ->setCurrentText(destination);
                    dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("10");
                    dialog->accept();
                }
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errorSeen = true;
                    box->accept();
                }
        });
        timer.start(1);
        action(window, "splitFileAction")->trigger();
        timer.stop();
        QVERIFY(submitted && errorSeen);
        QCOMPARE(read(destination), QByteArray("keep"));
        QCOMPARE(read(temp.filePath("source.bin")), QByteArray(75, 'x'));
        QVERIFY(action(window, "createFolderAction")->isEnabled());
        QVERIFY(QDir(temp.path()).entryList({"*.001"}, QDir::Files).isEmpty());
    }
};
QTEST_MAIN(VolumeWorkflowTests)
#include "volume_workflow_tests.moc"
