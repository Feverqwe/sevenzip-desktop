// Adapted from 7-Zip CPP/7zip/UI/FileManager/App.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source application commands and panel integration; selection summaries belong to PanelInfo.
#include "filemanager/App.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "archive/agent/Agent.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/AppState.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/external/qtexternalfilesession.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include "fileops/native/FileDir.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QWidget>

static const unsigned kParentIndex = (unsigned)(int)-1;

struct CApp::CopyRequest {
    CopyRequest(unsigned source, unsigned destination, bool move, bool copyToSame)
        : Source(source), Destination(destination), Move(move), CopyToSame(copyToSame) {}
    const unsigned Source;
    const unsigned Destination;
    const bool Move;
    const bool CopyToSame;
    CSelectedState SourceSelection;
    CSelectedState DestinationSelection;
};

CApp::CApp() : _qtOperations(Panels) {
    CreateDragTarget();
    SetPanels_AutoRefresh_Mode();
}

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
void CPanelCallbackImp::PanelWasFocused() {
    _app->SetFocusedPanel(_index);
    _app->RefreshTitlePanel(_index);
}
void CPanelCallbackImp::DragBegin() { _app->DragBegin(_index); }
void CPanelCallbackImp::DragEnd() { _app->DragEnd(); }
void CPanelCallbackImp::RefreshTitle(bool always) { _app->RefreshTitlePanel(_index, always); }

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

// Reduces path to the part that exists on disk (or the root prefix).
// The source helper normalizes the result with WCHAR_PATH_SEPARATOR.
static void Reduce_Path_To_RealFileSystemPath(UString &path) {
    unsigned prefixSize = NWindows::NFile::NName::GetRootPrefixSize(path);
    while (!path.IsEmpty()) {
        if (NWindows::NFile::NFind::DoesDirExist_FollowLink(us2fs(path))) {
            NWindows::NFile::NName::NormalizeDirPathPrefix(path);
            break;
        }
        int pos = path.ReverseFind_PathSepar();
        if (pos < 0) {
            path.Empty();
            break;
        }
        path.DeleteFrom((unsigned)(pos + 1));
        if ((unsigned)pos + 1 == prefixSize)
            break;
        path.DeleteFrom((unsigned)pos);
    }
}

void CApp::OnCopy(bool move, bool copyToSame, unsigned srcPanelIndex) {
    if (!_presentation || srcPanelIndex >= kNumPanelsMax)
        return;
    const unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
    CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[destPanelIndex];
    CPanel::CDisableTimerProcessing2 disableTimerProcessing1(&destPanel);
    CPanel::CDisableTimerProcessing2 disableTimerProcessing2(&srcPanel);

    if (move) {
        if (!srcPanel.CheckBeforeUpdate(IDS_MOVE))
            return;
    } else if (!srcPanel.DoesItSupportOperations()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }

    CRecordVector<UInt32> indices;
    UString destPath;
    bool useDestPanel = false;
    {
        if (copyToSame) {
            const int focusedItem = srcPanel._selectionList.GetFocusedItem();
            if (focusedItem < 0)
                return;
            const unsigned realIndex = srcPanel.GetRealItemIndex(focusedItem);
            if (realIndex == UInt32(-1))
                return;
            indices.Add(realIndex);
            destPath = srcPanel.GetItemName(realIndex);
        } else {
            srcPanel.Get_ItemIndices_OperSmart(indices);
            if (indices.IsEmpty())
                return;
            destPath = destPanel.GetFsPath();
            if (NumPanels == 1)
                Reduce_Path_To_RealFileSystemPath(destPath);
        }
    }

    UStringVector copyFolders;
    ReadCopyHistory(copyFolders, _settingsPath);
    QStringList copyHistory;
    FOR_VECTOR(i, copyFolders)
    copyHistory << ToQString(copyFolders[i]);
    QStringList sourcePaths;
    FOR_VECTOR(i, indices)
    sourcePaths << ToQString(srcPanel.GetItemFullPath(indices[i]));
    CCopyDialog copyDialog(move, ToQString(destPath), sourcePaths, copyHistory, _window,
                           srcPanel.GetItemsInfoString(indices));
    if (copyDialog.exec() != QDialog::Accepted)
        return;
    destPath = copyDialog.Value;
    if (destPath.IsEmpty()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    UString correctName;
    if (!srcPanel.CorrectFsPath(destPath, correctName)) {
        srcPanel.MessageBox_Error_HRESULT(E_INVALIDARG);
        return;
    }
    if (NWindows::NFile::NName::IsAbsolutePath(destPath))
        destPath.Empty();
    else
        destPath = srcPanel.GetFsPath();
    destPath += correctName;

#if defined(_WIN32) && !defined(UNDER_CE)
    if (destPath.Len() != 0 && destPath[0] == '\\')
        if (destPath.Len() == 1 || destPath[1] != '\\') {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        }
#endif

    bool possibleToUseDestPanel = false;
    if (CompareFileNames(destPath, destPanel.GetFsPath()) == 0) {
        if (NumPanels == 1 || CompareFileNames(destPath, srcPanel.GetFsPath()) == 0) {
            srcPanel.MessageBox_Error(QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot copy files onto itself")));
            return;
        }
        if (destPanel.DoesItSupportOperations())
            possibleToUseDestPanel = true;
    }

    bool destIsFsPath = false;
    if (possibleToUseDestPanel) {
        if (destPanel.IsFSFolder() || destPanel.IsAltStreamsFolder())
            destIsFsPath = true;
        else if (destPanel.IsFSDrivesFolder() || destPanel.IsRootFolder()) {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        }
    } else {
#ifdef _WIN32
        // Alternate-stream destinations are allowed only through a peer ADS folder.
        if (NWindows::NFile::NName::IsAltPathPrefix(us2fs(destPath))) {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        } else
#endif
        {
            if (indices.Size() == 1 && !destPath.IsEmpty() && !IsPathSepar(destPath.Back())) {
                const int pos = destPath.ReverseFind_PathSepar();
                if (pos < 0) {
                    srcPanel.MessageBox_Error_UnsupportOperation();
                    return;
                }
                UString prefix = destPath.Left(pos + 1);
                if (!NWindows::NFile::NDir::CreateComplexDir(us2fs(prefix))) {
                    const HRESULT lastError = GetLastError_noZero_HRESULT();
                    srcPanel.MessageBox_Error_2Lines_Message_HRESULT(prefix, lastError);
                    return;
                }
            } else {
                NWindows::NFile::NName::NormalizeDirPathPrefix(destPath);
                if (!NWindows::NFile::NDir::CreateComplexDir(us2fs(destPath))) {
                    const HRESULT lastError = GetLastError_noZero_HRESULT();
                    srcPanel.MessageBox_Error_2Lines_Message_HRESULT(destPath, lastError);
                    return;
                }
            }
            destIsFsPath = true;
        }
    }
    if (!destIsFsPath)
        useDestPanel = true;

    // Qt filesystem execution consumes portable slashes; classification above
    // retains the source path and provider rules before this string boundary.
    QString output = QDir::cleanPath(ToQString(destPath));
    if (!destPath.IsEmpty() && IsPathSepar(destPath.Back()) && !output.endsWith('/'))
        output += '/';
    AddUniqueStringToHeadOfList(copyFolders, ToUString(output));
    while (copyFolders.Size() > 20)
        copyFolders.DeleteBack();
    SaveCopyHistory(copyFolders, _settingsPath);

    const bool useSrcPanel = !useDestPanel || !srcPanel.Is_IO_FS_Folder();
    const bool useTemp = useSrcPanel && useDestPanel;
    if (useTemp && NumPanels == 1) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    auto request = std::make_shared<CopyRequest>(srcPanelIndex, destPanelIndex, move, copyToSame);
    srcPanel.SaveSelectedState(request->SourceSelection);
    destPanel.SaveSelectedState(request->DestinationSelection);
    // Keep the original RefreshListCtrl caller and its saved state across the
    // asynchronous presentation boundary. OnCopy ignores reload HRESULTs.
    _copyRequest = std::move(request);
    // Qt execution can return while the operation owner retains these guards.
    // Unwind the source stack in reverse order before that owner snapshots it.
    disableTimerProcessing2.Restore();
    disableTimerProcessing1.Restore();
    const auto route = !useDestPanel ? QtCopyRoute::FileSystem
                       : useTemp     ? QtCopyRoute::StagedDestinationPanel
                                     : QtCopyRoute::DestinationPanel;
    _presentation->ExecuteCopy(move, copyToSame, srcPanelIndex, output, indices, route);
}

void CApp::CompleteCopy() {
    auto request = std::move(_copyRequest);
    if (!request || _qtOperations.closed())
        return;
    // The original post-operation body runs after Qt publishes both panels.
    CPanel &srcPanel = Panels[request->Source];
    CPanel &destPanel = Panels[request->Destination];
    CPanel::CDisableNotify disableNotify1(destPanel);
    CPanel::CDisableNotify disableNotify2(srcPanel);
    RefreshTitleAlways();
    if (request->CopyToSame || request->Move)
        srcPanel.RestoreLoadedSelection(request->SourceSelection);
    if (!request->CopyToSame) {
        destPanel.RestoreLoadedSelection(request->DestinationSelection);
        srcPanel.KillSelection();
    }
    disableNotify1.Restore();
    disableNotify2.Restore();
    srcPanel.SetFocusToList();
}

bool CApp::RefreshCopyPanel(unsigned index) {
    const auto request = _copyRequest;
    if (!request || _qtOperations.closed() || index >= kNumPanelsMax)
        return false;
    // Synchronous publication can re-enter completion and release the owner.
    if (index == request->Source)
        Panels[index].RefreshListCtrl(request->SourceSelection);
    else if (index == request->Destination)
        Panels[index].RefreshListCtrl(request->DestinationSelection);
    return true;
}

QtArchiveTransferResult CApp::CopyArchiveToArchive(unsigned srcPanelIndex,
                                                   const CRecordVector<UInt32> &indices,
                                                   UStringVector *messages) {
    if (NumPanels != 2 || srcPanelIndex >= kNumPanelsMax || indices.IsEmpty())
        return {E_INVALIDARG, {}};
    const unsigned destPanelIndex = 1 - srcPanelIndex;
    CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[destPanelIndex];
    CPanel::CDisableTimerProcessing disableTimerProcessing1(destPanel);
    CPanel::CDisableTimerProcessing disableTimerProcessing2(srcPanel);

    NWindows::NFile::NDir::CTempDir tempDirectory;
    if (!tempDirectory.Create(FTEXT("7zO")))
        return {E_FAIL, {}};
    FString tempDirPrefix = tempDirectory.GetPath();
    NWindows::NFile::NName::NormalizeDirPathPrefix(tempDirPrefix);

    CCopyToOptions options;
    options.folder = fs2us(tempDirPrefix);
    // CAgentFolder::CopyTo returns E_NOTIMPL for move. The caller deletes the
    // source only after the destination transaction commits.
    options.moveMode = false;
    options.includeAltStreams = true;
    options.replaceAltStreamChars = false;
    options.showErrorMessages = true;
    QtArchiveExtractReport report;
    report.Purpose = QtArchiveExtractPurpose::ArchiveStaging;
    HRESULT result = srcPanel.CopyTo(options, indices, messages, &report);

    if (result == S_OK && (!messages || messages->IsEmpty())) {
        if (!report.Status || !report.Status->Complete || !report.Status->StagingCompatible)
            return {E_FAIL, QCoreApplication::translate(
                                "archive_transfer",
                                "Some selected items cannot be transferred without data or path "
                                "changes. Both archives were left unchanged.")};
        UStringVector filePaths;
        filePaths.ClearAndReserve(indices.Size());
        FOR_VECTOR(i, indices)
        filePaths.AddInReserved(srcPanel.GetItemName_for_Copy(indices[i]));
        auto *destination = dynamic_cast<CAgentFolder *>(destPanel.GetFolder());
        CMyComPtr<IUnknown> resourceOwner =
            destination ? destination->GetEngineResource() : nullptr;
        auto *resource = dynamic_cast<QtArchiveFileResource *>(resourceOwner.Interface());
        const auto reader = resource ? resource->NativeReaderLease() : nullptr;
        const auto *format = reader ? reader->formatInfo() : nullptr;
        const auto &inputs = report.Status->Inputs;
        const bool hasMetadata =
            inputs.SymLinks || inputs.HardLinks || !inputs.AltStreamPaths.isEmpty();
        if (hasMetadata && (!format || (inputs.SymLinks && !format->Flags_SymLinks()) ||
                            (inputs.HardLinks && !format->Flags_HardLinks()) ||
                            (!inputs.AltStreamPaths.isEmpty() && !format->Flags_AltStreams())))
            return {E_NOTIMPL, QCoreApplication::translate(
                                   "archive_transfer",
                                   "The destination format cannot preserve the selected links or "
                                   "alternate streams. Both archives were left unchanged.")};
        std::optional<QScopedValueRollback<std::optional<QtArchiveTransferInputs>>> transferInputs;
        if (resource)
            transferInputs.emplace(resource->TransferInputs(), inputs);
        result = destPanel.CopyFrom(false, fs2us(tempDirPrefix), filePaths, true, messages);
    }
    return {result, {}};
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
    if (_presentation)
        _presentation->RefreshFolder(1 - srcPanelIndex);
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
            if (_presentation)
                _presentation->RefreshFolder(1 - srcPanelIndex);
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
    if (_presentation)
        _presentation->RefreshFolder(1 - srcPanelIndex);
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
    ShowSystemMenu = st.ShowSystemMenu;
    for (unsigned i = 0; i < kNumPanelsMax; i++) {
        CPanel &panel = Panels[i];
        panel.SetAlternativeSelection(st.AlternativeSelection);
        panel._showDots = st.ShowDots;
        panel._showRealFileIcons = st.ShowRealFileIcons;
        // Qt view styles replace LVS_EX/LVS_SINGLESEL bit fields.
        if (_presentation)
            _presentation->SetListSettings(i, st);
    }
}

void CApp::ReadToolbar(bool smallScreen) {
    const UInt32 mask = ReadToolbarsMask(_settingsPath);
    if (mask & ((UInt32)1 << 31)) {
        ShowButtonsLables = !smallScreen;
        LargeButtons = false;
        ShowStandardToolbar = ShowArchiveToolbar = true;
    } else {
        ShowButtonsLables = ((mask & 1) != 0);
        LargeButtons = ((mask & 2) != 0);
        ShowStandardToolbar = ((mask & 4) != 0);
        ShowArchiveToolbar = ((mask & 8) != 0);
    }
}
void CApp::SaveToolbar() {
    UInt32 mask = 0;
    if (ShowButtonsLables)
        mask |= 1;
    if (LargeButtons)
        mask |= 2;
    if (ShowStandardToolbar)
        mask |= 4;
    if (ShowArchiveToolbar)
        mask |= 8;
    SaveToolbarsMask(mask, _settingsPath);
}
void CApp::ReloadToolbars() {
    if (_presentation)
        _presentation->ReloadToolbars(*this);
}
void CApp::MoveSubWindows() {
    if (_presentation)
        _presentation->MoveSubWindows();
}
void CApp::SaveToolbarChanges() {
    SaveToolbar();
    ReloadToolbars();
    MoveSubWindows();
}
void CApp::SwitchStandardToolbar() {
    ShowStandardToolbar = !ShowStandardToolbar;
    SaveToolbarChanges();
}
void CApp::SwitchArchiveToolbar() {
    ShowArchiveToolbar = !ShowArchiveToolbar;
    SaveToolbarChanges();
}
void CApp::SwitchButtonsLables() {
    ShowButtonsLables = !ShowButtonsLables;
    SaveToolbarChanges();
}
void CApp::SwitchLargeButtons() {
    LargeButtons = !LargeButtons;
    SaveToolbarChanges();
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
    // Qt child/menu construction replaces Win32 handles, not toolbar state.
    _presentation->Initialize(*this);
    ReadToolbar(_presentation->IsSmallScreen());
    ReloadToolbars();
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
        panel._flatModeForArc = ReadFlatView(i, _settingsPath);
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
        UString path;
        if (panel._parentFolders.IsEmpty())
            path = panel._currentFolderPrefix;
        else
            path = panel._parentFolders[0].ParentFolderPath;
        SavePanelPath(i, path, _settingsPath);
        listMode.Panels[i] = panel.GetListViewMode();
        SaveFlatView(i, panel._flatModeForArc, _settingsPath);
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
