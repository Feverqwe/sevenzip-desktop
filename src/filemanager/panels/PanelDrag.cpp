// PanelDrag.cpp
// Adapted from 7-Zip (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/Common/ArchiveName.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "archive/operations/ExtractingFilePath.h"
#include "dialogs/messages/MessagesDialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "fileops/patterns/Wildcard.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/diagnostics/qtdiagnostictext.h"

void CDataObject::SetTransfer(const CTargetTransferInfo &target) {
    m_Transfer_WasSet = false;
    m_Transfer.Target = target;
    if (target.FuncType != k_DragTargetMode_Leave)
        m_Transfer_WasSet = true;
    bool needProcessBySource = !DestDirPrefix_FromTarget.IsEmpty();
    if (target.FuncType == k_DragTargetMode_Drop_Begin)
        if (target.Cmd_Type != NDragMenu::k_Copy_Base)
            needProcessBySource = false;
    if (target.FuncType == k_DragTargetMode_Drop_End) {
        if (target.Flags & k_TargetFlags_MustBeProcessedBySource)
            needProcessBySource = true;
        else if (target.Flags & k_TargetFlags_WasProcessed)
            needProcessBySource = false;
    }
    DoNotProcessInTarget = needProcessBySource;
}
UInt32 CDataObject::GetTransferFlags() const {
    UInt32 flags = 0;
    if (m_DestDirPrefix_FromTarget_WasSet)
        flags |= k_SourceFlags_SetTargetFolder;
    if (!DestDirPrefix_FromTarget.IsEmpty())
        flags |= k_SourceFlags_SetTargetFolder_NonEmpty;
    if (IsTempFiles) {
        flags |= k_SourceFlags_TempFiles;
        flags |= k_SourceFlags_WaitFinish;
        flags |= k_SourceFlags_NeedExtractOpToFs;
        if (UsePreGlobal)
            flags |= k_SourceFlags_NamesAreParent;
    } else
        flags |= k_SourceFlags_DoNotWaitFinish;
    if (IsRightButton)
        flags |= k_SourceFlags_RightButton;
    else
        flags |= k_SourceFlags_LeftButton;
    if (DoNotProcessInTarget)
        flags |= k_SourceFlags_DoNotProcessInTarget;
    if (Copy_WasCalled)
        flags |= k_SourceFlags_Copy_WasCalled;
    return flags;
}
HRESULT CDataObject::GetData(UStringVector &names) {
    if (NativeData)
        return NativeData->GetNames(names);
    if (NeedCall_Copy && !Copy_WasCalled)
        CopyFromPanelTo_Folder();
    // Unlike an OLE HGLOBAL, Qt callers consume a typed result before exposure.
    RINOK(Copy_HRESULT)
    names = Names;
    if (UsePreGlobal && IsTempFiles) {
        names.Clear();
        names.Add(SrcDirPrefix_Temp);
    }
    return S_OK;
}
bool CDataObject::ReadTransferFlags(UInt32 &flags) const {
    if (NativeData)
        return NativeData->ReadTransferFlags(flags);
    if (!Panel)
        return false;
    flags = GetTransferFlags();
    return true;
}

bool CDataObject::BeginExternalReceiver() {
    if (IsTempFiles && (!OwnedTempLease || !OwnedTempLease->ProtectExternal()))
        return false;
    ExternalReceiverUnconfirmed = IsTempFiles;
    return true;
}
void CDataObject::FinishExternalReceiver() {
    if (!OwnedTempLease || OwnedTempLease->ExternalFinished())
        ExternalReceiverUnconfirmed = false;
}

void CDataObject::CompleteExternalDrag(bool cancellationConfirmed) {
    if (cancellationConfirmed ||
        (m_Transfer_WasSet && m_Transfer.Target.FuncType == k_DragTargetMode_Drop_End &&
         (m_Transfer.Target.Flags & (k_TargetFlags_WasCanceled | k_TargetFlags_WasProcessed |
                                     k_TargetFlags_MustBeProcessedBySource))))
        FinishExternalReceiver();
}

void CDataObject::CopyFromPanelTo_Folder() {
    if (!Panel) {
        Copy_HRESULT = E_ABORT;
        return;
    }
    try {
        CCopyToOptions options;
        options.folder = SrcDirPrefix_Temp;
        // Qt owns mouse capture; CopyTo uses the existing progress adapter.
        Copy_WasCalled = true;
        Copy_HRESULT = E_FAIL;
        Copy_HRESULT = Panel->CopyTo(options, Indices, &Messages);
    } catch (...) {
        Copy_HRESULT = E_FAIL;
    }
}

HRESULT CDropSource::QueryContinueDrag(BOOL escapePressed, DWORD keyState) {
    if (escapePressed) {
        DragProcessing_WasFinished = true;
        DragProcessing_HRESULT = DRAGDROP_S_CANCEL;
        return DRAGDROP_S_CANCEL;
    }
    if (DragProcessing_WasFinished)
        return DragProcessing_HRESULT;
    if ((keyState & MK_RBUTTON) != 0) {
        if (!DataObjectSpec->IsRightButton) {
            DragProcessing_WasFinished = true;
            DragProcessing_HRESULT = DRAGDROP_S_CANCEL;
            return DRAGDROP_S_CANCEL;
        }
        return S_OK;
    }
    if ((keyState & MK_LBUTTON) != 0) {
        if (DataObjectSpec->IsRightButton) {
            DragProcessing_WasFinished = true;
            DragProcessing_HRESULT = DRAGDROP_S_CANCEL;
            return DRAGDROP_S_CANCEL;
        }
        return S_OK;
    }
    {
        if (m_Effect == DROPEFFECT_NONE) {
            DragProcessing_WasFinished = true;
            DragProcessing_HRESULT = DRAGDROP_S_CANCEL;
            return DRAGDROP_S_CANCEL;
        }
        DataObjectSpec->UsePreGlobal = false;
        DataObjectSpec->Copy_HRESULT = S_OK;
        if (DataObjectSpec->IsTempFiles) {
            if (!DataObjectSpec->DestDirPrefix_FromTarget.IsEmpty())
                DataObjectSpec->DoNotProcessInTarget = true;
            else
                DataObjectSpec->NeedCall_Copy = true;
        }
        DragProcessing_HRESULT = DRAGDROP_S_DROP;
        DragProcessing_WasFinished = true;
        return DRAGDROP_S_DROP;
    }
}
HRESULT CDropSource::GiveFeedback(DWORD effect) {
    m_Effect = effect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

void CPanel::OnDrag(bool isRightButton) {
    if (!DoesItSupportOperations() || !_dragServices || !_panelCallback)
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    if (indices.Size() == 0)
        return;
    const bool isFSFolder = IsFSFolder();
    if (!isFSFolder && isRightButton)
        return;
    UString dirPrefix;
    NWindows::NFile::NDir::CTempDir tempDirectory;
    const auto dataObject = std::make_shared<CDataObject>();
    // Retain the original heap-owned IDataObject lifetime. A receiver may hold
    // its names/lease after this frame; panel callbacks end when the frame ends.
    struct CDetachPanel {
        CDataObject &Data;
        ~CDetachPanel() { Data.Panel = nullptr; }
    } detachPanel{*dataObject};
    CDataObject *dataObjectSpec = dataObject.get();
    dataObjectSpec->IsRightButton = isRightButton;
    if (isFSFolder) {
        dirPrefix = GetFsPath();
        dataObjectSpec->UsePreGlobal = false;
    } else {
        if (!tempDirectory.Create(FTEXT("7zE"))) {
            MessageBox_Error(QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Can't create temp folder")));
            return;
        }
        dirPrefix = fs2us(tempDirectory.GetPath());
        dataObjectSpec->IsTempFiles = true;
        dataObjectSpec->UsePreGlobal = true;
        dataObjectSpec->OwnedTempLease = tempDirectory.OwnedLease();
        NWindows::NFile::NName::NormalizeDirPathPrefix(dirPrefix);
    }
    {
        UStringVector names;
        FOR_VECTOR(i, indices) {
            const UInt32 index = indices[i];
            UString s;
            if (isFSFolder)
                s = GetItemRelPath(index);
            else {
                s = GetItemName(index);
                s = Get_Correct_FsFile_Name(s);
            }
            names.Add(dirPrefix + s);
        }
        // Qt MIME owns names rather than the Win32 CF_HDROP HGLOBAL transport.
        dataObjectSpec->Names = names;
    }
    CDropSource dropSource;
    CDropSource *dropSourceSpec = &dropSource;
    dataObjectSpec->Panel = this;
    dataObjectSpec->Indices = indices;
    dataObjectSpec->SrcDirPrefix_Temp = dirPrefix;
    dropSourceSpec->DataObjectSpec = dataObjectSpec;
    const bool moveIsAllowed = isFSFolder;
    _panelCallback->DragBegin();
    DWORD effect = 0;
    const DWORD effectsOK = DROPEFFECT_MOVE | DROPEFFECT_COPY;
    HRESULT res = _dragServices->DoDragDrop(*dataObject, dropSource, effectsOK, effect);
    _panelCallback->DragEnd();
    // Upstream notes recipients may use bytes after DoDragDrop. Preserve the
    // owned root when its platform receiver cannot positively confirm completion.
    if (dataObjectSpec->ExternalReceiverUnconfirmed)
        tempDirectory.DisableDeleting();
    // Native OLE can return DROP after a recipient's Cancel menu command.
    // Retain the Qt port's existing selection-on-cancel contract using the
    // original target's explicit cancellation flag.
    const bool canceled =
        (res == DRAGDROP_S_CANCEL) ||
        (dataObjectSpec->m_Transfer_WasSet &&
         dataObjectSpec->m_Transfer.Target.FuncType == k_DragTargetMode_Drop_End &&
         (dataObjectSpec->m_Transfer.Target.Flags & k_TargetFlags_WasCanceled));
    CDisableNotify disableNotify(*this);
    if (res == DRAGDROP_S_DROP) {
        res = dataObjectSpec->Copy_HRESULT;
        bool need_Process = dataObjectSpec->DoNotProcessInTarget;
        if (dataObjectSpec->m_Transfer_WasSet)
            if (dataObjectSpec->m_Transfer.Target.FuncType == k_DragTargetMode_Drop_End)
                if (dataObjectSpec->m_Transfer.Target.Flags & k_TargetFlags_MustBeProcessedBySource)
                    need_Process = true;
        if (need_Process)
            if (!dataObjectSpec->DestDirPrefix_FromTarget.IsEmpty()) {
#ifdef _WIN32
                if (!NWindows::NFile::NName::IsAltStreamPrefixWithColon(
                        dataObjectSpec->DestDirPrefix_FromTarget))
#endif
                    NWindows::NFile::NName::NormalizeDirPathPrefix(
                        dataObjectSpec->DestDirPrefix_FromTarget);
                CCopyToOptions options;
                options.folder = dataObjectSpec->DestDirPrefix_FromTarget;
                options.moveMode = moveIsAllowed;
                if (moveIsAllowed) {
                    if (dataObjectSpec->m_Transfer_WasSet)
                        options.moveMode =
                            dataObjectSpec->m_Transfer.Target.Cmd_Effect == DROPEFFECT_MOVE;
                    else
                        options.moveMode = effect == DROPEFFECT_MOVE;
                }
                res = CopyTo(options, indices, &dataObjectSpec->Messages);
            }
    } else {
        if (res != DRAGDROP_S_CANCEL && res != S_OK && res != HRESULT(0x8000FFFFL))
            MessageBox_Error_HRESULT(res);
        res = dataObjectSpec->Copy_HRESULT;
    }
    if (!dataObjectSpec->Messages.IsEmpty()) {
        CMessagesDialog messagesDialog;
        messagesDialog.Messages = &dataObjectSpec->Messages;
        messagesDialog.Create(GetParent());
    }
    if (res != S_OK && res != E_ABORT) {
        disableNotify.Restore();
        MessageBox_Error_HRESULT(res);
    }
    if (res == S_OK && dataObjectSpec->Messages.IsEmpty() && !canceled)
        KillSelection();
}
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

#ifdef _WIN32
#include "7zip/UI/FileManager/EnumFormatEtc.h"
#include "7zip/UI/FileManager/MyCom2.h"
#include "C/7zVersion.h"
#include "Windows/COM.h"
#include "Windows/MemoryGlobal.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "platform/cpu/CpuArch.h"
#include "platform/shell/Shell.h"
#include <cstring>
namespace NPanelDragNative {
using namespace NWindows;
static LPCTSTR const k_Format_7zip_SetTargetFolder = TEXT("7-Zip::SetTargetFolder");
static LPCTSTR const k_Format_7zip_SetTransfer = TEXT("7-Zip::SetTransfer");
static LPCTSTR const k_Format_7zip_GetTransfer = TEXT("7-Zip::GetTransfer");
static const UInt32 k_Struct_Id_SetTranfer = 2; // it's our selected id
static const UInt32 k_Struct_Id_GetTranfer = 3; // it's our selected id

static const UInt64 k_Program_Id = 1; // "7-Zip"

enum E_Program_ISA {
    k_Program_ISA_x86 = 2,
    k_Program_ISA_x64 = 3,
    k_Program_ISA_armt = 4,
    k_Program_ISA_arm64 = 5,
    k_Program_ISA_arm32 = 6,
    k_Program_ISA_ia64 = 9
};

#define k_Program_Ver ((MY_VER_MAJOR << 16) | MY_VER_MINOR)

struct CDataObject_TransferBase {
    UInt32 Struct_Id;
    UInt32 Struct_Size;

    UInt64 Program_Id;
    UInt32 Program_Ver_Main;
    UInt32 Program_Ver_Build;
    UInt32 Program_ISA;
    UInt32 Program_Flags;

    UInt32 ProcessId;
    UInt32 _reserved1[7];

  protected:
    void Init_Program();
};

void CDataObject_TransferBase::Init_Program() {
    Program_Id = k_Program_Id;
    Program_ISA =
#if defined(MY_CPU_AMD64)
        k_Program_ISA_x64
#elif defined(MY_CPU_X86)
        k_Program_ISA_x86
#elif defined(MY_CPU_ARM64)
        k_Program_ISA_arm64
#elif defined(MY_CPU_ARM32)
        k_Program_ISA_arm32
#elif defined(MY_CPU_ARMT) || defined(MY_CPU_ARM)
        k_Program_ISA_armt
#elif defined(MY_CPU_IA64)
        k_Program_ISA_ia64
#else
        0
#endif
        ;
    Program_Flags = sizeof(size_t);
    Program_Ver_Main = k_Program_Ver;
    // Program_Ver_Build = 0;
    ProcessId = GetCurrentProcessId();
}

#if defined(__GNUC__) && !defined(__clang__)
/* 'void* memset(void*, int, size_t)' clearing an object
    of non-trivial type 'struct CDataObject_SetTransfer' */
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

struct CDataObject_GetTransfer : public CDataObject_TransferBase {
    UInt32 Flags;

    UInt32 _reserved2[11];

    CDataObject_GetTransfer() {
        memset(this, 0, sizeof(*this));
        Init_Program();
        Struct_Id = k_Struct_Id_GetTranfer;
        Struct_Size = sizeof(*this);
    }

    bool Check() const {
        return Struct_Size >= sizeof(*this) && Struct_Id == k_Struct_Id_GetTranfer;
    }
};

struct CTargetTransferInfo {
    UInt32 Flags;
    UInt32 FuncType;

    UInt32 KeyState;
    UInt32 OkEffects;
    POINTL Point;

    UInt32 Cmd_Effect;
    UInt32 Cmd_Type;
    UInt32 FolderType;
    UInt32 _reserved3[3];

    CTargetTransferInfo() { memset(this, 0, sizeof(*this)); }
};

struct CDataObject_SetTransfer : public CDataObject_TransferBase {
    CTargetTransferInfo Target;

    void Init() {
        memset(this, 0, sizeof(*this));
        Init_Program();
        Struct_Id = k_Struct_Id_SetTranfer;
        Struct_Size = sizeof(*this);
    }

    bool Check() const {
        return Struct_Size >= sizeof(*this) && Struct_Id == k_Struct_Id_SetTranfer;
    }
};

static ::CTargetTransferInfo ToTyped(const CTargetTransferInfo &s) {
    ::CTargetTransferInfo d;
    d.Flags = s.Flags;
    d.FuncType = s.FuncType;
    d.KeyState = s.KeyState;
    d.OkEffects = s.OkEffects;
    d.X = s.Point.x;
    d.Y = s.Point.y;
    d.Cmd_Effect = s.Cmd_Effect;
    d.Cmd_Type = s.Cmd_Type;
    d.FolderType = s.FolderType;
    return d;
}
static CTargetTransferInfo ToNative(const ::CTargetTransferInfo &s) {
    CTargetTransferInfo d;
    d.Flags = s.Flags;
    d.FuncType = s.FuncType;
    d.KeyState = s.KeyState;
    d.OkEffects = s.OkEffects;
    d.Point.x = s.X;
    d.Point.y = s.Y;
    d.Cmd_Effect = s.Cmd_Effect;
    d.Cmd_Type = s.Cmd_Type;
    d.FolderType = s.FolderType;
    return d;
}
static_assert(sizeof(CDataObject_TransferBase) == 64);
static_assert(sizeof(CDataObject_GetTransfer) == 112);
static_assert(sizeof(CDataObject_SetTransfer) == 112);
static bool CopyNamesToHGlobal(NMemory::CGlobal &hgDrop, const UStringVector &names) {
    size_t totalLen = 1;

    {
        unsigned i;
        for (i = 0; i < names.Size(); i++)
            totalLen += names[i].Len() + 1;

        if (!hgDrop.Alloc(GHND | GMEM_SHARE, totalLen * sizeof(WCHAR) + sizeof(DROPFILES)))
            return false;

        NMemory::CGlobalLock dropLock(hgDrop);
        DROPFILES *dropFiles = (DROPFILES *)dropLock.GetPointer();
        if (!dropFiles)
            return false;
        /* fNC:
            TRUE  : pt specifies the screen coordinates of a point in a window's nonclient area.
            FALSE : pt specifies the client coordinates of a point in the client area.
        */
        dropFiles->fNC = FALSE;
        dropFiles->pt.x = 0;
        dropFiles->pt.y = 0;
        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;
        WCHAR *p = (WCHAR *)(void *)((BYTE *)dropFiles + sizeof(DROPFILES));
        for (i = 0; i < names.Size(); i++) {
            const UString &s = names[i];
            const unsigned fullLen = s.Len() + 1;
            MyStringCopy(p, (const WCHAR *)s);
            p += fullLen;
            totalLen -= fullLen;
        }
        *p = 0;
    }
    // if (totalLen != 1) return false;
    return true;
}
class CDataObject Z7_final : public IDataObject, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1_MT(IDataObject)

    Z7_COMWF_B GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM medium) Z7_override;
    Z7_COMWF_B GetDataHere(LPFORMATETC pformatetc, LPSTGMEDIUM medium) Z7_override;
    Z7_COMWF_B QueryGetData(LPFORMATETC pformatetc) Z7_override;

    Z7_COMWF_B GetCanonicalFormatEtc(LPFORMATETC /* pformatetc */,
                                     LPFORMATETC pformatetcOut) Z7_override {
        if (!pformatetcOut)
            return E_INVALIDARG;
        pformatetcOut->ptd = NULL;
        return E_NOTIMPL;
    }

    Z7_COMWF_B SetData(LPFORMATETC etc, STGMEDIUM *medium, BOOL release) Z7_override;
    Z7_COMWF_B EnumFormatEtc(DWORD drection, LPENUMFORMATETC *enumFormatEtc) Z7_override;

    Z7_COMWF_B DAdvise(FORMATETC * /* etc */, DWORD /* advf */, LPADVISESINK /* pAdvSink */,
                       DWORD * /* pdwConnection */) Z7_override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    Z7_COMWF_B DUnadvise(DWORD /* dwConnection */) Z7_override { return OLE_E_ADVISENOTSUPPORTED; }
    Z7_COMWF_B EnumDAdvise(LPENUMSTATDATA *ppenumAdvise) Z7_override {
        if (ppenumAdvise)
            *ppenumAdvise = NULL;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    bool m_PerformedDropEffect_WasSet;
    bool m_LogicalPerformedDropEffect_WasSet;

  private:
    // GetData formats (source to target):
    FORMATETC m_Etc;
    // UINT m_Format_FileOpFlags;
    // UINT m_Format_PreferredDropEffect;

    // SetData() formats (target to source):
    // 7-Zip's format:
    UINT m_Format_7zip_SetTargetFolder;
    UINT m_Format_7zip_SetTransfer;
    UINT m_Format_7zip_GetTransfer; // for GetData()

    UINT m_Format_PerformedDropEffect;
    UINT m_Format_LogicalPerformedDropEffect;
    UINT m_Format_DisableDragText;
    UINT m_Format_IsShowingLayered;
    UINT m_Format_IsShowingText;
    UINT m_Format_DropDescription;
    UINT m_Format_TargetCLSID;

    DWORD m_PerformedDropEffect;
    DWORD m_LogicalPerformedDropEffect;

    NMemory::CGlobal m_hGlobal_HDROP_Pre, m_hGlobal_HDROP_Final;
    std::shared_ptr<::CDataObject> model_;
    HRESULT SetData2(const FORMATETC *, const STGMEDIUM *);

  public:
    explicit CDataObject(std::shared_ptr<::CDataObject> model);
    HRESULT InitNames();
};
HRESULT CDataObject::InitNames() {
    if (!CopyNamesToHGlobal(m_hGlobal_HDROP_Final, model_->Names))
        return E_OUTOFMEMORY;
    if (model_->IsTempFiles) {
        UStringVector names;
        names.Add(model_->SrcDirPrefix_Temp);
        if (!CopyNamesToHGlobal(m_hGlobal_HDROP_Pre, names))
            return E_OUTOFMEMORY;
    }
    return S_OK;
}
CDataObject::CDataObject(std::shared_ptr<::CDataObject> model) : model_(std::move(model)) {
    // GetData formats (source to target):
    // and we use CF_HDROP format to transfer file paths from source to target:
    m_Etc.cfFormat = CF_HDROP;
    m_Etc.ptd = NULL;
    m_Etc.dwAspect = DVASPECT_CONTENT;
    m_Etc.lindex = -1;
    m_Etc.tymed = TYMED_HGLOBAL;

    // m_Format_FileOpFlags          = RegisterClipboardFormat(TEXT("FileOpFlags"));
    // m_Format_PreferredDropEffect  = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT); //
    // "Preferred DropEffect"

    // SetData() formats (target to source):
    m_Format_7zip_SetTargetFolder = RegisterClipboardFormat(k_Format_7zip_SetTargetFolder);
    m_Format_7zip_SetTransfer = RegisterClipboardFormat(k_Format_7zip_SetTransfer);
    m_Format_7zip_GetTransfer = RegisterClipboardFormat(k_Format_7zip_GetTransfer);

    m_Format_PerformedDropEffect =
        RegisterClipboardFormat(CFSTR_PERFORMEDDROPEFFECT); // "Performed DropEffect"
    m_Format_LogicalPerformedDropEffect =
        RegisterClipboardFormat(CFSTR_LOGICALPERFORMEDDROPEFFECT); // "Logical Performed DropEffect"
    m_Format_DisableDragText = RegisterClipboardFormat(TEXT("DisableDragText"));
    m_Format_IsShowingLayered = RegisterClipboardFormat(TEXT("IsShowingLayered"));
    m_Format_IsShowingText = RegisterClipboardFormat(TEXT("IsShowingText"));
    m_Format_DropDescription = RegisterClipboardFormat(TEXT("DropDescription"));
    m_Format_TargetCLSID = RegisterClipboardFormat(CFSTR_TARGETCLSID);

    m_PerformedDropEffect = 0;
    m_LogicalPerformedDropEffect = 0;

    m_PerformedDropEffect_WasSet = false;
    m_LogicalPerformedDropEffect_WasSet = false;
}
Z7_COMWF_B CDataObject::SetData(LPFORMATETC etc, STGMEDIUM *medium, BOOL release) {
    try {
        const HRESULT hres = SetData2(etc, medium);
        // PrintFormat(release ? "SetData RELEASE=TRUE" : "SetData RELEASE=FALSE" , etc->cfFormat);
        if (release) {
            /*
            const DWORD tymed = medium->tymed;
            IUnknown *pUnkForRelease = medium->pUnkForRelease;
            */
            // medium->tymed = NULL; // for debug
            // return E_NOTIMPL;  // for debug
            ReleaseStgMedium(medium);
            /* ReleaseStgMedium() will change STGMEDIUM::tymed to (TYMED_NULL = 0).
               but we also can clear (medium.hGlobal = NULL),
               to prevent some incorrect releasing, if the caller will try to release the data  */
            /*
            if (medium->tymed == TYMED_NULL && tymed == TYMED_HGLOBAL && !pUnkForRelease)
              medium->hGlobal = NULL;
            */
            // do we need return S_OK; for (tymed != TYMED_HGLOBAL) cases ?
            /* we return S_OK here to shows that we take ownership of the data in (medium),
               so the caller will not try to release (medium) */
            return S_OK; // to be more compatible with Win10-Explorer and DOCs.
        }
        return hres;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT CDataObject::SetData2(const FORMATETC *etc, const STGMEDIUM *medium) {
    // PRF3("== CDataObject::SetData()");

    HRESULT hres = S_OK;

    if (etc->cfFormat == 0)
        return DV_E_FORMATETC;
    if (etc->tymed != TYMED_HGLOBAL)
        return E_NOTIMPL; // DV_E_TYMED;
    if (etc->dwAspect != DVASPECT_CONTENT)
        return E_NOTIMPL; // DV_E_DVASPECT;
    if (medium->tymed != TYMED_HGLOBAL)
        return E_NOTIMPL; // DV_E_TYMED;

    if (!medium->hGlobal)
        return S_OK;

    if (etc->cfFormat == m_Format_7zip_SetTargetFolder) {
        model_->DestDirPrefix_FromTarget.Empty();
        model_->m_DestDirPrefix_FromTarget_WasSet = true;
    } else if (etc->cfFormat == m_Format_7zip_SetTransfer)
        model_->m_Transfer_WasSet = false;

    const size_t size = GlobalSize(medium->hGlobal);
    // GlobalLock() can return NULL, if memory block has a zero size
    if (size == 0)
        return S_OK;
    NWindows::NMemory::CGlobalLock lock(medium->hGlobal);
    const void *src = lock.GetPointer();
    if (!src)
        return E_FAIL;

    if (etc->cfFormat == m_Format_7zip_SetTargetFolder) {
        /* this is our registered k_Format_7zip_SetTargetFolder format.
           so it's call from 7-zip's CDropTarget */
        /* 7-zip's CDropTarget calls SetData() for m_Format_7zip_SetTargetFolder
           with (release == FALSE) */
        const size_t num = size / sizeof(wchar_t);
        if (size != num * sizeof(wchar_t))
            return E_FAIL;
        // if (num == 0) return S_OK;
        // GlobalLock() can return NULL, if memory block has a zero-byte size
        const wchar_t *s = (const wchar_t *)src;
        UString &dest = model_->DestDirPrefix_FromTarget;
        for (size_t i = 0; i < num; i++) {
            const wchar_t c = s[i];
            if (c == 0)
                break;
            dest += c;
        }
        // PRF_(PrintFormat_AndData("SetData", etc->cfFormat, src, size))

    } else if (etc->cfFormat == m_Format_7zip_SetTransfer) {
        /* 7-zip's CDropTarget calls SetData() for m_Format_7zip_SetTransfer
           with (release == FALSE) */
        if (size < sizeof(CDataObject_SetTransfer))
            return E_FAIL;
        const CDataObject_SetTransfer *t = (const CDataObject_SetTransfer *)src;
        if (!t->Check())
            return E_FAIL;
        model_->SetTransfer(ToTyped(t->Target));
    } else {
        // SetData() from Explorer Target:
        if (etc->cfFormat == m_Format_PerformedDropEffect) {
            m_PerformedDropEffect_WasSet = false;
            if (size == sizeof(DWORD)) {
                m_PerformedDropEffect = *(const DWORD *)src;
                m_PerformedDropEffect_WasSet = true;
            }
        } else if (etc->cfFormat == m_Format_LogicalPerformedDropEffect) {
            m_LogicalPerformedDropEffect_WasSet = false;
            if (size == sizeof(DWORD)) {
                m_LogicalPerformedDropEffect = *(const DWORD *)src;
                m_LogicalPerformedDropEffect_WasSet = true;
            }
        } else if (etc->cfFormat == m_Format_DropDescription) {
            // drop description contains only name of dest folder without full path

        } else if (etc->cfFormat == m_Format_TargetCLSID) {
            // it's called after call QueryContinueDrag() (keyState & MK_LBUTTON) == 0
            // Shell File System Folder (explorer) guid: F3364BA0-65B9-11CE-A9BA-00AA004AE837

        } else if (etc->cfFormat == m_Format_DisableDragText) {
            // (size == 4) (UInt32 value)
            //    value==0 : if drag to folder item or folder
            //    value==1 : if drag to file or non list_view */
        } else if (etc->cfFormat == m_Format_IsShowingLayered ||
                   etc->cfFormat == m_Format_IsShowingText) {
            // (size == 4) (UInt32 value) value==0 :
        } else
            hres = DV_E_FORMATETC;
        // hres = E_NOTIMPL; // for debug
        // hres = DV_E_FORMATETC; // for debug
    }

    return hres;
}

static HGLOBAL DuplicateGlobalMem(HGLOBAL srcGlobal) {
    /* GlobalSize() returns 0: If the specified handle
       is not valid or if the object has been discarded */
    const SIZE_T size = GlobalSize(srcGlobal);
    if (size == 0)
        return NULL;
    // GlobalLock() can return NULL, if memory block has a zero-byte size
    const void *src = GlobalLock(srcGlobal);
    if (!src)
        return NULL;
    HGLOBAL destGlobal = GlobalAlloc(GHND | GMEM_SHARE, size);
    if (destGlobal) {
        void *dest = GlobalLock(destGlobal);
        if (!dest) {
            GlobalFree(destGlobal);
            destGlobal = NULL;
        } else {
            memcpy(dest, src, size);
            GlobalUnlock(destGlobal);
        }
    }
    GlobalUnlock(srcGlobal);
    return destGlobal;
}

static bool Medium_CopyFrom(LPSTGMEDIUM medium, const void *data, size_t size) {
    medium->tymed = TYMED_NULL;
    medium->pUnkForRelease = NULL;
    medium->hGlobal = NULL;
    const HGLOBAL global = GlobalAlloc(GHND | GMEM_SHARE, size);
    if (!global)
        return false;
    void *dest = GlobalLock(global);
    if (!dest) {
        GlobalFree(global);
        return false;
    }
    memcpy(dest, data, size);
    GlobalUnlock(global);
    medium->hGlobal = global;
    medium->tymed = TYMED_HGLOBAL;
    return true;
}

Z7_COMWF_B CDataObject::GetData(LPFORMATETC etc, LPSTGMEDIUM medium) {
    try {

        medium->tymed = TYMED_NULL;
        medium->pUnkForRelease = NULL;
        medium->hGlobal = NULL;

        if (model_->NeedCall_Copy && !model_->Copy_WasCalled) {
            // Original CopyFromPanelTo_Folder restores OLE capture before a
            // password/progress modal. Qt supplies the source window handle.
            if (model_->Panel)
                if (QWidget *window = model_->Panel->GetParent())
                    ::SetCapture(reinterpret_cast<HWND>(window->winId()));
            model_->CopyFromPanelTo_Folder();
        }

        // PRF3("+ CDataObject::GetData");
        // PrintFormat(etc->cfFormat);
        HGLOBAL global;
        RINOK(QueryGetData(etc))
        if (etc->cfFormat == CF_HDROP) {
            RINOK(model_->Copy_HRESULT)
        }

        /*
        if (etc->cfFormat == m_Format_FileOpFlags)
          global = m_hGlobal_FileOpFlags;
        else if (etc->cfFormat == m_Format_PreferredDropEffect)
        {
          // Explorer requests PreferredDropEffect only if Move/Copy selection is possible:
          //   Shift is not pressed and Ctrl is not pressed

          global = m_hGlobal_PreferredDropEffect;
        }
        else
        */
        if (etc->cfFormat == m_Etc.cfFormat) // CF_HDROP
            global = model_->UsePreGlobal ? m_hGlobal_HDROP_Pre : m_hGlobal_HDROP_Final;
        else if (etc->cfFormat == m_Format_7zip_GetTransfer) {
            CDataObject_GetTransfer transfer;
            transfer.Flags = model_->GetTransferFlags();

            if (Medium_CopyFrom(medium, &transfer, sizeof(transfer)))
                return S_OK;
            return E_OUTOFMEMORY;
        } else
            return DV_E_FORMATETC;

        if (!global)
            return DV_E_FORMATETC;
        medium->tymed = m_Etc.tymed;
        medium->hGlobal = DuplicateGlobalMem(global);
        if (!medium->hGlobal)
            return E_OUTOFMEMORY;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

Z7_COMWF_B CDataObject::GetDataHere(LPFORMATETC /* etc */, LPSTGMEDIUM /* medium */) {

    // Seems Windows doesn't call it, so we will not implement it.
    return E_UNEXPECTED;
}

/*
  IDataObject::QueryGetData() Determines whether the data object is capable of
  rendering the data as specified. Objects attempting a paste or drop
  operation can call this method before calling IDataObject::GetData
  to get an indication of whether the operation may be successful.

  The client of a data object calls QueryGetData to determine whether
  passing the specified FORMATETC structure to a subsequent call to
  IDataObject::GetData is likely to be successful.

  we check Try_QueryGetData with CF_HDROP
*/

Z7_COMWF_B CDataObject::QueryGetData(LPFORMATETC etc) {

    if (etc->cfFormat == m_Etc.cfFormat // CF_HDROP
        || etc->cfFormat == m_Format_7zip_GetTransfer
        // || (etc->cfFormat == m_Format_FileOpFlags && (HGLOBAL)m_hGlobal_FileOpFlags)
        // || (etc->cfFormat == m_Format_PreferredDropEffect &&
        // (HGLOBAL)m_hGlobal_PreferredDropEffect)
    ) {
    } else
        return DV_E_FORMATETC;
    if (etc->dwAspect != m_Etc.dwAspect)
        return DV_E_DVASPECT;
    /* GetData(): It is possible to specify more than one medium by using the Boolean OR
       operator, allowing the method to choose the best medium among those specified. */
    if ((etc->tymed & m_Etc.tymed) == 0)
        return DV_E_TYMED;
    return S_OK;
}

Z7_COMWF_B CDataObject::EnumFormatEtc(DWORD direction, LPENUMFORMATETC FAR *enumFormatEtc) {
    // we don't enumerate for DATADIR_SET. Seems it can work without it.
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    // we don't enumerate for m_Format_FileOpFlags also. Seems it can work without it.
    return CreateEnumFormatEtc(1, &m_Etc, enumFormatEtc);
}

#define INIT_FORMATETC_HGLOBAL(type) {(type), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}

static bool DataObject_GetData_GetTransfer(IDataObject *dataObject, UINT a_Format_7zip_GetTransfer,
                                           CDataObject_GetTransfer &transfer) {
    FORMATETC etc = INIT_FORMATETC_HGLOBAL((CLIPFORMAT)a_Format_7zip_GetTransfer);
    NCOM::CStgMedium medium;
    const HRESULT res = dataObject->GetData(&etc, &medium);
    if (res != S_OK)
        return false;
    if (medium.tymed != TYMED_HGLOBAL)
        return false;
    const size_t size = GlobalSize(medium.hGlobal);
    if (size < sizeof(transfer))
        return false;
    NMemory::CGlobalLock dropLock(medium.hGlobal);
    const CDataObject_GetTransfer *t = (const CDataObject_GetTransfer *)dropLock.GetPointer();
    if (!t)
        return false;
    if (!t->Check()) // isSetData
        return false;
    transfer = *t;
    return true;
}

class CNativeDragData final : public IQtNativeDragData {
    CMyComPtr<IDataObject> data_;
    bool Send(UINT format, const void *bytes, size_t size, BOOL release) {
        if (!format)
            return false;
        FORMATETC etc = {(CLIPFORMAT)format, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium;
        if (!Medium_CopyFrom(&medium, bytes, size))
            return false;
        const HRESULT hres = data_->SetData(&etc, &medium, release);
        // SetTarget/SetTransfer use FALSE; Explorer performed-effect requires TRUE.
        // On error, ownership did not transfer. A successful TRUE call may leave
        // a stale handle, so never release that successful medium a second time.
        if (!release || hres != S_OK)
            ReleaseStgMedium(&medium);
        return hres == S_OK;
    }

  public:
    explicit CNativeDragData(IDataObject *data) : data_(data) {}
    HRESULT GetNames(UStringVector &names) override {
        return NShell::DataObject_GetData_HDROP_or_IDLIST_Names(data_, names);
    }
    bool SetTargetFolder(const UString &path) override {
        return Send(RegisterClipboardFormat(k_Format_7zip_SetTargetFolder), path.Ptr(),
                    (path.Len() + 1) * sizeof(wchar_t), FALSE);
    }
    bool SetTransfer(const ::CTargetTransferInfo &info) override {
        CDataObject_SetTransfer transfer;
        transfer.Init();
        transfer.Target = ToNative(info);
        return Send(RegisterClipboardFormat(k_Format_7zip_SetTransfer), &transfer, sizeof(transfer),
                    FALSE);
    }
    bool ReadTransferFlags(UInt32 &flags) override {
        CDataObject_GetTransfer transfer;
        if (!DataObject_GetData_GetTransfer(
                data_, RegisterClipboardFormat(k_Format_7zip_GetTransfer), transfer))
            return false;
        flags = transfer.Flags;
        return true;
    }
    void SendPerformedEffect(DWORD effect) override {
        Send(RegisterClipboardFormat(CFSTR_PERFORMEDDROPEFFECT), &effect, sizeof(effect), TRUE);
    }
};
class CDropSource final : public IDropSource, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1_MT(IDropSource)
    ::CDropSource &source_;

  public:
    explicit CDropSource(::CDropSource &source) : source_(source) {}
    STDMETHOD(QueryContinueDrag)(BOOL escapePressed, DWORD keyState) override {
        return source_.QueryContinueDrag(escapePressed, keyState);
    }
    STDMETHOD(GiveFeedback)(DWORD effect) override { return source_.GiveFeedback(effect); }
};
} // namespace NPanelDragNative

HRESULT CreatePanelDragDataObject(const std::shared_ptr<CDataObject> &model, IDataObject **out) {
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!model)
        return E_INVALIDARG;
    auto *native = new NPanelDragNative::CDataObject(model);
    CMyComPtr<IDataObject> data = native;
    RINOK(native->InitNames())
    *out = data.Detach();
    return S_OK;
}
std::shared_ptr<IQtNativeDragData> ReadPanelDragDataObject(IDataObject *data) {
    return data ? std::make_shared<NPanelDragNative::CNativeDragData>(data) : nullptr;
}
HRESULT DoNativePanelDrag(CDataObject &model, CDropSource &source, DWORD allowed, DWORD &effect) {
    const auto owner = model.weak_from_this().lock();
    if (!owner)
        return E_INVALIDARG;
    CMyComPtr<IDataObject> data;
    RINOK(CreatePanelDragDataObject(owner, &data))
    if (!model.BeginExternalReceiver()) {
        model.Copy_HRESULT = E_FAIL;
        return DRAGDROP_S_CANCEL;
    }
    CMyComPtr<IDropSource> dropSource = new NPanelDragNative::CDropSource(source);
    const HRESULT result = ::DoDragDrop(data, dropSource, allowed, &effect);
    model.CompleteExternalDrag(result == DRAGDROP_S_CANCEL);
    return result;
}
#endif
