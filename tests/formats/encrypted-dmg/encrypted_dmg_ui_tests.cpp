#include "archive/backend/archivebackend.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include "support/codec_runtime.h"
#include "support/widget_dialogs.h"
#include <QAction>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <future>

namespace {
int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
} // namespace

class EncryptedDmgUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString engine_, module_;
    QByteArray previousSettings_;
  private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", runtime_.filePath("settings.ini").toUtf8());
        const QDir prepared(QString::fromUtf8(SEVENZIP_DMG_RUNTIME));
#ifdef Q_OS_WIN
        const QString engineName = "7z.exe", coreName = "7z.dll", suffix = ".dll";
#else
        const QString engineName = "7z", coreName = "7z.so", suffix = ".so";
#endif
        engine_ = runtime_.filePath(engineName);
        module_ = runtime_.filePath(coreName);
        QVERIFY(CopyCodecRuntime(prepared, QDir(runtime_.path())));
        QVERIFY(QDir(runtime_.path()).mkdir("Formats"));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_MODULE),
                            runtime_.filePath("Formats/EncryptedDmg" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_HFS_MODULE),
                            runtime_.filePath("Formats/Hfs" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_EXFAT_MODULE),
                            runtime_.filePath("Formats/Exfat" + suffix)));
    }
    void cleanupTestCase() { qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_); }
    void nativeMountFromWindow_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("filesystem");
        QTest::addColumn<bool>("previews");
        QTest::newRow("encrypted read-only") << QString("UDRO") << QString("APFS") << false;
        QTest::newRow("encrypted compressed") << QString("UDZO") << QString("APFS") << false;
        QTest::newRow("encrypted exfat read-only") << QString("UDRO") << QString("ExFAT") << false;
        QTest::newRow("encrypted exfat compressed") << QString("UDZO") << QString("ExFAT") << false;
        QTest::newRow("encrypted exfat previews") << QString("UDRO") << QString("ExFAT") << true;
    }
    void nativeMountFromWindow() {
#ifndef Q_OS_MACOS
        QSKIP("Native macOS mount integration.");
#else
        if (!qEnvironmentVariableIsSet("SEVENZIP_NATIVE_SMB_TEST"))
            QSKIP("Opt-in native mount with an isolated synthetic image.");
        QFETCH(QString, format);
        QFETCH(QString, filesystem);
        QFETCH(bool, previews);
        const QString vlc = previews ? qEnvironmentVariable("SEVENZIP_TEST_VLC") : QString();
        QTemporaryDir inputs;
        QVERIFY(QDir(inputs.path()).mkdir("files"));
        const QByteArray payload = QByteArray("0123456789abcdef").repeated(262145);
        QFile input(inputs.filePath("files/large.bin"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), payload.size());
        input.close();
        QStringList pictures;
        if (previews) {
            quint32 random = 0x12345678;
            for (int n = 0; n < 12; ++n) {
                QImage picture(1024, 1024, QImage::Format_RGB32);
                for (int y = 0; y < picture.height(); ++y) {
                    auto *line = reinterpret_cast<quint32 *>(picture.scanLine(y));
                    for (int x = 0; x < picture.width(); ++x) {
                        random = random * 1664525 + 1013904223;
                        line[x] = 0xff000000 | (random & 0xffffff);
                    }
                }
                const auto name = QString("photo%1.jpg").arg(n, 2, 10, QChar('0'));
                QVERIFY(picture.save(inputs.filePath("files/" + name), "JPEG", 92));
                pictures.append(name);
            }
        }
        if (!vlc.isEmpty()) {
            const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
            QVERIFY(!ffmpeg.isEmpty());
            QProcess video;
            video.start(ffmpeg, {"-f", "lavfi", "-i", "testsrc2=size=640x352:rate=24", "-t", "6",
                                 "-an", "-c:v", "libx264", "-preset", "ultrafast", "-y",
                                 inputs.filePath("files/video.mp4")});
            QVERIFY(video.waitForFinished(30000));
            QCOMPARE(video.exitCode(), 0);
            QVERIFY(QDir(inputs.path()).mkdir("frames"));
        }
        const auto path = inputs.filePath("readonly.dmg");
        QProcess producer;
        QStringList arguments{"create",     "-srcfolder",  inputs.filePath("files"),
                              "-format",    format,        "-fs",
                              filesystem,   "-encryption", "AES-256",
                              "-stdinpass", path};
        if (filesystem == "ExFAT")
            arguments << "-size" << "64m";
        producer.start("/usr/bin/hdiutil", arguments);
        QVERIFY(producer.waitForStarted());
        producer.write(QByteArray("fixture-password\0", 17));
        producer.closeWriteChannel();
        QVERIFY(producer.waitForFinished(30000));
        QCOMPARE(producer.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        MainWindow window(&backend);
        window.show();
        const auto cleanup = qScopeGuard([&] {
            for (auto *session : window.findChildren<QtArchiveMountSession *>()) {
                session->unmount(true);
                for (int n = 0;
                     n < 300 && session->status() != QtArchiveMountSession::Status::Stopped; ++n)
                    QTest::qWait(10);
            }
        });
        int extractions = 0;
        bool cacheVisible = false;
        QString error;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            auto *active = dialogs.activeDialog();
            if (auto *password = dynamic_cast<CPasswordDialog *>(active)) {
                password->findChild<QLineEdit *>("passwordEdit")->setText("fixture-password");
                password->accept();
            } else if (auto *prompt = dynamic_cast<QtMountExtractionPrompt *>(active)) {
                ++extractions;
                prompt->reject();
            } else if (auto *message = qobject_cast<QMessageBox *>(active)) {
                error += message->text();
                message->accept();
            } else if (active && active->objectName() == "archiveMountSettings") {
                auto *box = active->findChild<QDialogButtonBox *>();
                for (auto *button : box->buttons())
                    if (box->buttonRole(button) == QDialogButtonBox::AcceptRole &&
                        button->isEnabled()) {
                        cacheVisible =
                            active->findChild<QSpinBox *>("mountCacheLimit")->isVisible();
                        active->accept();
                        break;
                    }
            }
        });
        automation.start(5);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            error += "Dialog did not complete within 20 seconds.";
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        watchdog.start(20000);
        window.openArchive(path);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QTRY_VERIFY(Row(*table, "large.bin") >= 0);
        watchdog.start(20000);
        window.findChild<QAction *>("mountReadOnlyAction")->trigger();
        watchdog.stop();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto sessions = window.findChildren<QtArchiveMountSession *>();
        QCOMPARE(sessions.size(), 1);
        auto *session = sessions.first();
        QTRY_COMPARE_WITH_TIMEOUT(session->status(), QtArchiveMountSession::Status::Mounted, 15000);
        QProcess thumbnails;
        if (previews) {
            QVERIFY(QDir(inputs.path()).mkdir("previews"));
            QStringList args{"-t", "-s", "256", "-o", inputs.filePath("previews")};
            for (const auto &name : pictures)
                args.append(QDir(session->path()).filePath(name));
            thumbnails.start("/usr/bin/qlmanage", args);
            QVERIFY(thumbnails.waitForStarted());
        }
        QProcess player;
        if (!vlc.isEmpty()) {
            player.start(vlc, {"--intf=dummy", "--ignore-config", "--no-media-library",
                               "--no-metadata-network-access", "--vout=dummy", "--aout=dummy",
                               "--play-and-exit", "--run-time=3", "--video-filter=scene",
                               "--scene-ratio=24", "--scene-prefix=frame", "--scene-format=png",
                               "--scene-path=" + inputs.filePath("frames"),
                               "--config=" + inputs.filePath("vlc.ini"),
                               QDir(session->path()).filePath("video.mp4")});
            QVERIFY(player.waitForStarted());
        }
        auto read = std::async(std::launch::async, [mount = session->path()] {
            QFile file(QDir(mount).filePath("large.bin"));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        });
        QElapsedTimer deadline;
        deadline.start();
        bool timedOut = false;
        while (read.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            QTest::qWait(1);
            if (!timedOut && deadline.elapsed() > 15000) {
                timedOut = true;
                session->unmount(true);
            }
        }
        QCOMPARE(read.get(), payload);
        QVERIFY(!timedOut);
        if (previews) {
            while ((thumbnails.state() != QProcess::NotRunning ||
                    player.state() != QProcess::NotRunning) &&
                   deadline.elapsed() < 30000)
                QTest::qWait(5);
            if (thumbnails.state() != QProcess::NotRunning) {
                thumbnails.kill();
                thumbnails.waitForFinished();
            }
            QVERIFY2(thumbnails.exitCode() == 0,
                     qPrintable(QString::fromUtf8(thumbnails.readAllStandardError())));
            QCOMPARE(QDir(inputs.filePath("previews")).entryList({"*.png"}, QDir::Files).size(),
                     pictures.size());
            if (!vlc.isEmpty()) {
                if (player.state() != QProcess::NotRunning) {
                    player.kill();
                    player.waitForFinished();
                }
                QVERIFY2(player.exitCode() == 0,
                         qPrintable(QString::fromUtf8(player.readAllStandardError())));
                const QDir frames(inputs.filePath("frames"));
                const auto files = frames.entryList({"*.png"}, QDir::Files);
                QVERIFY(files.size() >= 2);
                const QImage first(frames.filePath(files.first()));
                const QImage last(frames.filePath(files.last()));
                QCOMPARE(first.size(), QSize(640, 352));
                QVERIFY(first != last);
            }
        }
        QCOMPARE(extractions, 0);
        QVERIFY(!cacheVisible);
#endif
    }
    void opening_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("wrong-then-correct-and-copy") << false;
        QTest::newRow("cancel") << true;
    }
    void opening() {
        QFETCH(bool, cancel);
        qputenv("SEVENZIP_SETTINGS_PATH",
                runtime_.filePath(cancel ? "cancel.ini" : "copy.ini").toUtf8());
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        MainWindow window(&backend);
        const QString source = QString::fromUtf8(SEVENZIP_DMG_FIXTURES) + "/native-hfs256.dmg";
        window.openPath(QFileInfo(source).absolutePath());
        window.show();
        int prompts = 0;
        QString error;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            auto *active = dialogs.activeDialog();
            if (auto *message = qobject_cast<QMessageBox *>(active)) {
                error += message->text();
                message->accept();
            }
            if (auto *password = dynamic_cast<CPasswordDialog *>(active)) {
                ++prompts;
                if (cancel || prompts > 2) {
                    password->reject();
                    return;
                }
                password->findChild<QLineEdit *>("passwordEdit")
                    ->setText(prompts == 1 ? "wrong" : "fixture-password");
                password->accept();
            }
        });
        automation.start(10);
        window.openArchive(source);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        if (cancel) {
            QCOMPARE(prompts, 1);
            QVERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
            return;
        }
        QCOMPARE(prompts, 2);
        const int root = Row(*table, "files");
        QVERIFY(root >= 0);
        QVERIFY(QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, table->model()->index(root, 0))));
        const auto name = QString::fromUtf8("hello 空.txt");
        QTRY_VERIFY(Row(*table, name) >= 0);
        table->selectRow(Row(*table, name));
        QTemporaryDir output;
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        window.extractTo(output.path(), true, {}, options);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(prompts, 2);
        QDirIterator files(output.path(), QDir::Files, QDirIterator::Subdirectories);
        QByteArray copied;
        while (files.hasNext()) {
            const auto path = files.next();
            if (QFileInfo(path).fileName() == name) {
                QFile file(path);
                QVERIFY(file.open(QIODevice::ReadOnly));
                copied = file.readAll();
            }
        }
        QCOMPARE(copied, QByteArray("Encrypted DMG fixture\n"));
        automation.stop();
    }
};
QTEST_MAIN(EncryptedDmgUiTests)
#include "encrypted_dmg_ui_tests.moc"
