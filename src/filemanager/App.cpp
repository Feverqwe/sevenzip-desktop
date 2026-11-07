// Adapted from 7-Zip CPP/7zip/UI/FileManager/App.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source application composition and panel integration; copy workflows belong to AppCopy.
#include "filemanager/App.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/AppState.h"
#include "filemanager/external/qtexternalfilesession.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include <QWidget>

static const unsigned kParentIndex = (unsigned)(int)-1;

CApp::CApp() : _qtOperations(Panels) { CreateDragTarget(); }

CApp::~CApp() { ReleaseApp(); }

struct CButtonInfo {
    int CommandID;
    UINT BitmapResID;
    UINT Bitmap2ResID;
    UINT StringResID;

    UString GetText() const { return LangString(StringResID); }
};

static const CButtonInfo g_StandardButtons[] = {
    {IDM_COPY_TO, IDB_COPY, IDB_COPY2, IDS_BUTTON_COPY},
    {IDM_MOVE_TO, IDB_MOVE, IDB_MOVE2, IDS_BUTTON_MOVE},
    {IDM_DELETE, IDB_DELETE, IDB_DELETE2, IDS_BUTTON_DELETE},
    {IDM_PROPERTIES, IDB_INFO, IDB_INFO2, IDS_BUTTON_INFO}};

static const CButtonInfo g_ArchiveButtons[] = {
    {kMenuCmdID_Toolbar_Add, IDB_ADD, IDB_ADD2, IDS_ADD},
    {kMenuCmdID_Toolbar_Extract, IDB_EXTRACT, IDB_EXTRACT2, IDS_EXTRACT},
    {kMenuCmdID_Toolbar_Test, IDB_TEST, IDB_TEST2, IDS_TEST}};

static bool SetButtonText(int commandID, const CButtonInfo *buttons, unsigned numButtons,
                          UString &s) {
    for (unsigned i = 0; i < numButtons; i++) {
        const CButtonInfo &b = buttons[i];
        if (b.CommandID == commandID) {
            s = b.GetText();
            return true;
        }
    }
    return false;
}

// External linkage replaces App.cpp::OnNotify's native tooltip callback.
void SetButtonText(int commandID, UString &s) {
    if (SetButtonText(commandID, g_StandardButtons, Z7_ARRAY_SIZE(g_StandardButtons), s))
        return;
    SetButtonText(commandID, g_ArchiveButtons, Z7_ARRAY_SIZE(g_ArchiveButtons), s);
}

bool CApp::ExecuteCommand(unsigned id) {
    return !_qtOperations.closed() && _qtCommandServices && WndProc(id, *this, *_qtCommandServices);
}

void CPanelCallbackImp::OnTab() {
    if (_app->NumPanels != 1)
        _app->Panels[1 - _index].SetFocusToList();
    _app->RefreshTitle();
}

void CPanelCallbackImp::SetFocusToPath(unsigned index) {
    unsigned newPanelIndex = index;
    if (_app->NumPanels == 1)
        newPanelIndex = _app->LastFocusedPanel;
    _app->RefreshTitle();
    _app->Panels[newPanelIndex].SetFocusToPath();
}

void CPanelCallbackImp::OnCopy(bool move, bool copyToSame) {
    _app->OnCopy(move, copyToSame, _index);
}
void CPanelCallbackImp::OnSetSameFolder() { _app->OnSetSameFolder(_index); }
void CPanelCallbackImp::OnSetSubFolder() { _app->OnSetSubFolder(_index); }
void CPanelCallbackImp::PanelWasFocused() { _app->NotifyPanelFocus(_index); }
void CPanelCallbackImp::DragBegin() { _app->DragBegin(_index); }
void CPanelCallbackImp::DragEnd() { _app->DragEnd(); }
void CPanelCallbackImp::RefreshTitle(bool always) { _app->RefreshTitlePanel(_index, always); }

void CApp::NotifyPanelFocus(unsigned index) {
    if (_presentation && !_presentation->AllowPanelFocusChange(index))
        return;
    SetFocusedPanel(index);
    RefreshTitlePanel(index);
}

void CApp::RefreshTitle(bool always) {
    if (_qtOperations.closed())
        return;
    UString path = GetFocusedPanel().GetCurrentFolderPrefix();
    if (path.IsEmpty())
        path = "7-Zip";
    if (!always && path == PrevTitle)
        return;
    PrevTitle = path;
    if (_window)
        _window->setWindowTitle(ToQString(path));
}

void CApp::RefreshTitlePanel(unsigned index, bool always) {
    if (index != GetFocusedPanelIndex())
        return;
    RefreshTitle(always);
}

void CApp::OnSetSameFolder(unsigned srcPanelIndex) {
    if (_qtOperations.closed())
        return;
    if (NumPanels <= 1)
        return;
    const CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[1 - srcPanelIndex];
    if (_presentation)
        _presentation->PrepareFolderChange(1 - srcPanelIndex);
    destPanel.BindToPathAndRefresh(srcPanel._currentFolderPrefix);
    _qtOperations.ContinueScanAfterSourceCall(1 - srcPanelIndex);
}

void CApp::OnSetSubFolder(unsigned srcPanelIndex) {
    if (_qtOperations.closed())
        return;
    if (NumPanels <= 1)
        return;
    const CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[1 - srcPanelIndex];
    const int focusedItem = srcPanel._selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = srcPanel.GetRealItemIndex(focusedItem);
    if (!srcPanel.IsItem_Folder(realIndex))
        return;

    CMyComPtr<IFolderFolder> newFolder;
    if (realIndex == kParentIndex) {
        if (srcPanel._folder->BindToParentFolder(&newFolder) != S_OK)
            return;
        if (!newFolder) {
            {
                const UString parentPrefix = srcPanel.GetParentDirPrefix();
                COpenResult openRes;
                if (_presentation)
                    _presentation->PrepareFolderChange(1 - srcPanelIndex);
                destPanel.BindToPath(parentPrefix, UString(), openRes);
            }
            destPanel.RefreshListCtrl();
            return;
        }
    } else {
        if (srcPanel._folder->BindToFolder(realIndex, &newFolder) != S_OK)
            return;
    }
    if (!newFolder)
        return;
    if (_presentation)
        _presentation->PrepareFolderChange(1 - srcPanelIndex);
    destPanel.CloseOpenFolders();
    destPanel.SetNewFolder(newFolder);
    destPanel.RefreshListCtrl();
}

HRESULT CApp::CreateOnePanel(unsigned panelIndex, const UString &mainPath, const UString &arcFormat,
                             bool needOpenArc, COpenResult &openRes) {
    if (Panels[panelIndex].PanelCreated)
        return S_OK;
    if (!_presentation)
        return E_NOTIMPL;
    m_PanelCallbackImp[panelIndex].Init(this, panelIndex);
    UString path;
    if (mainPath.IsEmpty()) {
        if (!ReadPanelPath(panelIndex, path, _settingsPath))
            path.Empty();
    } else
        path = mainPath;
    const unsigned id = 1000 + 100 * panelIndex; // check it
    Panels[panelIndex].AttachCreationPresentation([this, panelIndex](CPanel &panel) {
        return _presentation->CreatePanel(panel, panelIndex);
    });
    return Panels[panelIndex].Create(_window, _window, id, path, arcFormat,
                                     &m_PanelCallbackImp[panelIndex], &AppState, needOpenArc,
                                     openRes);
}

void CApp::SetListSettings() {
    CFmSettings st;
    st.Load(_settingsPath);
    for (unsigned i = 0; i < kNumPanelsMax; i++) {
        CPanel &panel = Panels[i];
        panel._showDots = st.ShowDots;
        if (_presentation)
            _presentation->SetListSettings(i, st);
    }
}

void CApp::MoveSubWindows() {
    if (_presentation)
        _presentation->MoveSubWindows();
}
HRESULT CApp::Create(QWidget *window, const UString &mainPath, const UString &arcFormat,
                     int xSizes[2], bool needOpenArc, COpenResult &openRes,
                     IQtAppPresentation &presentation, const QString &settingsPath) {
    _qtOperations.ResetForCreate();
    _qtCommandServices = nullptr;
    PrevTitle.Empty();
    _window = window;
    _presentation = &presentation;
    _settingsPath = settingsPath;
    _presentation->Initialize(*this);
    unsigned i;
    for (i = 0; i < kNumPanelsMax; i++)
        Panels[i].PanelCreated = false;
    AppState.Read(_settingsPath);
    SetListSettings();
    if (LastFocusedPanel >= kNumPanelsMax)
        LastFocusedPanel = 0;
    // Qt construction and asynchronous publication can project another panel.
    // Keep the original main-panel choice without changing that live projection.
    const unsigned mainPanelIndex = LastFocusedPanel;
    CListMode listMode;
    listMode.Read(_settingsPath);
    for (i = 0; i < kNumPanelsMax; i++) {
        CPanel &panel = Panels[i];
        panel._listViewMode = listMode.Panels[i];
        panel._xSize = xSizes[i];
    }
    for (i = 0; i < kNumPanelsMax; i++) {
        unsigned panelIndex = i;
        if (needOpenArc && mainPanelIndex == 1)
            panelIndex = 1 - i;
        bool isMainPanel = (panelIndex == mainPanelIndex);
        if (NumPanels > 1 || isMainPanel) {
            if (NumPanels == 1)
                Panels[panelIndex]._xSize = xSizes[0] + xSizes[1];
            COpenResult openRes2;
            UString path;
            if (isMainPanel)
                path = mainPath;
            RINOK(CreateOnePanel(panelIndex, path, arcFormat, isMainPanel && needOpenArc,
                                 *(isMainPanel ? &openRes : &openRes2)))
            if (isMainPanel) {
                if (needOpenArc && !openRes.ArchiveIsOpened)
                    return S_OK;
            }
        }
    }
    SetFocusedPanel(mainPanelIndex);
    Panels[mainPanelIndex].SetFocusToList();
    return S_OK;
}

HRESULT CApp::SwitchOnOffOnePanel() {
    if (NumPanels == 1) {
        NumPanels++;
        COpenResult openRes;
        const HRESULT result =
            CreateOnePanel(1 - LastFocusedPanel, UString(), UString(), false, openRes);
        if (result != S_OK) {
            NumPanels--;
            return result;
        }
        if (_presentation)
            _presentation->ShowPanel(1 - LastFocusedPanel, true);
    } else {
        NumPanels--;
        if (_presentation)
            _presentation->ShowPanel(1 - LastFocusedPanel, false);
    }
    MoveSubWindows();
    return S_OK;
}

void CApp::Save() {
    AppState.Save(_settingsPath);
    CListMode listMode;
    listMode.Read(_settingsPath);
    for (unsigned i = 0; i < kNumPanelsMax; i++) {
        const CPanel &panel = Panels[i];
        if (!panel.PanelCreated)
            continue;
        SavePanelPath(i, panel.GetResumeFolderPath(), _settingsPath);
        listMode.Panels[i] = panel.GetListViewMode();
    }
    listMode.Save(_settingsPath);
}

void CApp::ReleaseApp() {
    _copyRequest.reset();
    _qtOperations.Close();
    _qtCommandServices = nullptr;
    // 24.09: ReleasePanel() will stop panel timer processing.
    // but we want to stop timer processing for all panels
    // before ReleasePanel() calling.
    unsigned i;
    for (i = 0; i < kNumPanelsMax; i++)
        Panels[i].Disable_Processing_Timer_Notify_StatusBar();
    // It's for unloading COM dll's: don't change it.
    for (i = 0; i < kNumPanelsMax; i++)
        Panels[i].ReleasePanel();
}

void CFolderHistory::Normalize() {
    const unsigned kMaxSize = 100;
    if (Strings.Size() > kMaxSize)
        Strings.DeleteFrom(kMaxSize);
}

void CFolderHistory::AddString(const UString &s) {
    AddUniqueStringToHeadOfList(Strings, s);
    Normalize();
}

CPanel::CPanel(const QString &prefix, const CObjectVector<CPanelItem> &items)
    : _currentFolderPrefix(ToUString(prefix)) {
    SetNewFolder(new QtPanelFolder(prefix, items));
}

CPanel::CPanel(IFolderFolder *folder) {
    SetNewFolder(folder);
    if (_folder)
        _currentFolderPrefix = GetFolderPath(_folder);
}
