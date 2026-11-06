#pragma once
#include <QByteArray>
#include <QString>

namespace ExternalEditorFixture {
inline const QByteArray modified = "modified external editor contents";
bool write(const QString &path, const QByteArray &bytes);
QByteArray read(const QString &path);
QString executable();
bool createArchive(const QString &directory, const QString &archive, const QString &name,
                   const QString &format = "zip", const QString &password = {});
QByteArray contents(const QString &archive, const QString &name, const QString &password = {});
void configure(const QString &control, const QString &mode = "--archive-editor");
// Exercise the retained native long-process branch, separately from launchers.
void waitForKnownEditor();
// Returns -1 when argv is a test invocation rather than an owned process fixture.
int run(int argc, char **argv);
} // namespace ExternalEditorFixture
