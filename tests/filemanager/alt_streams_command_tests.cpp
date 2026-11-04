#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"

class AltStreamsCommandTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
#ifdef Q_OS_WIN
    void sourceDeleteAndProperties_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void sourceDeleteAndProperties() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        const QString name = QStringLiteral("основной файл δ.bin");
        const QString streamName = QStringLiteral("поток δ with space");
        const QString basePath = temp.filePath(name);
        for (const QString &path : {basePath, basePath + ':' + streamName}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("fixture bytes"), qint64(13));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        table(window)->selectRow(filesystemRow(window, name));
        action(window, "altStreamsAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QVERIFY(table(window)->currentIndex().isValid());
        table(window)->selectRow(0);
        table(window)->setCurrentCell(0, 0);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*table(window), window));
            QVERIFY(UiInspection::fullyVisible(*address, window));
            QVERIFY(UiInspection::writeReport(window, QStringLiteral("alt-streams-%1-%2x%3")
                                                          .arg(language)
                                                          .arg(size.width())
                                                          .arg(size.height())));
        }
        bool propertiesSeen = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            if (!dialog || !dialog->isVisible())
                return;
            propertiesSeen = true;
            dialog->reject();
        });
        responder.start(1);
        QVERIFY(action(window, "selectedPropertiesAction")->isEnabled());
        action(window, "selectedPropertiesAction")->trigger();
        responder.stop();
        // Upstream AltStreamsFolder has no IGetFolderArcProps and is excluded
        // by InvokeSystemCommand's filesystem/drives guard. Do not invent a
        // portable property dialog for that original no-op branch.
        QVERIFY(!propertiesSeen);
        QCOMPARE(table(window)->rowCount(), 1);
        auto *remove = action(window, "deleteAction");
        QVERIFY(remove->isEnabled());
        for (QMessageBox::StandardButton answer : {QMessageBox::Cancel, QMessageBox::Yes}) {
            bool asked = false;
            QTimer confirmation;
            connect(&confirmation, &QTimer::timeout, &window, [&] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                asked = true;
                box->button(answer)->click();
            });
            confirmation.start(1);
            remove->trigger();
            confirmation.stop();
            QVERIFY(asked);
            QCOMPARE(table(window)->rowCount(), answer == QMessageBox::Cancel ? 1 : 0);
            QFile base(basePath);
            QVERIFY(base.open(QIODevice::ReadOnly));
            QCOMPARE(base.readAll(), QByteArray("fixture bytes"));
        }
        QVERIFY(!QFileInfo::exists(basePath + ':' + streamName));
    }
    void sourceCreateAndRename() {
        QTemporaryDir temp;
        const QString basePath = temp.filePath(QStringLiteral("файл δ.bin"));
        QFile base(basePath);
        QVERIFY(base.open(QIODevice::WriteOnly));
        QCOMPARE(base.write("base bytes"), qint64(10));
        base.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, QFileInfo(basePath).fileName()) >= 0);
        table(window)->selectRow(filesystemRow(window, QFileInfo(basePath).fileName()));
        action(window, "altStreamsAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 0);
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
        auto *create = action(window, "createFileAction");
        QVERIFY(create->isEnabled());
        const QString name = QStringLiteral("новый поток δ");
        for (bool accepted : {false, true}) {
            bool seen = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<QDialog *>("comboDialog");
                QVERIFY(dialog);
                seen = true;
                dialog->findChild<QComboBox *>("comboValue")->setEditText(name);
                if (accepted)
                    dialog->accept();
                else
                    dialog->reject();
            });
            create->trigger();
            QVERIFY(seen);
            QCOMPARE(table(window)->rowCount(), accepted ? 1 : 0);
        }
        QFile stream(basePath + ':' + name);
        QVERIFY(stream.open(QIODevice::WriteOnly));
        QCOMPARE(stream.write("stream bytes"), qint64(12));
        stream.close();
        table(window)->selectRow(0);
        table(window)->setCurrentCell(0, 0);
        auto *rename = action(window, "renameAction");
        QVERIFY(rename->isEnabled());
        rename->trigger();
        auto *editor = table(window)->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->text(), name);
        const QString renamed = QStringLiteral("переименованный δ");
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(table(window)->model()->index(0, 0).data().toString(), renamed);
        QTRY_VERIFY(QFileInfo::exists(basePath + ':' + renamed));
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QVERIFY(!QFileInfo::exists(stream.fileName()));
        QFile copied(basePath + ':' + renamed);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("stream bytes"));
        QVERIFY(base.open(QIODevice::ReadOnly));
        QCOMPARE(base.readAll(), QByteArray("base bytes"));
    }
#endif
    void filesystemCapabilityAndSourceNavigation() {
        QTemporaryDir temp;
        const QString name = QStringLiteral("файл δ with space.bin");
        QFile base(temp.filePath(name));
        QVERIFY(base.open(QIODevice::WriteOnly));
        QCOMPARE(base.write("main bytes"), qint64(10));
        base.close();
#ifdef Q_OS_WIN
        QFile stream(base.fileName() + QStringLiteral(":поток δ"));
        QVERIFY(stream.open(QIODevice::WriteOnly));
        QCOMPARE(stream.write("stream bytes"), qint64(12));
        stream.close();
#endif
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        table(window)->selectRow(filesystemRow(window, name));
        auto *streams = action(window, "altStreamsAction");
#ifdef Q_OS_WIN
        QTRY_VERIFY(streams->isEnabled());
        try {
            streams->trigger();
        } catch (int code) {
            QFAIL(qPrintable(QStringLiteral("OpenAltStreams source exception %1").arg(code)));
        }
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QVERIFY(table(window)->currentIndex().isValid());
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral("поток δ"));
        QCOMPARE(table(window)->model()->index(0, 0).data(Qt::UserRole + 1).toBool(), false);
        QCOMPARE(table(window)->model()->index(0, 0).data(Qt::UserRole).toString(),
                 address->text() + QStringLiteral("поток δ"));
        QVERIFY(address->text().endsWith(name + ':'));
        QVERIFY(!streams->isEnabled());
        auto *view = table(window);
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        const QString target = temp.filePath(QStringLiteral("copied stream δ.bin"));
        const auto transfer = [&](const char *command, const QString &destination) {
            auto *copy = action(window, command);
            QVERIFY(copy->isEnabled());
            bool dialogSeen = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                if (!dialog)
                    return;
                dialogSeen = true;
                dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(destination);
                dialog->accept();
            });
            try {
                copy->trigger();
            } catch (int code) {
                QFAIL(qPrintable(QStringLiteral("%1 source exception %2")
                                     .arg(QString::fromLatin1(command))
                                     .arg(code)));
            }
            QVERIFY(dialogSeen);
        };
        transfer("copyAction", target);
        QFile copied(target);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("stream bytes"));
        QCOMPARE(view->rowCount(), 1);
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        const QString movedPath = temp.filePath(QStringLiteral("moved stream δ.bin"));
        transfer("moveAction", movedPath);
        QFile moved(movedPath);
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("stream bytes"));
        QCOMPARE(view->rowCount(), 0);
        auto *parent = action(window, "parentFolderAction");
        QVERIFY(parent->isEnabled());
        parent->trigger();
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        QCOMPARE(QDir::fromNativeSeparators(address->text()), temp.path());
#else
        QTRY_VERIFY(!streams->isEnabled());
        QCOMPARE(QDir::fromNativeSeparators(address->text()), temp.path());
#endif
        QFile verify(base.fileName());
        QVERIFY(verify.open(QIODevice::ReadOnly));
        QCOMPARE(verify.readAll(), QByteArray("main bytes"));
    }
};

QTEST_MAIN(AltStreamsCommandTests)
#include "alt_streams_command_tests.moc"
