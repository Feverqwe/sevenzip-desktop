// Adapted from 7-Zip CPP/7zip/UI/FileManager/FM.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Partial port: WM_CLOSE, WM_COMMAND/toolbar dispatch and IsLargePageSupported.
// WinMain exception policy, WinMain2's active command-line body and WM_CREATE
// path/refusal flow are also ported. Qt/CApp services own widget construction,
// the event loop and per-window cleanup. InitInstance retains source placement
// and panel settings flow through narrow Qt window services.
#include "7zip/UI/FileManager/StringUtils.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/MyException.h"
#include "Common/MyWindows.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/System.h"
#include "filemanager/App.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include "filemanager/qtfilemanagerinitservices.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/qtfilemanagerwindowservices.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/memory/qtmemorylockservices.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMessageBox>
#ifdef _WIN32
#include "C/DllSecur.h"
#include "Windows/MemoryLock.h"
#endif

static void ErrorMessage(const wchar_t *s) {
    QMessageBox::critical(nullptr, QStringLiteral("7-Zip"), QString::fromWCharArray(s));
}

static void ErrorMessage(const char *s) { ErrorMessage(QtDiagnosticText(s)); }

extern bool g_LargePagesMode;

const QtFileManagerRamInfo &NQtFileManager::GetRamInfo() {
    static const QtFileManagerRamInfo info = [] {
        QtFileManagerRamInfo result;
        result.Defined = NWindows::NSystem::GetRamSize(result.Size);
        return result;
    }();
    return info;
}

void NQtFileManager::SetMemoryLock(const QString &settingsPath,
                                   const IQtMemoryLockServices &services) {
    if (!services.IsSupported())
        return;
    // if (ReadLockMemoryAdd())
    services.AddLockMemoryPrivilege();

    if (ReadLockMemoryEnable(settingsPath))
        if (services.Get_LargePages_RiskLevel() == 0) {
            // note: child processes can inherit that Privilege
            g_LargePagesMode = services.EnablePrivilege_LockMemory();
        }
}

bool NQtFileManager::InitInstance(int nCmdShow, CApp &g_App, IQtFileManagerInitServices &wnd,
                                  const QString &settingsPath) {
    // Qt registers its QWidget class and constructs menus/icons with CApp.
    // Original Win32 class/style setup and inactive theme experiments are
    // presentation services; the active settings/create/placement flow follows.
    static const int kNumDefaultPanels = 1;
    CWindowInfo info;
    info.maximized = false;
    int x, y, xSize, ySize;
    x = y = xSize = ySize = WindowUseDefault;
    bool windowPosIsRead, g_PanelsInfoDefined;
    info.Read(windowPosIsRead, g_PanelsInfoDefined, settingsPath);
    if (windowPosIsRead) {
        x = info.rect.left;
        y = info.rect.top;
        xSize = info.rect.right - info.rect.left;
        ySize = info.rect.bottom - info.rect.top;
    }
    UInt32 g_SplitterPos = 0;
    if (g_PanelsInfoDefined) {
        g_SplitterPos = info.splitterPos;
        if (info.numPanels < 1 || info.numPanels > 2)
            info.numPanels = kNumDefaultPanels;
        if (info.currentPanel >= 2)
            info.currentPanel = 0;
    } else {
        info.numPanels = kNumDefaultPanels;
        info.currentPanel = 0;
    }
    g_App.NumPanels = info.numPanels;
    g_App.LastFocusedPanel = info.currentPanel;
    if (!wnd.Create(x, y, xSize, ySize, g_PanelsInfoDefined, g_SplitterPos))
        return false;

    if (nCmdShow == NShowCommand::Normal || nCmdShow == NShowCommand::Show ||
        nCmdShow == NShowCommand::Default) {
        if (info.maximized)
            nCmdShow = NShowCommand::Maximized;
        else
            nCmdShow = NShowCommand::Normal;
    }
    // QWidget state replaces g_Maximized; SaveWindowInfo queries isMaximized.
    QtFileManagerPlacement placement;
    if (wnd.GetPlacement(placement)) {
        if (windowPosIsRead)
            placement.normalPosition = info.rect;
        placement.showCmd = (unsigned)nCmdShow;
        wnd.SetPlacement(placement);
    } else
        wnd.Show(nCmdShow);
    return true;
}

int NQtFileManager::WinMain(const std::function<int()> &main2) {
    // Original WinMain exception/exit policy. CApp's per-window RAII release
    // joins external workers before this handler, replacing the global launcher.
    try {
#ifdef _WIN32
        My_SetDefaultDllDirectories();
#endif
        return main2();
    } catch (const CNewException &) {
        ErrorMessage(LangString(IDS_MEM_ERROR));
        return 1;
    } catch (const UString &s) {
        ErrorMessage(s);
        return 1;
    } catch (const AString &s) {
        ErrorMessage(s.Ptr());
        return 1;
    } catch (const wchar_t *s) {
        ErrorMessage(s);
        return 1;
    } catch (const char *s) {
        ErrorMessage(s);
        return 1;
    } catch (int v) {
        ErrorMessage(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error: %1"), (unsigned)v));
        return 1;
    } catch (...) {
        ErrorMessage(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Unknown error"));
        return 1;
    }
}

QtFileManagerStartup WinMain2(const UString &nativeCommandLine) {
    // Original WinMain2 command-line body; Qt supplies GetCommandLineW's
    // string. The commented-out alternate parsers are not active upstream.
    UString commandsString;
    UString programString;
    SplitStringToTwoStrings(nativeCommandLine, programString, commandsString);
    commandsString.Trim();
    UString paramString, tailString;
    SplitStringToTwoStrings(commandsString, paramString, tailString);
    paramString.Trim();
    tailString.Trim();
    return WinMain2(paramString, tailString);
}

QtFileManagerStartup WinMain2(const UString &paramString, const UString &tailString) {
    // Unix argv is already decoded by the OS/Qt. Keep literal quotes and
    // pathname whitespace rather than passing it through the Win32 tokenizer.
    QtFileManagerStartup startup;
    UString &g_MainPath = startup.FullPath;
    UString &g_ArcFormat = startup.ArcFormat;
    if (tailString.IsPrefixedBy("-t"))
        g_ArcFormat = tailString.Ptr(2);
    if (!paramString.IsEmpty())
        g_MainPath = paramString;

    // Original WM_CREATE path preparation, before CApp::Create.
    bool &needOpenArc = startup.NeedOpenArc;
    UString fullPath = g_MainPath;
    if (!fullPath.IsEmpty()) {
        if (!NWindows::NFile::NName::IsAbsolutePath(fullPath)) {
            FString fullPathF;
            if (NWindows::NFile::NName::GetFullPath(us2fs(fullPath), fullPathF))
                fullPath = fs2us(fullPathF);
        }
        if (NWindows::NFile::NFind::DoesFileExist_FollowLink(us2fs(fullPath)))
            needOpenArc = true;
    }
    startup.FullPath = fullPath;
    return startup;
}

void CheckFileManagerStartup(HRESULT res, const QtFileManagerStartup &startup,
                             const COpenResult &openRes) {
    // Native WM_CREATE returns -1 on refusal; the Qt entry catches this
    // construction failure before showing the File Manager window.
    if (res == E_ABORT)
        throw QtFileManagerStartupFailure{};
    const bool needOpenArc = startup.NeedOpenArc;
    const UString &fullPath = startup.FullPath;
    if ((needOpenArc && !openRes.ArchiveIsOpened) || res != S_OK) {
        UString m = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error"));
        if (res == S_FALSE || res == S_OK)
            m = MyFormatNew(openRes.Encrypted ? IDS_CANT_OPEN_ENCRYPTED_ARCHIVE
                                              : IDS_CANT_OPEN_ARCHIVE,
                            fullPath);
        else if (res != S_OK)
            m = HResultToMessage(res);
        if (!openRes.ErrorMessage.IsEmpty()) {
            m.Add_LF();
            m += openRes.ErrorMessage;
        }
        throw QtFileManagerStartupFailure{m};
    }
}

static void ExecuteCommand(unsigned commandID, IQtFileManagerMenuCommandServices &services) {
    switch (commandID) {
    case kMenuCmdID_Toolbar_Add:
        services.AddToArchive();
        break;
    case kMenuCmdID_Toolbar_Extract:
        services.ExtractArchives();
        break;
    case kMenuCmdID_Toolbar_Test:
        services.TestArchives();
        break;
    }
}

bool WndProc(unsigned wmId, CApp &g_App, IQtFileManagerMenuCommandServices &services) {
    if (g_App.QtOperations().closed() || !services.CanExecuteCommand(wmId))
        return false;
    // Exit holds no native folder input. Its Qt close event must be allowed to
    // perform WM_CLOSE; every other synchronous command protects live callers.
    if (wmId == 8) // IDCLOSE
        return OnMenuCommand(wmId, services);
    QtAppOperationState::CallScope call(g_App.QtOperations());
    // Original WM_COMMAND body, after Qt has supplied the action ID instead of
    // LOWORD(wParam). Control-notification messages are handled by Qt signals.
    if (wmId >= kMenuCmdID_Toolbar_Start && wmId < kMenuCmdID_Toolbar_End) {
        ExecuteCommand(wmId, services);
        return true;
    }
    if (OnMenuCommand(wmId, services))
        return true;
    // Existing Qt-only dialogs/navigation entries have no original menu ID.
    // Their explicit registry entries share this owner and capability gate.
    return services.ExecuteExtensionCommand(wmId);
}

bool CloseFileManager(CApp &app, IQtFileManagerWindowServices &services,
                      QtFileManagerCloseReason reason) {
    auto &operations = app.QtOperations();
    if (operations.closed())
        return true;
    // A disabled Win32 modal parent cannot initiate this close workflow.
    // Qt programmatic/reentrant close must not release live caller inputs.
    if (reason == QtFileManagerCloseReason::Request &&
        (operations.callActive() || operations.kind() == QtAppOperationState::Kind::Open ||
         !services.CanCloseNow()))
        return false;
    operations.Close();
    services.Deactivate(); // WM_ACTIVATE / WA_INACTIVE
    services.ExitExternalWorkers(false);
    services.RevokeDropTarget(); // RevokeDragDrop / _dropTarget.Release
    // Qt asynchronous workers can outlive the original modal call stack.
    // Join them while panel folders/temporary inputs still exist.
    services.QuiesceOperations();
    const bool windowWasCreated = app.Panels[0].PanelCreated || app.Panels[1].PanelCreated;
    if (windowWasCreated)
        app.Save();
    app.ReleaseApp();
    if (windowWasCreated)
        services.SaveWindowInfo();
    services.ExitExternalWorkers(true);
    return true;
}

bool IsLargePageSupported() {
#ifdef _WIN32
#ifdef _WIN64
    return true;
#else
    DWORD v = GetVersion();
    // low byte is major version:
    // next byte is minor version:
    v = ((v & 0xff) << 8) | ((v >> 8) & 0xFF);
    return (v > 0x501);
#endif
#else
    // The source FM privilege/large-page command is Windows-only.
    return false;
#endif
}

#ifdef _WIN32
bool g_SymLink_Supported = false;

static void Set_SymLink_Supported() {
    // g_SymLink_Supported = false;
    const DWORD v = GetVersion();
    // low byte is major version:
    if ((Byte)v < 6)
        return;
    g_SymLink_Supported = true;
    // if (g_SymLink_Supported)
    {
        NWindows::NSecurity::EnablePrivilege_SymLink();
    }
}

void QtInitializeLinkPlatform() {
    static const bool initialized = [] {
        Set_SymLink_Supported();
        return true;
    }();
    (void)initialized;
}
#else
// POSIX symbolic links have no Windows type/privilege initialization.
bool g_SymLink_Supported = true;
void QtInitializeLinkPlatform() {}
#endif
