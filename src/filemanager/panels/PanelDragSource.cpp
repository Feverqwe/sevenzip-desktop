// Source-side drag preparation and completion adapted from 7-Zip PanelDrag.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "archive/operations/ExtractingFilePath.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/ProgressDialog2.h"

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
    if (DeferredTempFiles && target.FuncType == k_DragTargetMode_Drop_Begin &&
        !(target.Flags & k_TargetFlags_WasCanceled))
        NeedCall_Copy = !needProcessBySource;
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

void CDataObject::CopyFromPanelTo_Folder(CPanel *source) {
    if (!source)
        source = Panel;
    if (!source) {
        Copy_HRESULT = E_ABORT;
        return;
    }
    try {
        CCopyToOptions options;
        options.folder = SrcDirPrefix_Temp;
        // Qt owns mouse capture; CopyTo uses the existing progress adapter.
        Copy_WasCalled = true;
        Copy_HRESULT = E_FAIL;
        Copy_HRESULT = source->CopyTo(options, Indices, &Messages);
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
        QStringList lines;
        FOR_VECTOR(i, dataObjectSpec->Messages)
        lines.append(ToQString(dataObjectSpec->Messages[i]));
        const char *summary =
            QT_TRANSLATE_NOOP("QtTaskResultDialog", "The operation completed with messages.");
        if (res == E_ABORT)
            summary = QT_TRANSLATE_NOOP("QtTaskResultDialog", "The operation was cancelled.");
        else if (res != S_OK) {
            summary = QT_TRANSLATE_NOOP("QtTaskResultDialog", "The operation failed.");
            lines.append(ToQString(HResultToMessage(res)));
        }
        disableNotify.Restore();
        QtTaskResultDialog result(summary, lines.join('\n'), GetParent());
        result.setAttribute(Qt::WA_DeleteOnClose, false);
        result.exec();
    } else if (res != S_OK && res != E_ABORT) {
        disableNotify.Restore();
        MessageBox_Error_HRESULT(res);
    }
    if (res == S_OK && dataObjectSpec->Messages.IsEmpty() && !canceled)
        KillSelection();
}
