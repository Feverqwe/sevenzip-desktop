// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItemOpen.cpp
// (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17). Igor Pavlov, LGPL-2.1-or-later; see
// third_party/7zip/License.txt.
#include "filemanager/external/qtexternalapplications.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileName.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include <QMessageBox>

using namespace NWindows;
using namespace NFile;
using namespace NDir;

void SplitCmdLineSmart(const UString &cmd, UString &prg, UString &params) {
    params.Empty();
    prg = cmd;
    prg.Trim();
    if (prg.Len() >= 2 && prg[0] == L'"') {
        int pos = prg.Find(L'"', 1);
        if (pos >= 0) {
            if ((unsigned)(pos + 1) == prg.Len() || prg[pos + 1] == ' ') {
                params = prg.Ptr((unsigned)(pos + 1));
                params.Trim();
                prg.DeleteFrom((unsigned)pos);
                prg.DeleteFrontal(1);
            }
        }
    }
}

HRESULT StartAppWithParams(const UString &cmd, const UStringVector &paramVector,
                           IQtExternalFileServices &process, QWidget *window,
                           IQtProcessHandle **knownProcess) {
    UString param;
    UString prg;
    SplitCmdLineSmart(cmd, prg, param);
    param.Trim();
    // The source appends quoted paths to this string. The process service keeps
    // them as argv entries on POSIX and retains native quoting on Windows.
    return knownProcess ? process.CreateKnownProcess(prg, param, paramVector, window, *knownProcess)
                        : process.CreateProcess(prg, param, paramVector, window);
}

HRESULT StartEditApplication(const UString &path, bool useEditor, QWidget *window,
                             IQtExternalFileServices &process, const QString &settingsPath,
                             IQtProcessHandle **knownProcess) {
    UString command;
    ReadRegEditor(useEditor, command, settingsPath);
    if (command.IsEmpty()) {
#ifdef Q_OS_WIN
        FString winDir;
        if (!GetWindowsDir(winDir))
            return S_OK;
        NName::NormalizeDirPathPrefix(winDir);
        command = fs2us(winDir);
        command += L"notepad.exe";
#else
        // There is no upstream POSIX default text editor. View uses the system
        // association; Edit requires a configured executable.
        if (!useEditor)
            return knownProcess
                       ? process.StartApplicationKnown(UString(), path, window, *knownProcess)
                       : process.StartApplication(UString(), path, window);
#endif
    }
    UStringVector params;
    params.Add(path);
    const HRESULT res = StartAppWithParams(command, params, process, window, knownProcess);
    if (res != S_OK) {
        QMessageBox box(QMessageBox::Critical, QStringLiteral("7-Zip"),
                        QString::fromWCharArray(LangString(IDS_CANNOT_START_EDITOR).Ptr()),
                        QMessageBox::Ok, window);
        box.setObjectName("externalEditorError");
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }
    return res;
}
