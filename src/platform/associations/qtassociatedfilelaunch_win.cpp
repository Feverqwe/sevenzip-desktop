// Windows association launcher adapted from 7-Zip BrowseDialog2.cpp.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "platform/associations/qtassociatedfilelaunch_win.h"
#include "Common/MyCom.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "Windows/ProcessUtils.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/loader/DLL.h"
#include "platform/shell/Shell.h"

using namespace NWindows;
extern UString HResultToMessage(HRESULT errorCode);

HRESULT ShellFolder_ParseDisplayName(IShellFolder *shellFolder, HWND hwnd, const UString &path,
                                     LPITEMIDLIST *ppidl);

HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window,
                         CProcess &process) {
    UString path2 = path;
    HRESULT error = S_OK;
    {
#ifdef _WIN32
        NShell::CItemIDList pidl;
        // SHELLEXECUTEINFO::pidl is more accurate way than SHELLEXECUTEINFO::lpFile
        {
            CMyComPtr<IShellFolder> desktop;
            if (SHGetDesktopFolder(&desktop) == S_OK && desktop)
                if (ShellFolder_ParseDisplayName(desktop,
                                                 NULL, // HWND : do we need (window) or NULL here?
                                                 path, &pidl) != S_OK)
                    pidl.Detach();
        }
        {
            const int dot = path2.ReverseFind_Dot();
            const int separ = path2.ReverseFind_PathSepar();
            if (separ != (int)path2.Len() - 1)
                if (dot < 0 || dot < separ)
                    path2.Add_Dot();
        }
#endif // _WIN32

#ifndef _UNICODE
        if (g_IsNT) {
            SHELLEXECUTEINFOW execInfo;
            memset(&execInfo, 0, sizeof(execInfo));
            // execInfo.hwnd = NULL;
            // execInfo.lpVerb = NULL;
            // execInfo.lpFile = NULL;
            // execInfo.lpDirectory = NULL;
            // execInfo.lpParameters = NULL;
            // execInfo.hProcess = NULL;
            execInfo.cbSize = sizeof(execInfo);
            execInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
            if (!dir.IsEmpty())
                execInfo.lpDirectory = dir;
            execInfo.nShow = SW_SHOWNORMAL;

            if ((LPCITEMIDLIST)pidl) {
                execInfo.lpIDList = pidl;
                execInfo.fMask |= SEE_MASK_IDLIST;
            } else
                execInfo.lpFile = path2;

            typedef BOOL(WINAPI * Func_ShellExecuteExW)(LPSHELLEXECUTEINFOW lpExecInfo);
            Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION
            const Func_ShellExecuteExW f_ShellExecuteExW = Z7_GET_PROC_ADDRESS(
                Func_ShellExecuteExW, ::GetModuleHandleW(L"shell32.dll"), "ShellExecuteExW");
            if (!f_ShellExecuteExW)
                error = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
            else if (!f_ShellExecuteExW(&execInfo))
                error = GetLastError_noZero_HRESULT();
            process.Attach(execInfo.hProcess);
        } else
#endif
        {
            SHELLEXECUTEINFO execInfo;
            memset(&execInfo, 0, sizeof(execInfo));
            // execInfo.hwnd = NULL;
            // execInfo.lpVerb = NULL;
            // execInfo.lpFile = NULL;
            // execInfo.lpDirectory = NULL;
            // execInfo.lpParameters = NULL;
            // execInfo.hProcess = NULL;
            execInfo.cbSize = sizeof(execInfo);
            execInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI
#ifndef UNDER_CE
                             | SEE_MASK_FLAG_DDEWAIT
#endif
                ;
            execInfo.nShow = SW_SHOWNORMAL;
            const CSysString sysPath(GetSystemString(path2));
            const CSysString sysDir(GetSystemString(dir));
#ifndef UNDER_CE
            if (!sysDir.IsEmpty())
                execInfo.lpDirectory = sysDir;
#endif

            if ((LPCITEMIDLIST)pidl) {
                execInfo.lpIDList = pidl;
                execInfo.fMask |= SEE_MASK_IDLIST;
            } else
                execInfo.lpFile = sysPath;
            if (!::ShellExecuteEx(&execInfo))
                error = GetLastError_noZero_HRESULT();
            process.Attach(execInfo.hProcess);
        }
    }
    if (error == S_OK)
        return S_OK;
    if (error == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return E_ABORT;
    QtShowAssociationOpenError(QString::fromWCharArray(path.Ptr()), window,
                               QString::fromWCharArray(HResultToMessage(error).Ptr()));
    return E_FAIL;
}
