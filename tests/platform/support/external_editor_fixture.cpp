#include "platform/support/external_editor_fixture.h"
#include "platform/process/qtexternalprocess.h"
#include "settings/ViewSettings.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#ifndef Q_OS_WIN
#include <spawn.h>
#ifdef Q_OS_MACOS
#include <crt_externs.h>
#else
extern char **environ;
#endif
#endif

namespace ExternalEditorFixture {
void waitForKnownEditor() {
    QEventLoop loop;
    QTimer::singleShot(2200, &loop, &QEventLoop::quit);
    loop.exec();
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString executable() { return QStandardPaths::findExecutable("7z"); }
bool createArchive(const QString &directory, const QString &archive, const QString &name,
                   const QString &format, const QString &password) {
    QProcess process;
    process.setWorkingDirectory(directory);
    QStringList arguments = {"a", "-t" + format};
    if (!password.isEmpty()) {
        arguments.append("-p" + password);
        arguments.append(format == "zip" ? "-mem=AES256" : "-mhe=on");
    }
    arguments.append({"--", archive, name});
    process.start(executable(), arguments);
    return process.waitForFinished(10000) && process.exitCode() == 0;
}
QByteArray contents(const QString &archive, const QString &name, const QString &password) {
    QTemporaryDir selection;
    const QString list = selection.filePath("members.txt");
    // Filesystem-created fixtures can store NFD names, while native Rename
    // preserves the entered NFC name. QProcess filename encoding must not
    // change either reference selection; these fixtures use unique names.
    QStringList names{name.normalized(QString::NormalizationForm_C),
                      name.normalized(QString::NormalizationForm_D)};
    names.removeDuplicates();
    if (!selection.isValid() || !write(list, (names.join('\n') + '\n').toUtf8()))
        return {};
    QProcess process;
    QStringList arguments = {"x", "-so", "-spd", "-ssc", "-scsUTF-8", "-i@" + list};
    if (!password.isEmpty())
        arguments.append("-p" + password);
    arguments.append({"--", archive});
    process.start(executable(), arguments);
    return process.waitForFinished(10000) && process.exitCode() == 0
               ? process.readAllStandardOutput()
               : QByteArray();
}
QtKnownAssociationOpener opener(QString control, QString mode) {
    return [control = std::move(control), mode = std::move(mode)](const UString &path,
                                                                  IQtProcessHandle *&handle) {
        UStringVector paths;
        paths.Add(path);
        QtNativeProcessServices process;
        return process.Create(ToUString(QCoreApplication::applicationFilePath()),
                              ToUString(QString("%1 \"%2\"").arg(mode, control)), paths, handle);
    };
}
int run(int argc, char **argv) {
    if (argc != 4 || !QByteArray(argv[1]).startsWith("--archive-editor"))
        return -1;
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments()[1];
    const QString control = app.arguments()[2];
    const QString path = app.arguments()[3];
    if (mode == "--archive-editor-parent") {
        const QString program = QCoreApplication::applicationFilePath();
        const QString childMode = "--archive-editor";
#ifdef Q_OS_WIN
        if (!QProcess::startDetached(program, {childMode, control, path}))
            return 3;
#else
        // Deliberately inherit the launched parent's group, then orphan the
        // child. QProcess::startDetached would create a different session.
        QByteArray bytes[] = {QFile::encodeName(program), childMode.toUtf8(),
                              QFile::encodeName(control), QFile::encodeName(path)};
        char *arguments[] = {bytes[0].data(), bytes[1].data(), bytes[2].data(), bytes[3].data(),
                             nullptr};
#ifdef Q_OS_MACOS
        char **environment = *_NSGetEnviron();
#else
        char **environment = environ;
#endif
        pid_t child = 0;
        if (posix_spawn(&child, bytes[0].constData(), nullptr, nullptr, arguments, environment) !=
            0)
            return 3;
#endif
        write(control + ".parent-finished", "done");
        return 0;
    }
    if (!write(control + ".ready", path.toUtf8()))
        return 1;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (QFileInfo(control + ".ready").exists() && !QFileInfo(control + ".exit").exists())
            return;
        if (read(control) == "change" && !write(path, modified)) {
            app.exit(2);
            return;
        }
        write(control + ".finished", "done");
        app.quit();
    });
    timer.start(10);
    return app.exec();
}
} // namespace ExternalEditorFixture
