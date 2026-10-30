#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "settings/ViewSettings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>
#ifdef Q_OS_WIN
#include "platform/support/native_link_fixture.h"
#include <windows.h>

namespace {
struct NativeStartupWindow {
    DWORD PID;
    QString Title;
    bool Refusal;
    HWND Window = nullptr;
};
BOOL CALLBACK FindStartupWindow(HWND window, LPARAM parameter) {
    auto &request = *reinterpret_cast<NativeStartupWindow *>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != request.PID || !IsWindowVisible(window))
        return TRUE;
    wchar_t title[2048]{};
    GetWindowTextW(window, title, 2048);
    const QString text = QString::fromWCharArray(title);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const bool matches = request.Refusal ? text == request.Title && (style & WS_POPUP)
                                         : text.contains(request.Title);
    if (!matches)
        return TRUE;
    request.Window = window;
    return FALSE;
}
} // namespace
#endif

class FileManagerStartupTests final : public QObject {
    Q_OBJECT
    QString module, engine;
    quint32 version = 0;

  private slots:
    void initTestCase() {
        module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        ArchiveCodecCatalog probe(module, 0);
        QCOMPARE(probe.result(), S_OK);
        version = probe.version();
    }
    void ordinaryFolderHasArchiveCommandsWhenFirstShown() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("первый файл 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), "startup payload"));
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_PROBE_MEMBER", name);
        environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath("ordinary-startup.ini"));
        process.setProcessEnvironment(environment);
        process.start(QString::fromUtf8(SEVENZIP_FM_PROBE), {fixture.path()});
        QVERIFY(process.waitForStarted(5000));
        QVERIFY2(process.waitForFinished(35000), "ordinary FM entry did not finish");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QVERIFY(report.value("bound").toBool());
        QVERIFY(report.value("archiveCommandsReadyAtFirstShow").toBool());
        QVERIFY(!report.value("timeout").toBool());
    }
    void initialNativeBinding_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("tail");
        QTest::addColumn<bool>("refused");
        QTest::addColumn<bool>("typed");
        const auto rows = [](bool typed) {
            const auto row = [typed](const char *name, const QString &kind, const QString &tail,
                                     bool refused) {
                QTest::newRow(qPrintable(QString(typed ? "argv-%1" : "%1").arg(name)))
                    << kind << tail << refused << typed;
            };
            row("quoted-unicode-auto", "zip", {}, false);
            row("quoted-unicode-forced-zip", "zip", " -tzip", false);
            row("wrong-forced-type", "zip", " -t7z", true);
            row("broken-archive", "broken", " -tzip", true);
            row("encrypted-open-cancel", "encrypted", " -t7z", true);
            row("relative-folder", "folder", {}, false);
            row("source-ignored-tail", "zip", " -ignored -t7z", false);
        };
        rows(false);
#ifndef Q_OS_WIN
        rows(true);
#endif
    }
    void initialNativeBinding() {
        QFETCH(QString, kind);
        QFETCH(QString, tail);
        QFETCH(bool, refused);
        QFETCH(bool, typed);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString cwd = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(cwd); });
        QVERIFY(QDir::setCurrent(fixture.path()));
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const QString name = QStringLiteral("payload 密碼 with spaces.txt");
        const QByteArray bytes("native File Manager startup bytes");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), bytes));
        const bool encrypted = kind == "encrypted";
        QString path = fixture.filePath(encrypted ? QStringLiteral("архив 空 with spaces.7z")
                                                  : QStringLiteral("архив 空 with spaces.zip"));
        if (typed)
            path.insert(path.lastIndexOf('.'), QStringLiteral(" \"literal quote\""));
        if (kind == "folder") {
            path = typed ? QStringLiteral(" каталог 空 \"quoted\" with spaces ")
                         : QStringLiteral("каталог 空 with spaces");
            QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(path + "/" + name), bytes));
        } else if (kind == "broken") {
            QVERIFY(SourceExtractFixture::writeFile(path, "invalid ZIP fixture"));
        } else {
            QProcess create;
            QStringList arguments{"a", encrypted ? "-t7z" : "-tzip"};
            if (encrypted)
                arguments << "-pfixture" << "-mhe=on";
            arguments << path << name;
            create.start(engine, arguments);
            QVERIFY(create.waitForFinished(10000));
            QCOMPARE(create.exitCode(), 0);
        }
        const QByteArray original =
            kind == "folder" ? QByteArray() : SourceExtractFixture::readFile(path);
        const QString line = QStringLiteral("\"C:\\Sevenzip Desktop\\7zFM.exe\" \"") + path +
                             QStringLiteral("\"") + tail;
        const auto startup = typed ? WinMain2(ToUString(path), ToUString(tail.trimmed()))
                                   : WinMain2(ToUString(line));
        QCOMPARE(startup.NeedOpenArc, kind != "folder");
        QCOMPARE(QDir::fromNativeSeparators(ToQString(startup.FullPath)),
                 QFileInfo(path).absoluteFilePath());
        ArchiveBackend backend(nullptr, engine);
        QCOMPARE(backend.loadCodecCatalog(module, version), S_OK);
        int passwordRequests = 0;
        QTimer cancellation;
        connect(&cancellation, &QTimer::timeout, [&] {
            if (auto *password =
                    dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget())) {
                ++passwordRequests;
                password->reject();
            }
        });
        if (encrypted)
            cancellation.start(5);
        bool rejected = false;
        try {
            MainWindow window(&backend, {}, nullptr, &startup);
            window.show();
            auto *table = window.findChild<FileTable *>("archiveTable");
            QVERIFY(table);
            QTRY_VERIFY(table->rowCount() > 0);
            bool found = false;
            for (int row = 0; row < table->rowCount(); ++row)
                found |= table->model()->index(row, 0).data().toString() == name;
            QVERIFY(found);
        } catch (const QtFileManagerStartupFailure &failure) {
            rejected = true;
            QCOMPARE(failure.Message.IsEmpty(), encrypted);
        }
        cancellation.stop();
        QCOMPARE(rejected, refused);
        QCOMPARE(passwordRequests > 0, encrypted);
        QVERIFY(!backend.busy());
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(name)), bytes);
        if (kind != "folder")
            QCOMPARE(SourceExtractFixture::readFile(path), original);
#ifndef Q_OS_WIN
        if (typed) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("SEVENZIP_PROBE_MEMBER", name);
            environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath("probe-settings.ini"));
            process.setProcessEnvironment(environment);
            process.setWorkingDirectory(fixture.path());
            QStringList arguments{path};
            arguments.append(tail.split(' ', Qt::SkipEmptyParts));
            process.start(QString::fromUtf8(SEVENZIP_FM_PROBE), arguments);
            QVERIFY(process.waitForStarted(5000));
            QVERIFY2(process.waitForFinished(35000), "production FM entry did not finish");
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(process.exitCode(), 0);
            const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
            QVERIFY(!report.isEmpty());
            QCOMPARE(report.value("bound").toBool(), !refused);
            QCOMPARE(report.value("refusal").toBool(), refused && !encrypted);
            QCOMPARE(report.value("password").toBool(), encrypted);
            QVERIFY(!report.value("timeout").toBool());
            QCOMPARE(report.value("exitCode").toInt(), 0);
            QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(name)), bytes);
            if (kind != "folder")
                QCOMPARE(SourceExtractFixture::readFile(path), original);
        }
#endif
    }
#ifdef Q_OS_WIN
    void nativeStartupEnablesLinkCopy_data() {
        QTest::addColumn<QString>("platform");
        QTest::newRow("offscreen") << QString("offscreen");
        QTest::newRow("windows-ole") << QString("windows");
    }
    void nativeStartupEnablesLinkCopy() {
        QFETCH(QString, platform);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath(QStringLiteral("цель 空 with spaces.txt"));
        const QString link = fixture.filePath(QStringLiteral("ссылка 空 with spaces"));
        const QString destination = fixture.filePath(QStringLiteral("копия 空 with spaces"));
        const QByteArray bytes("startup symbolic link protected target");
        QVERIFY(SourceExtractFixture::writeFile(target, bytes));
        QVERIFY(CreateNativeSymbolicLink(link, QFileInfo(target).fileName()));
        CByteBuffer original;
        QVERIFY(NWindows::NFile::NIO::GetReparseData(NativeFixturePath(link), original));
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", platform);
        environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath("link startup.ini"));
        environment.insert("SEVENZIP_PROBE_MEMBER", QFileInfo(link).fileName());
        environment.insert("SEVENZIP_PROBE_COPY_LINK", link);
        environment.insert("SEVENZIP_PROBE_LINK_DESTINATION", destination);
        process.setProcessEnvironment(environment);
        process.start(QString::fromUtf8(SEVENZIP_FM_PROBE), {fixture.path()});
        QVERIFY(process.waitForStarted(5000));
        QVERIFY2(process.waitForFinished(35000), "startup link probe did not finish");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QVERIFY(report.value("bound").toBool());
        QVERIFY(!report.value("timeout").toBool());
        QVERIFY(report.value("privilegeDropped").toBool());
        QVERIFY(report.value("linkCopied").toBool());
        if (platform == "windows")
            QVERIFY(report.value("oleInitialized").toBool());
        CByteBuffer actual, source;
        QVERIFY(NWindows::NFile::NIO::GetReparseData(NativeFixturePath(destination), actual));
        QVERIFY(NWindows::NFile::NIO::GetReparseData(NativeFixturePath(link), source));
        const auto blob = [](const CByteBuffer &data) {
            return QByteArray(reinterpret_cast<const char *>(static_cast<const Byte *>(data)),
                              qsizetype(data.Size()));
        };
        QCOMPARE(blob(actual), blob(original));
        QCOMPARE(blob(source), blob(original));
        QCOMPARE(SourceExtractFixture::readFile(target), bytes);
        QCOMPARE(SourceExtractFixture::readFile(destination), bytes);
    }

    void nativeLaunchShowCommand_data() {
        QTest::addColumn<int>("command");
        QTest::addColumn<bool>("savedMaximized");
        QTest::addColumn<bool>("maximized");
        QTest::addColumn<bool>("minimized");
        QTest::addColumn<bool>("visible");
        QTest::addColumn<bool>("withoutActivation");
        QTest::newRow("hidden") << SW_HIDE << true << false << false << false << false;
        QTest::newRow("normal-saved-maximized")
            << SW_SHOWNORMAL << true << true << false << true << false;
        QTest::newRow("explicit-maximized")
            << SW_SHOWMAXIMIZED << false << true << false << true << false;
        QTest::newRow("explicit-minimized")
            << SW_SHOWMINIMIZED << true << false << true << true << false;
        QTest::newRow("no-activate") << SW_SHOWNOACTIVATE << true << false << false << true << true;
        QTest::newRow("show-no-activate") << SW_SHOWNA << true << false << false << true << true;
        QTest::newRow("restore") << SW_RESTORE << true << false << false << true << false;
    }

    void nativeLaunchShowCommand() {
        QFETCH(int, command);
        QFETCH(bool, savedMaximized);
        QFETCH(bool, maximized);
        QFETCH(bool, minimized);
        QFETCH(bool, visible);
        QFETCH(bool, withoutActivation);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("native startup show command fixture");
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const QByteArray original = SourceExtractFixture::readFile(archive);
        const QString settings = fixture.filePath(QStringLiteral("startup 空 settings.ini"));
        CWindowInfo info{{42, 54, 1022, 674}, savedMaximized, 1, 0, 370};
        info.Save(settings);
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_SETTINGS_PATH", settings);
        environment.insert("SEVENZIP_PROBE_MEMBER", QFileInfo(SourceExtractFixture::kName).path());
        environment.insert("SEVENZIP_PROBE_WINDOW_STATE", "1");
        // Inspect Qt state through the actual Win32 launch transport. Native
        // visibility/exposure remains covered independently below.
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(fixture.path());
        process.setCreateProcessArgumentsModifier([command](QProcess::CreateProcessArguments *a) {
            a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            a->startupInfo->wShowWindow = WORD(command);
        });
        process.start(QString::fromUtf8(SEVENZIP_FM_PROBE), {archive, "-tzip"});
        QVERIFY(process.waitForStarted(5000));
        QVERIFY2(process.waitForFinished(30000), "startup state probe did not finish");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QVERIFY(report.value("bound").toBool());
        QVERIFY(!report.value("timeout").toBool());
        const auto state = report.value("windowState").toObject();
        QVERIFY(!state.isEmpty());
        QCOMPARE(state.value("visible").toBool(), visible);
        QCOMPARE(state.value("maximized").toBool(), maximized);
        QCOMPARE(state.value("minimized").toBool(), minimized);
        QCOMPARE(state.value("withoutActivation").toBool(), withoutActivation);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
    }

    void actualFileManagerProcess_data() {
        QTest::addColumn<QString>("type");
        QTest::addColumn<bool>("refused");
        QTest::newRow("actual-7zFM-forced-zip") << QString("zip") << false;
        QTest::newRow("actual-7zFM-wrong-type") << QString("7z") << true;
    }
    void actualFileManagerProcess() {
        QFETCH(QString, type);
        QFETCH(bool, refused);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("actual native 7zFM process startup");
        const QString original = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!original.isEmpty());
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        QVERIFY(QFile::rename(original, archive));
        const QByteArray archiveBytes = SourceExtractFixture::readFile(archive);
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_SETTINGS_PATH", fixture.filePath("fm-settings.ini"));
        environment.remove("QT_QPA_PLATFORM");
        const QString installed = qEnvironmentVariable("NATIVE_SHELL_TEST_INSTALL_DIR");
        QString executable = QString(SEVENZIP_NATIVE_FM);
        if (!installed.isEmpty()) {
            const QString system = environment.value("SystemRoot");
            QVERIFY(!system.isEmpty());
            const QString binary = QDir::toNativeSeparators(QDir(installed).filePath("bin"));
            executable = binary + "\\7zFM.exe";
            environment.insert("PATH", binary + ";" + system + "\\System32;" + system);
            environment.insert("SEVENZIP_CODEC_MODULE_PATH", binary + "\\7z.dll");
            environment.insert("QT_PLUGIN_PATH", QString());
            environment.insert("QT_QPA_PLATFORM_PLUGIN_PATH", QString());
        }
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(fixture.path());
        const auto stop = qScopeGuard([&] {
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(5000);
            }
        });
        process.start(executable, {archive, "-t" + type});
        QVERIFY(process.waitForStarted(5000));
        NativeStartupWindow window{DWORD(process.processId()),
                                   refused ? QString("7-Zip") : QFileInfo(archive).fileName(),
                                   refused};
        QElapsedTimer timeout;
        timeout.start();
        while (!window.Window && timeout.elapsed() < 15000 &&
               process.state() != QProcess::NotRunning) {
            EnumWindows(FindStartupWindow, reinterpret_cast<LPARAM>(&window));
            QTest::qWait(20);
        }
        QVERIFY2(window.Window, process.readAll().constData());
        QVERIFY(PostMessageW(window.Window, WM_CLOSE, 0, 0));
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(SourceExtractFixture::readFile(archive), archiveBytes);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
    }
#endif
};

QTEST_MAIN(FileManagerStartupTests)
#include "file_manager_startup_tests.moc"
