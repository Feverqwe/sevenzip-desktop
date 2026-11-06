#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QLineEdit>
#include <QScrollArea>

class UiCopySameTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }

    void drivePeerUsesCurrentDirectory_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("root");
        for (const char *language : {"-", "ru"})
            for (const QSize size : {QSize(440, 250), QSize(360, 200)})
                for (bool move : {false, true})
                    for (bool root : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(language)
                                                     .arg(size.width())
                                                     .arg(move)
                                                     .arg(root)))
                            << QString::fromLatin1(language) << size << move << root;
    }
    void drivePeerUsesCurrentDirectory() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QFETCH(bool, move);
        QFETCH(bool, root);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("контроль δ.txt");
        QFile input(fixture.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("control bytes"), qint64(13));
        input.close();

        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *source = left->findChild<FileTable *>("archiveTable");
        auto *peer = right->findChild<FileTable *>("archiveTable");
        QVERIFY(address && source && peer);
        address->setFocus();
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(source->rowCount(), 1);
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());
        action(window, "openRootAction")->trigger();
        QVERIFY(static_cast<FileTable *>(peer)->sourcePanel->IsFSDrivesFolder());
        if (root) {
            action(window, "parentFolderAction")->trigger();
            QVERIFY(static_cast<FileTable *>(peer)->sourcePanel->IsRootFolder());
        }
        QVERIFY(static_cast<FileTable *>(peer)->sourcePanel->GetFsPath().IsEmpty());
        source->selectRow(0);
        source->setCurrentCell(0, 0);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        bool dialogSeen = false;
        bool inspected = false;
        QString initial;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            dialog->resize(size);
            dialog->layout()->activate();
            auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
            auto *scroll = dialog->findChild<QScrollArea *>("dialogContentScroll");
            const QPoint center =
                destination->mapTo(scroll->widget(), destination->rect().center());
            scroll->ensureVisible(center.x(), center.y(), (destination->width() + 2) / 2,
                                  (destination->height() + 2) / 2);
            initial = dialog->findChild<QLineEdit *>("copyDestination")->text();
            inspected =
                UiInspection::fullyVisible(*dialog->findChild<QLineEdit *>("copyDestination"),
                                           *dialog) &&
                UiInspection::writeReport(*dialog, QString("source-copy-default-%1-%2-%3-%4")
                                                       .arg(language)
                                                       .arg(size.width())
                                                       .arg(move)
                                                       .arg(root));
            dialog->reject();
        });
        QTest::keyClick(source, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(dialogSeen);
        QVERIFY(inspected);
        QCOMPARE(QDir::cleanPath(initial), fixture.path());
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("control bytes"));
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Files), QStringList{name});
        QCOMPARE(backend.listCalls, 0);
    }

    void shiftF5CopiesFocusedFileWithNewName() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("исходный файл.txt"));
        const QString target = fixture.filePath(QStringLiteral("новое имя.txt"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("source bytes"), qint64(12));
        input.close();

        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        window.show();
        QCoreApplication::processEvents();
        auto *view = table(window);
        QTRY_VERIFY(view->rowCount() > 0);
        const int row = filesystemRow(window, QStringLiteral("исходный файл.txt"));
        QVERIFY(row >= 0);
        view->selectRow(row);
        view->setCurrentCell(row, 0);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        bool dialogSeen = false;
        QString initial;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            auto *name = dialog->findChild<QLineEdit *>("copyDestinationName");
            QVERIFY(name && name->isVisible());
            initial = name->text();
            name->setText(QStringLiteral("новое имя.txt"));
            dialog->accept();
        });
        QTest::keyClick(view, Qt::Key_F5, Qt::ShiftModifier);
        QVERIFY(dialogSeen);
        QCOMPARE(initial.normalized(QString::NormalizationForm_C),
                 QStringLiteral("исходный файл.txt"));
        QTRY_VERIFY(QFileInfo::exists(target));
        QFile copied(target);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("source bytes"));
        QVERIFY(QFileInfo::exists(source));
    }

    void shiftF6MovesFocusedFileToNewName() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("первое имя.txt"));
        const QString target = fixture.filePath(QStringLiteral("второе имя.txt"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("move bytes"), qint64(10));
        input.close();

        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        window.show();
        QCoreApplication::processEvents();
        auto *view = table(window);
        QTRY_VERIFY(view->rowCount() > 0);
        const int row = filesystemRow(window, QStringLiteral("первое имя.txt"));
        QVERIFY(row >= 0);
        view->selectRow(row);
        view->setCurrentCell(row, 0);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        bool dialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            auto *name = dialog->findChild<QLineEdit *>("copyDestinationName");
            QVERIFY(name && name->isVisible());
            name->setText(QStringLiteral("второе имя.txt"));
            dialog->accept();
        });
        QTest::keyClick(view, Qt::Key_F6, Qt::ShiftModifier);
        QVERIFY(dialogSeen);
        QTRY_VERIFY(QFileInfo::exists(target));
        QTRY_VERIFY(!QFileInfo::exists(source));
        QFile moved(target);
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("move bytes"));
    }

    void newNameDoesNotBecomeDirectoryContainer_data() {
        QTest::addColumn<bool>("move");
        QTest::newRow("copy") << false;
        QTest::newRow("move") << true;
    }
    void newNameDoesNotBecomeDirectoryContainer() {
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString target = fixture.filePath("occupied");
        QVERIFY(QDir().mkdir(target));
        QFile original(source);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("source bytes");
        original.close();
        QFile control(QDir(target).filePath("keep.txt"));
        QVERIFY(control.open(QIODevice::WriteOnly));
        control.write("keep bytes");
        control.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = table(window);
        QTRY_VERIFY(filesystemRow(window, "source.txt") >= 0);
        const int row = filesystemRow(window, "source.txt");
        view->selectRow(row);
        view->setCurrentCell(row, 0);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        bool submitted = false;
        QString failure;
        QTimer driver;
        dialogs_.connectResponder(driver, &window, [&] {
            if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                dialog && dialogs_.ready(*dialog) && !submitted) {
                auto *name = dialog->findChild<QLineEdit *>("copyDestinationName");
                QVERIFY(name && name->isVisible());
                name->setText("occupied");
                submitted = true;
                dialog->accept();
                return;
            }
            if (auto *message = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                failure = message->text();
                message->accept();
                return;
            }
        });
        driver.start(2);
        QTest::keyClick(view, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(submitted);
        QTRY_VERIFY(!failure.isEmpty());
        driver.stop();
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("source bytes"));
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("keep bytes"));
        QVERIFY(!QFileInfo::exists(QDir(target).filePath("source.txt")));
    }

    void ordinaryF5AcceptsSingleFilenameDestination() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("source file.txt"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("ordinary bytes"), qint64(14));
        input.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        window.show();
        QCoreApplication::processEvents();
        auto *view = table(window);
        const int row = filesystemRow(window, QStringLiteral("source file.txt"));
        QVERIFY(row >= 0);
        view->selectRow(row);
        view->setCurrentCell(row, 0);
        view->setFocus();
        const QString target = fixture.filePath(QStringLiteral("nested/new file.txt"));
        bool dialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            dialog->findChild<QLineEdit *>("copyDestination")->setText(target);
            dialog->accept();
        });
        QTest::keyClick(view, Qt::Key_F5);
        QVERIFY(dialogSeen);
        QTRY_VERIFY(QFileInfo::exists(target));
        QFile copied(target);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("ordinary bytes"));
        QVERIFY(QFileInfo::exists(source));
    }

    void shiftF5CopiesFocusedArchiveItemToAbsoluteFolder() {
        QString errors;
        QTimer errorResponder;
        connect(&errorResponder, &QTimer::timeout, this, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errors += box->text() + '\n';
                    box->accept();
                }
        });
        errorResponder.start(10);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath(QStringLiteral("first file.txt"));
        const QString second = fixture.filePath(QStringLiteral("second file.txt"));
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QFileInfo(path).fileName().toUtf8()),
                     qint64(QFileInfo(path).fileName().toUtf8().size()));
        }
        const QString archive = fixture.filePath(QStringLiteral("archive.zip"));
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {first, second}));
        MainWindow window(&backend);
        window.openArchive(archive);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 2);
        const int focused = filesystemRow(window, QStringLiteral("first file.txt"));
        const int selected = filesystemRow(window, QStringLiteral("second file.txt"));
        QVERIFY(focused >= 0);
        QVERIFY(selected >= 0);
        view->selectRow(focused);
        view->setCurrentCell(focused, 0);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        bool defaultSeen = false;
        QString ordinaryDefault;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            defaultSeen = true;
            ordinaryDefault = dialog->findChild<QLineEdit *>("copyDestination")->text();
            dialog->reject();
        });
        QTest::keyClick(view, Qt::Key_F5);
        QVERIFY(defaultSeen);
        QCOMPARE(QDir::fromNativeSeparators(ordinaryDefault), fixture.path() + '/');
        QCOMPARE(view->rowCount(), 2);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        view->selectRow(selected);
        view->setCurrentCell(focused, 0, QItemSelectionModel::NoUpdate);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        const QString destination = fixture.filePath(QStringLiteral("папка назначения"));
        bool dialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
            dialog->accept();
        });
        QTest::keyClick(view, Qt::Key_F5, Qt::ShiftModifier);
        QVERIFY(dialogSeen);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QTRY_VERIFY(
            QFileInfo::exists(QDir(destination).filePath(QStringLiteral("first file.txt"))));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("second file.txt"))));

        const QString movedDestination = fixture.filePath(QStringLiteral("moved folder"));
        view->setCurrentCell(selected, 0);
        view->setFocus();
        dialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog)
                return;
            dialogSeen = true;
            dialog->findChild<QLineEdit *>("copyDestination")->setText(movedDestination);
            dialog->accept();
        });
        QTest::keyClick(view, Qt::Key_F6, Qt::ShiftModifier);
        QVERIFY(dialogSeen);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QTRY_VERIFY(
            QFileInfo::exists(QDir(movedDestination).filePath(QStringLiteral("second file.txt"))));
        QTRY_COMPARE(view->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("first file.txt"));
    }
};

QTEST_MAIN(UiCopySameTests)
#include "ui_copy_same_tests.moc"
