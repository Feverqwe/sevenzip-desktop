#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/read_only_virtual_disk.h"
#include "ui/support/layout_inspection.h"
#include <QLineEdit>

class FsDrivesDeviceUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int deviceRow(MainWindow &window, const QString &name) {
        auto *view = table(window);
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 3).toString() == name)
                return row;
        return -1;
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

  private slots:
    void logicalDrivesAllowHashWithoutCopy() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "openRootAction")->trigger();
        auto *view = table(window);
        QVERIFY(static_cast<FileTable *>(view)->sourcePanel->IsFSDrivesFolder());
        QTRY_VERIFY(view->rowCount() > 0);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        QTRY_VERIFY(!action(window, "copyAction")->isEnabled());
        QVERIFY(action(window, "hashCRC32Action")->isEnabled());
        QCOMPARE(backend.listCalls, 0);
    }
    void sourceDeviceNamespaceAndF5_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("cancel");
        for (const char *language : {"-", "ru"})
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(cancel)))
                    << QString::fromLatin1(language) << cancel;
    }
    void sourceDeviceNamespaceAndF5() {
        QFETCH(QString, language);
        QFETCH(bool, cancel);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        ReadOnlyVirtualDisk disk(temporary.filePath(QStringLiteral("приватный диск 空.vhd")));
        QVERIFY2(disk.valid(), qPrintable(QString::number(disk.error())));
        const QByteArray source = disk.readDevice();
        QCOMPARE(quint64(source.size()), ReadOnlyVirtualDisk::Size);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "openRootAction")->trigger();
        action(window, "parentFolderAction")->trigger();
        auto *view = table(window);
        int volumes = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 3)
                volumes = row;
        QVERIFY(volumes >= 0);
        view->setCurrentCell(volumes, 0);
        view->selectRow(volumes);
        action(window, "openItemAction")->trigger();
        QTRY_VERIFY(deviceRow(window, disk.device().mid(4)) >= 0);
        const int row = deviceRow(window, disk.device().mid(4));
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        QCOMPARE(view->model()->index(row, 0).data(Qt::UserRole + 1).toBool(), false);
        QTRY_VERIFY(action(window, "copyAction")->isEnabled());
        for (const char *command : {"moveAction", "deleteAction", "renameAction",
                                    "createFolderAction", "createFileAction"})
            QVERIFY(!action(window, command)->isEnabled());
        QVERIFY(action(window, "hashCRC32Action")->isEnabled());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(
                UiInspection::fullyVisible(*window.findChild<QLineEdit *>("archivePath"), window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("device-namespace-%1-%2-%3").arg(language).arg(cancel).arg(size.width())));
        }
        CopyThroughKeyboard(window, temporary.filePath(QStringLiteral("образ 空 с пробелом.img")),
                            language, cancel);
        const QString target = temporary.filePath(QStringLiteral("образ 空 с пробелом.img"));
        if (cancel)
            QVERIFY(!QFileInfo::exists(target));
        else
            QCOMPARE(read(target), source);
        QCOMPARE(disk.readDevice(), source);
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(disk.detach(), DWORD(ERROR_SUCCESS));
    }

  private:
    void CopyThroughKeyboard(MainWindow &window, const QString &target, const QString &language,
                             bool cancel) {
        bool seen = false, geometry = true;
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
                return;
            }
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (seen || !dialog || !dialog->isVisible())
                return;
            seen = true;
            auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
            destination->setText(target);
            for (const QSize size : {QSize(640, 400), QSize(480, 320)}) {
                dialog->resize(size);
                QApplication::processEvents();
                geometry &= UiInspection::fullyVisible(*destination, *dialog);
                geometry &= UiInspection::writeReport(
                    *dialog,
                    QString("device-copy-%1-%2-%3").arg(language).arg(cancel).arg(size.width()));
            }
            cancel ? dialog->reject() : dialog->accept();
        });
        viewFocus(window);
        responder.start(10);
        QTest::keyClick(table(window), Qt::Key_F5);
        responder.stop();
        QVERIFY(seen);
        QVERIFY(geometry);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    static void viewFocus(MainWindow &window) {
        table(window)->setFocus();
        QTRY_VERIFY(table(window)->hasFocus());
    }
};

QTEST_MAIN(FsDrivesDeviceUiTests)
#include "fs_drives_device_ui_tests.moc"
