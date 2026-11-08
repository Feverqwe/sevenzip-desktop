// PanelDrag.cpp
// Adapted from 7-Zip (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/Common/ArchiveName.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "fileops/patterns/Wildcard.h"

void CDropTarget::RemoveSelection() {
    if (m_Panel) {
        m_Panel->m_DropHighlighted_SubFolderName.Empty();
        if (m_Panel->m_DropHighlighted_SelectionIndex >= 0) {
            if (presentation_)
                for (unsigned i = 0; i < kNumPanelsMax; ++i)
                    if (&App->Panels[i] == m_Panel)
                        presentation_->Highlight(i, m_Panel->m_DropHighlighted_SelectionIndex,
                                                 false);
            m_Panel->m_DropHighlighted_SelectionIndex = -1;
        }
    }
}
void CDropTarget::PositionCursor(int x, int y) {
    RemoveSelection();
    m_IsAppTarget = true;
    m_Panel = NULL;
    m_PanelDropIsAllowed = false;
    if (!m_DropIsAllowed)
        return;
    // Qt replaces ChildWindowFromPointEx/WindowFromPoint/ListView_HitTest.
    const int hit = presentation_ ? presentation_->PanelAtPoint(x, y) : -1;
    for (unsigned i = 0; i < kNumPanelsMax; i++)
        if (App->IsPanelVisible(i)) {
            CPanel *panel = &App->Panels[i];
            if (presentation_ && presentation_->PanelEnabled(i) && hit == (int)i) {
                m_Panel = panel;
                m_IsAppTarget = false;
                if ((int)i == SrcPanelIndex)
                    return;
                if (panel->IsArcFolder() && panel->IsThereReadOnlyFolder())
                    return;
                break;
            }
        }
    m_PanelDropIsAllowed = true;
    if (!m_Panel) {
        if (TargetPanelIndex >= 0)
            m_Panel = &App->Panels[TargetPanelIndex];
        return;
    }
    if (!m_Panel->IsFsOrPureDrivesFolder())
        return;
    if (!presentation_)
        return;
    const int index =
        presentation_->FolderAtPoint((unsigned)hit, x, y, m_Panel->m_DropHighlighted_SubFolderName);
    if (index < 0)
        return;
    presentation_->Highlight((unsigned)hit, index, true);
    m_Panel->m_DropHighlighted_SelectionIndex = index;
}
bool CDropTarget::IsFsFolderPath() const {
    if (m_IsAppTarget || !m_Panel)
        return false;
    if (m_Panel->IsFSFolder())
        return true;
    if (m_Panel->IsAltStreamsFolder())
        return true;
    return m_Panel->IsFSDrivesFolder() && m_Panel->m_DropHighlighted_SelectionIndex >= 0;
}
bool CDropTarget::IsItSameDrive() const {
    if (!m_Panel)
        return false;
    if (!IsFsFolderPath())
        return false;
    UString drive;
    if (m_Panel->IsFSFolder()) {
        drive = m_Panel->GetDriveOrNetworkPrefix();
        if (drive.IsEmpty())
            return false;
    } else if (m_Panel->IsFSDrivesFolder() && m_Panel->m_DropHighlighted_SelectionIndex >= 0) {
        drive = m_Panel->m_DropHighlighted_SubFolderName;
        drive.Add_PathSepar();
    } else
        return false;
    if (m_SourcePaths.Size() == 0)
        return false;
#ifdef _WIN32
    FOR_VECTOR(i, m_SourcePaths) {
        if (!m_SourcePaths[i].IsPrefixedBy_NoCase(drive))
            return false;
    }
    return true;
#else
    // POSIX root prefixes cannot distinguish mounted volumes. Retain the source
    // admission above and replace only its Windows volume-identity comparison.
    return presentation_ && presentation_->IsSameDrive(GetTargetPath(), m_SourcePaths);
#endif
}
static DWORD GetEffect_ForKeys(DWORD keyState) {
    if (keyState & MK_CONTROL) {
        if (keyState & MK_ALT)
            return 0;
        if (keyState & MK_SHIFT)
            return DROPEFFECT_LINK;
        return DROPEFFECT_COPY;
    }
    if (keyState & MK_SHIFT) {
        if (keyState & MK_ALT)
            return 0;
        return DROPEFFECT_MOVE;
    }
    if (keyState & MK_ALT)
        return DROPEFFECT_LINK;
    return 0;
}
DWORD CDropTarget::GetEffect(DWORD keyState, DWORD allowedEffect) const {
    if (!m_DropIsAllowed || !m_PanelDropIsAllowed)
        return 0;
    if (!IsFsFolderPath() || !m_TargetPath_WasSent_ToDataObject) {
        // Original source leaves this disabled: allowedEffect &= ~DROPEFFECT_MOVE;
    }
    DWORD effect;
    {
        effect = GetEffect_ForKeys(keyState);
        if (effect == DROPEFFECT_LINK)
            return 0;
        effect &= allowedEffect;
    }
    if (effect == 0) {
        if (allowedEffect & DROPEFFECT_COPY)
            effect = DROPEFFECT_COPY;
        if (allowedEffect & DROPEFFECT_MOVE)
            if (IsItSameDrive())
                effect = DROPEFFECT_MOVE;
    }
    return effect;
}
UString CDropTarget::GetTargetPath() const {
    if (!IsFsFolderPath())
        return UString();
    UString path = m_Panel->GetFsPath();
    if (!m_Panel->m_DropHighlighted_SubFolderName.IsEmpty()) {
        path += m_Panel->m_DropHighlighted_SubFolderName;
        path.Add_PathSepar();
    }
    return path;
}
void CDropTarget::LoadNames_From_DataObject(const UStringVector &paths, bool is7zip) {
    m_SourcePaths = paths;
    m_DropIsAllowed = !paths.IsEmpty();
    m_TargetPath_WasSent_ToDataObject = is7zip;
}
void CDropTarget::ClearState() {
    RemoveSelection();
    m_SourcePaths.Clear();
    m_DropIsAllowed = false;
    m_PanelDropIsAllowed = false;
    m_Panel = NULL;
    m_IsAppTarget = false;
    m_TargetPath_WasSent_ToDataObject = false;
    m_TargetPath_NonEmpty_WasSent_ToDataObject = false;
    m_IsRightButton = false;
    m_DataObject = nullptr;
}

UInt32 CDropTarget::GetFolderType() const {
    if (m_IsAppTarget || !m_Panel)
        return k_FolderType_None;
    if (m_Panel->IsFSFolder() ||
        (m_Panel->IsFSDrivesFolder() && m_Panel->m_DropHighlighted_SelectionIndex >= 0))
        return k_FolderType_Fs;
    if (m_Panel->IsAltStreamsFolder())
        return k_FolderType_AltStreams;
    if (m_Panel->IsArcFolder())
        return k_FolderType_Archive;
    return k_FolderType_Unknown;
}
HRESULT CDropTarget::LoadNames_From_DataObject(CDataObject *dataObject) {
    m_SourcePaths.Clear();
    const HRESULT result = dataObject ? dataObject->GetData(m_SourcePaths) : E_INVALIDARG;
    m_DropIsAllowed = result == S_OK && !m_SourcePaths.IsEmpty();
    return result;
}
bool CDropTarget::SendToSource_TargetPath_enable(CDataObject *dataObject, bool enablePath) {
    m_TargetPath_NonEmpty_WasSent_ToDataObject = false;
    UString path;
    if (enablePath)
        path = GetTargetPath();
    if (!dataObject)
        return false;
    if (dataObject->NativeData) {
        if (!dataObject->NativeData->SetTargetFolder(path))
            return false;
    } else {
        if (!dataObject->Panel)
            return false;
        dataObject->DestDirPrefix_FromTarget = path;
        dataObject->m_DestDirPrefix_FromTarget_WasSet = true;
    }
    if (!path.IsEmpty())
        m_TargetPath_NonEmpty_WasSent_ToDataObject = true;
    return true;
}
bool CDropTarget::SendToSource_TransferInfo(CDataObject *dataObject,
                                            const CTargetTransferInfo &info) {
    if (!dataObject)
        return false;
    if (dataObject->NativeData)
        return dataObject->NativeData->SetTransfer(info);
    if (!dataObject->Panel)
        return false;
    dataObject->SetTransfer(info);
    return true;
}
void CDropTarget::SendToSource_auto(CDataObject *dataObject, const CTargetTransferInfo &info) {
    bool need_Send = false;
    if (info.FuncType == k_DragTargetMode_Enter || info.FuncType == k_DragTargetMode_Over ||
        (info.FuncType == k_DragTargetMode_Drop_Begin && info.Cmd_Type != NDragMenu::k_Cancel))
        need_Send = m_DropIsAllowed && m_PanelDropIsAllowed && IsFsFolderPath();
    m_TargetPath_WasSent_ToDataObject = SendToSource_TargetPath_enable(dataObject, need_Send);
    SendToSource_TransferInfo(dataObject, info);
}
HRESULT CDropTarget::DragEnter(CDataObject *dataObject, DWORD keyState, int x, int y,
                               DWORD *effect) {
    try {
        m_IsRightButton = (keyState & (MK_RBUTTON | MK_MBUTTON)) != 0;
        RINOK(LoadNames_From_DataObject(dataObject))
        m_DataObject = dataObject;
        PositionCursor(x, y);
        CTargetTransferInfo target;
        target.FuncType = k_DragTargetMode_Enter;
        target.KeyState = keyState;
        target.X = x;
        target.Y = y;
        target.OkEffects = *effect;
        SendToSource_auto(m_DataObject, target);
        UInt32 flags = 0;
        if (dataObject->ReadTransferFlags(flags)) {
            if (flags & k_SourceFlags_LeftButton)
                m_IsRightButton = false;
            else if (flags & k_SourceFlags_RightButton)
                m_IsRightButton = true;
        }
        *effect = GetEffect(keyState, *effect);
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT CDropTarget::DragOver(DWORD keyState, int x, int y, DWORD *effect) {
    try {
        PositionCursor(x, y);
        CTargetTransferInfo target;
        target.FuncType = k_DragTargetMode_Over;
        target.KeyState = keyState;
        target.X = x;
        target.Y = y;
        target.OkEffects = *effect;
        SendToSource_auto(m_DataObject, target);
        *effect = GetEffect(keyState, *effect);
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT CDropTarget::DragLeave() {
    try {
        RemoveSelection();
        CTargetTransferInfo target;
        target.FuncType = k_DragTargetMode_Leave;
        SendToSource_auto(m_DataObject, target);
        ClearState();
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT CDropTarget::Drop(CDataObject *dataObject, DWORD keyState, int x, int y, DWORD *effect,
                          std::optional<CSelectedState> *refreshSelection) {
    if (refreshSelection)
        refreshSelection->reset();
    HRESULT hres = S_OK;
    bool needDrop_by_Source = false;
    DWORD opEffect = DROPEFFECT_NONE;
    try {
        m_DataObject = nullptr;
        if (!m_DropIsAllowed) {
            hres = LoadNames_From_DataObject(dataObject);
            if (hres != S_OK) {
                if (dataObject && dataObject->NativeData)
                    dataObject->NativeData->SendPerformedEffect(DROPEFFECT_NONE);
                ClearState();
                *effect = 0;
                return hres;
            }
        }
        PositionCursor(x, y);
        CPanel::CDisableTimerProcessing2 disableTimerProcessing(m_Panel);
        UInt32 cmd = NDragMenu::k_None;
        UInt32 cmdEffect = DROPEFFECT_NONE;
        bool menu_WasShown = false;
        if (m_IsRightButton && m_Panel) {
            UInt32 flagsMask;
            if (m_Panel->IsArcFolder())
                flagsMask = UInt32(1) << NDragMenu::k_Copy_ToArc;
            else {
                flagsMask = UInt32(1) << NDragMenu::k_AddToArc;
                if (IsFsFolderPath())
                    flagsMask |= UInt32(1) << NDragMenu::k_Copy_Base;
            }
            const UInt32 cmd32 = presentation_ ? presentation_->Drag_OnContextMenu(x, y, flagsMask)
                                               : NDragMenu::k_Cancel;
            cmd = cmd32 & NDragMenu::k_MenuFlags_CmdMask;
            if (cmd32 & NDragMenu::k_MenuFlag_Copy)
                cmdEffect = DROPEFFECT_COPY;
            else if (cmd32 & NDragMenu::k_MenuFlag_Move)
                cmdEffect = DROPEFFECT_MOVE;
            opEffect = cmdEffect;
            menu_WasShown = true;
        } else {
            opEffect = GetEffect(keyState, *effect);
            if (m_IsAppTarget)
                cmd = NDragMenu::k_AddToArc;
            else if (m_Panel) {
                if (IsFsFolderPath()) {
                    const bool is7zip = m_TargetPath_WasSent_ToDataObject;
                    bool createNewArchive = false;
                    if (is7zip)
                        createNewArchive = false;
                    else
                        createNewArchive = true;
                    if (createNewArchive)
                        cmd = NDragMenu::k_AddToArc;
                    else {
                        if (opEffect != 0)
                            cmd = NDragMenu::k_Copy_Base;
                        cmdEffect = opEffect;
                    }
                } else {
                    if (m_Panel->IsArcFolder() || m_Panel->DoesItSupportOperations()) {
                        cmd = NDragMenu::k_Copy_ToArc;
                        if (opEffect != DROPEFFECT_NONE)
                            opEffect = DROPEFFECT_COPY;
                        cmdEffect = opEffect;
                    } else
                        cmd = NDragMenu::k_AddToArc;
                }
            }
        }
        if (cmd == 0)
            cmd = NDragMenu::k_AddToArc;
        if (cmd == NDragMenu::k_AddToArc) {
            opEffect = DROPEFFECT_COPY;
            cmdEffect = DROPEFFECT_COPY;
        }
        if (m_Panel && cmd == NDragMenu::k_Copy_ToArc)
            if (!presentation_ || !presentation_->ConfirmCopyToArchive(*m_Panel, cmdEffect))
                cmd = NDragMenu::k_Cancel;
        CTargetTransferInfo target;
        target.FuncType = k_DragTargetMode_Drop_Begin;
        target.KeyState = keyState;
        target.X = x;
        target.Y = y;
        target.OkEffects = *effect;
        target.Cmd_Effect = cmdEffect;
        target.Cmd_Type = cmd;
        target.FolderType = GetFolderType();
        if (cmd == NDragMenu::k_Cancel)
            target.Flags |= k_TargetFlags_WasCanceled;
        if (menu_WasShown)
            target.Flags |= k_TargetFlags_MenuWasShown;
        SendToSource_auto(dataObject, target);
        UInt32 transferFlags = 0;
        const bool gotTransfer = dataObject && dataObject->ReadTransferFlags(transferFlags);
        if (cmd != NDragMenu::k_Cancel)
            hres = LoadNames_From_DataObject(dataObject);
        if (cmd == NDragMenu::k_Cancel || hres != S_OK) {
            opEffect = DROPEFFECT_NONE;
            cmdEffect = DROPEFFECT_NONE;
        } else {
            if (gotTransfer)
                needDrop_by_Source = (transferFlags & k_SourceFlags_DoNotProcessInTarget) != 0;
            if (!needDrop_by_Source) {
                bool moveMode = cmdEffect == DROPEFFECT_MOVE;
                bool needDrop = false;
                if (m_IsRightButton && m_Panel)
                    needDrop = true;
                if (m_DropIsAllowed && m_PanelDropIsAllowed) {
                    needDrop_by_Source = cmd != NDragMenu::k_AddToArc &&
                                         m_TargetPath_WasSent_ToDataObject &&
                                         m_TargetPath_NonEmpty_WasSent_ToDataObject;
                    needDrop = !needDrop_by_Source;
                }
                if (needDrop && m_Panel) {
                    UString path = GetTargetPath();
                    if (m_IsAppTarget && m_Panel->IsFSFolder())
                        path = m_Panel->GetFsPath();
                    if (menu_WasShown)
                        target.Flags |= k_TargetFlags_MenuWasShown;
                    target.Flags |= k_TargetFlags_WasProcessed;
                    RemoveSelection();
                    hres = m_Panel->CompressDropFiles(
                        m_SourcePaths, path, cmd == NDragMenu::k_AddToArc, moveMode, transferFlags,
                        target.Flags, refreshSelection);
                }
            }
        }
        RemoveSelection();
        target.FuncType = k_DragTargetMode_Drop_End;
        target.Cmd_Type = cmd;
        if (needDrop_by_Source)
            target.Flags |= k_TargetFlags_MustBeProcessedBySource;
        SendToSource_TransferInfo(dataObject, target);
        // Typed result preserves selection on failed/cancelled target operations.
        if (dataObject && dataObject->Panel && hres != S_OK)
            dataObject->Copy_HRESULT = hres;
    } catch (...) {
        hres = E_FAIL;
    }
    if (hres != S_OK)
        opEffect = DROPEFFECT_NONE;
    if (dataObject && dataObject->NativeData)
        dataObject->NativeData->SendPerformedEffect(opEffect == DROPEFFECT_NONE ? DROPEFFECT_NONE
                                                                                : DROPEFFECT_COPY);
    ClearState();
    *effect = opEffect;
    return hres;
}

static bool Is_Path1_Prefixed_by_Path2(const UString &path, const UString &prefix) {
    const unsigned len = prefix.Len();
    if (path.Len() < len)
        return false;
    return CompareFileNames(path.Left(len), prefix) == 0;
}
static bool IsFolderInTemp(const UString &path) {
    FString tempPathF;
    if (!NWindows::NFile::NDir::MyGetTempPath(tempPathF))
        return false;
    const UString tempPath = fs2us(tempPathF);
    if (tempPath.IsEmpty())
        return false;
    return Is_Path1_Prefixed_by_Path2(path, tempPath);
}
static bool AreThereNamesFromTemp(const UStringVector &filePaths) {
    FString tempPathF;
    if (!NWindows::NFile::NDir::MyGetTempPath(tempPathF))
        return false;
    const UString tempPath = fs2us(tempPathF);
    if (tempPath.IsEmpty())
        return false;
    FOR_VECTOR(i, filePaths) if (Is_Path1_Prefixed_by_Path2(filePaths[i], tempPath)) return true;
    return false;
}
HRESULT CPanel::CompressDropFiles(const UStringVector &filePaths, const UString &folderPath,
                                  bool createNewArchive, bool moveMode, UInt32 sourceFlags,
                                  UInt32 &targetFlags,
                                  std::optional<CSelectedState> *refreshSelection) {
    if (filePaths.Size() == 0)
        return S_OK;
    if (createNewArchive) {
        UString folderPath2 = folderPath;
        if (folderPath2.IsEmpty()) {
            FString folderPath2F;
            NWindows::NFile::NDir::GetOnlyDirPrefix(us2fs(filePaths.Front()), folderPath2F);
            folderPath2 = fs2us(folderPath2F);
            if (IsFolderInTemp(folderPath2))
                folderPath2 = ROOT_FS_FOLDER;
        }
        UString arcName_base;
        const UString arcName = CreateArchiveName(filePaths, false, NULL, arcName_base);
        bool needWait;
        if (sourceFlags & k_SourceFlags_WaitFinish)
            needWait = true;
        else if (sourceFlags & k_SourceFlags_DoNotWaitFinish)
            needWait = false;
        else if (sourceFlags & k_SourceFlags_TempFiles)
            needWait = true;
        else
            needWait = AreThereNamesFromTemp(filePaths);
        targetFlags |= needWait ? k_TargetFlags_WaitFinish : k_TargetFlags_DoNotWaitFinish;
        return _dragServices
                   ? _dragServices->CompressFiles(*this, folderPath2, arcName, filePaths, needWait)
                   : E_NOTIMPL;
    } else {
        targetFlags |= k_TargetFlags_WaitFinish;
        if (!folderPath.IsEmpty()) {
            CCopyToOptions options;
            options.moveMode = moveMode;
            options.folder = folderPath;
            options.showErrorMessages = true;
            options.NeedRegistryZone = false;
            options.ZoneIdMode = NExtract::NZoneIdMode::kNone;
            return CopyFsItems(options, filePaths, NULL);
        } else
            return CopyFromNoAsk(moveMode, filePaths, refreshSelection);
    }
}
void CApp::CreateDragTarget() {
    _dropTargetSpec = std::make_unique<CDropTarget>();
    _dropTargetSpec->App = this;
}

void CApp::SetFocusedPanel(unsigned index) {
    LastFocusedPanel = index;
    _dropTargetSpec->TargetPanelIndex = (int)LastFocusedPanel;
}
void CApp::DragBegin(unsigned panelIndex) {
    _dropTargetSpec->TargetPanelIndex = (int)(NumPanels > 1 ? 1 - panelIndex : panelIndex);
    _dropTargetSpec->SrcPanelIndex = (int)panelIndex;
}
void CApp::DragEnd() {
    _dropTargetSpec->TargetPanelIndex = (int)LastFocusedPanel;
    _dropTargetSpec->SrcPanelIndex = -1;
}
