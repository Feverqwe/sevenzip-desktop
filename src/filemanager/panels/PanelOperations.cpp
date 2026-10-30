// PanelOperations.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Filesystem Create/Rename/Delete portions; archive mutations remain P1-04.

#include "7zip/UI/Common/StdAfx.h"

#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"

#include "Windows/ErrorMsg.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtfolderpropertycapabilities.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashservice.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/qtprogresstransport.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QScopeGuard>

#ifdef _WIN32
#include "Windows/COM.h"
#endif

static const UInt32 kParentIndex = (UInt32)-1;

enum EFolderOpType {
    FOLDER_TYPE_CREATE_FOLDER = 0,
    FOLDER_TYPE_DELETE = 1,
    FOLDER_TYPE_RENAME = 2,
    FOLDER_TYPE_COMMENT = 4
};

class CThreadFolderOperations : public CProgressThreadVirt {
    HRESULT ProcessVirt() Z7_override;

  public:
    EFolderOpType OpType;
    UString Name;
    UInt32 Index;
    CRecordVector<UInt32> Indices;
    CMyComPtr<IFolderOperations> FolderOperations;
    CMyComPtr<IProgress> UpdateCallback;
    CUpdateCallback100Imp *UpdateCallbackSpec;

    explicit CThreadFolderOperations(EFolderOpType opType) : OpType(opType) {}
    HRESULT DoOperation(CPanel &panel, const UString &progressTitle, const UString &titleError);
};

HRESULT CThreadFolderOperations::ProcessVirt() {
#ifdef _WIN32
    NWindows::NCOM::CComInitializer comInitializer;
#endif
    switch ((int)OpType) {
    case FOLDER_TYPE_CREATE_FOLDER:
        return FolderOperations->CreateFolder(Name, UpdateCallback);
    case FOLDER_TYPE_DELETE:
        return FolderOperations->Delete(Indices.ConstData(), Indices.Size(), UpdateCallback);
    case FOLDER_TYPE_RENAME:
        return FolderOperations->Rename(Index, Name, UpdateCallback);
    case FOLDER_TYPE_COMMENT: {
        NWindows::NCOM::CPropVariant value(Name);
        return FolderOperations->SetProperty(Index, kpidComment, &value, UpdateCallback);
    }
    default:
        return E_FAIL;
    }
}

HRESULT CThreadFolderOperations::DoOperation(CPanel &panel, const UString &progressTitle,
                                             const UString &titleError) {
    UpdateCallbackSpec = new CUpdateCallback100Imp;
    UpdateCallback = UpdateCallbackSpec;
    UpdateCallbackSpec->ProgressDialog = this;

    WaitMode = true;
    Sync.FinalMessage.ErrorMessage.Title = titleError;
    UpdateCallbackSpec->Init();

    if (!panel.GetParentFolders().IsEmpty()) {
        const CFolderLink &folderLink = panel.GetParentFolders().Back();
        UpdateCallbackSpec->PasswordIsDefined = folderLink.UsePassword;
        UpdateCallbackSpec->Password = folderLink.Password;
    }

    MainWindow = panel.GetParent();
    MainTitle = "7-Zip";
    MainAddTitle = progressTitle + L' ';

    std::unique_ptr<IQtProgressTransport> transport;
    if (auto *services = dynamic_cast<IQtProgressServices *>(FolderOperations.Interface()))
        transport = OpType == FOLDER_TYPE_COMMENT ? services->CreateNativeProgressTransport(*this)
                                                  : services->CreateProgressTransport(*this);
    RINOK(Create(progressTitle, MainWindow))
    return Result;
}

static bool IsCorrectFsName(const UString &name) {
    const UString lastPart = name.Ptr((unsigned)(name.ReverseFind_PathSepar() + 1));
    return !lastPart.IsEqualTo(".") && !lastPart.IsEqualTo("..");
}

bool CPanel::CanComment() const {
    const int focused = _selectionList.GetFocusedItem();
    if (focused < 0 || GetRealItemIndex(focused) == kParentIndex || IsThereReadOnlyFolder())
        return false;
    const auto *folder = dynamic_cast<IQtFolderPropertyCapabilities *>(_folder.Interface());
    return folder && folder->CanComment();
}

void CPanel::ChangeComment() {
    if (IsHashFolder())
        return;
    if (!CheckBeforeUpdate(IDS_COMMENT))
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    const int index = _selectionList.GetFocusedItem();
    if (index < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(index);
    if (realIndex == kParentIndex)
        return;
    CSelectedState state;
    SaveSelectedState(state);
    UString comment;
    {
        NWindows::NCOM::CPropVariant propVariant;
        if (_folder->GetProperty(realIndex, kpidComment, &propVariant) != S_OK)
            return;
        if (propVariant.vt == VT_BSTR)
            comment = propVariant.bstrVal;
        else if (propVariant.vt != VT_EMPTY)
            return;
    }
    const UString name = GetItemRelPath2(realIndex);
    UString title = name;
    title += " : ";
    AddLangString(title, IDS_COMMENT);
    CComboDialog dlg(ToQString(title), ToQString(LangString(IDS_COMMENT2)), ToQString(comment),
                     GetParent());
    dlg.BindLanguage(0, IDS_COMMENT2);
    if (dlg.exec() != QDialog::Accepted)
        return;

    CDisableNotify disableNotify(*this);
    // Keep the source property operation on Qt's joined worker/callback
    // transport; this source owner retains error and refresh ordering.
    CThreadFolderOperations op(FOLDER_TYPE_COMMENT);
    op.FolderOperations = _folderOperations;
    op.Index = realIndex;
    op.Name = dlg.Value;
    op.DisableUserQuestions = true;
    const HRESULT result = op.DoOperation(
        *this, LangString(IDS_COMMENT),
        QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Set Comment Error")));
    if (result != S_OK && result != E_ABORT) {
        if (result == E_NOINTERFACE)
            MessageBox_Error_UnsupportOperation();
        else
            MessageBox_Error_HRESULT_Caption(
                result,
                QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Set Comment Error")));
    }
    // The original reaches RefreshListCtrl here even after operation errors.
    RefreshListCtrl(state);
}

bool CPanel::CorrectFsPath(const UString &path2, UString &result) {
    return ::CorrectFsPath(GetFsPath(), path2, result);
}

void CPanel::CreateFolder() {
    if (IsHashFolder())
        return;
    if (!CheckBeforeUpdate(IDS_CREATE_FOLDER_ERROR))
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CSelectedState state;
    SaveSelectedState(state);
    UString enteredName;
    if (!Dlg_CreateFolder(GetParent(), enteredName))
        return;
    UString newName = enteredName;
    if (!IsCorrectFsName(newName)) {
        MessageBox_Error_HRESULT(E_INVALIDARG);
        return;
    }
    if (IsFSFolder()) {
        UString correctName;
        if (!CorrectFsPath(newName, correctName)) {
            MessageBox_Error_HRESULT(E_INVALIDARG);
            return;
        }
        newName = correctName;
    }
    HRESULT res;
    CDisableNotify disableNotify(*this);
    {
        CThreadFolderOperations op(FOLDER_TYPE_CREATE_FOLDER);
        op.FolderOperations = _folderOperations;
        op.Name = newName;
        res = op.DoOperation(*this, LangString(IDS_CREATE_FOLDER),
                             LangString(IDS_CREATE_FOLDER_ERROR));
        // fixed for 9.26: we must refresh always
    }
    if (res == S_OK) {
        int pos = newName.Find(WCHAR_PATH_SEPARATOR);
        if (pos >= 0)
            newName.DeleteFrom((unsigned)(pos));
        if (!_mySelectMode)
            state.SelectedNames.Clear();
        state.FocusedName = newName;
        state.FocusedName_Defined = true;
        state.SelectFocused = true;
    }
    RefreshTitleAlways();
    RefreshListCtrl(state);
}

void CPanel::CreateFile() {
    if (IsHashFolder())
        return;
    if (!CheckBeforeUpdate(IDS_CREATE_FILE_ERROR))
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CSelectedState state;
    SaveSelectedState(state);
    CComboDialog dlg(ToQString(LangString(IDS_CREATE_FILE)),
                     ToQString(LangString(IDS_CREATE_FILE_NAME)),
                     ToQString(LangString(IDS_CREATE_FILE_DEFAULT_NAME)), GetParent());
    dlg.BindLanguage(IDS_CREATE_FILE, IDS_CREATE_FILE_NAME);
    if (dlg.exec() != QDialog::Accepted)
        return;
    CDisableNotify disableNotify(*this);
    UString newName = dlg.Value;
    if (IsFSFolder()) {
        UString correctName;
        if (!CorrectFsPath(newName, correctName)) {
            MessageBox_Error_HRESULT(E_INVALIDARG);
            return;
        }
        newName = correctName;
    }
    const HRESULT result = _folderOperations->CreateFile(newName, NULL);
    if (result != S_OK) {
        MessageBox_Error_HRESULT_Caption(result, LangString(IDS_CREATE_FILE_ERROR));
        return;
    }
    const int pos = newName.Find(WCHAR_PATH_SEPARATOR);
    if (pos >= 0)
        newName.DeleteFrom((unsigned)pos);
    if (!_mySelectMode)
        state.SelectedNames.Clear();
    state.FocusedName = newName;
    state.FocusedName_Defined = true;
    state.SelectFocused = true;
    RefreshListCtrl(state);
}

bool CPanel::OnBeginLabelEdit(int row) {
    const unsigned realIndex = GetRealItemIndex(row);
    if (realIndex == kParentIndex)
        return true;
    if (IsThereReadOnlyFolder())
        return true;
    return false;
}

void CPanel::RenameFile() {
    if (!CheckBeforeUpdate(IDS_ERROR_RENAMING))
        return;
    int index = _selectionList.GetFocusedItem();
    if (index >= 0)
        _selectionList.EditLabel(index);
}

HRESULT CPanel::OnEndLabelEdit(UInt32 realIndex, const UString &enteredName) {
    CDisableTimerProcessing disableTimerProcessing2(*this);
    if (!CheckBeforeUpdate(IDS_ERROR_RENAMING))
        return E_ABORT;
    if (!IsCorrectFsName(enteredName)) {
        MessageBox_Error_HRESULT(E_INVALIDARG);
        return E_INVALIDARG;
    }
    UString newName = enteredName;
    if (IsFSFolder()) {
        UString correctName;
        if (!CorrectFsPath(newName, correctName)) {
            MessageBox_Error_HRESULT(E_INVALIDARG);
            return E_INVALIDARG;
        }
        newName = correctName;
    }
    CSelectedState state;
    SaveSelectedState(state);
    if (realIndex == kParentIndex)
        return E_INVALIDARG;
    const UString prefix = GetItemPrefix(realIndex);
    const UString oldName = GetItemName(realIndex);
    CDisableNotify disableNotify(*this);
    CThreadFolderOperations op(FOLDER_TYPE_RENAME);
    op.FolderOperations = _folderOperations;
    op.Index = realIndex;
    op.Name = newName;
    const HRESULT result =
        op.DoOperation(*this, LangString(IDS_RENAMING), LangString(IDS_ERROR_RENAMING));
    if (result == S_OK)
        state.FocusedName = prefix + newName;
    else
        state.FocusedName = prefix + oldName;
    state.FocusedName_Defined = true;
    state.SelectFocused = true;
    _dontShowMode = true;
    // Qt already queued the editor completion; its publisher takes this source
    // reload state instead of posting a second native list message.
    _refreshSelection = std::move(state);
    return result;
}

static bool ConfirmDeleteItems(const CPanel &panel, const CRecordVector<UInt32> &indices) {
    UInt32 titleID, messageID;
    UString messageParam;
    if (indices.Size() == 1) {
        const unsigned index = indices[0];
        messageParam = panel.GetItemRelPath2(index);
        if (panel.IsItem_Folder(index)) {
            titleID = IDS_CONFIRM_FOLDER_DELETE;
            messageID = IDS_WANT_TO_DELETE_FOLDER;
        } else {
            titleID = IDS_CONFIRM_FILE_DELETE;
            messageID = IDS_WANT_TO_DELETE_FILE;
        }
    } else {
        titleID = IDS_CONFIRM_ITEMS_DELETE;
        messageID = IDS_WANT_TO_DELETE_ITEMS;
    }
    // The source shell branch presents the platform confirmation itself. Qt's
    // native trash API does not, so use the same source prompt for both modes.
    const QString title = ToQString(LangString(titleID));
    const QString message =
        ToQString(indices.Size() == 1 ? MyFormatNew(messageID, messageParam)
                                      : MyFormatNew(messageID, UInt64(indices.Size())));
    return QMessageBox::question(panel.GetParent(), title, message,
                                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                 QMessageBox::Yes) == QMessageBox::Yes;
}

static HRESULT MoveItemsToTrash(const CPanel &panel, const CRecordVector<UInt32> &indices,
                                IQtTrashService &trash) {
    FOR_VECTOR(i, indices) {
        const QString path = ToQString(panel.GetItemFullPath(indices[i]));
        if (QFileInfo(path).isSymLink())
            return E_NOTIMPL;
        if (!trash.MoveToTrash(path))
            return E_FAIL;
    }
    return S_OK;
}

HRESULT CPanel::DeleteItems(bool toRecycleBin, IQtTrashService &trash, CSelectedState &state,
                            const CRecordVector<UInt32> *explicitIndices, bool confirm) {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CRecordVector<UInt32> indices;
    if (explicitIndices)
        indices = *explicitIndices;
    else
        Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return S_FALSE;
    SaveSelectedState(state);
    if (!CheckBeforeUpdate(IDS_ERROR_DELETING) || (confirm && !ConfirmDeleteItems(*this, indices)))
        return E_ABORT;

    CDisableNotify disableNotify(*this);
    const bool recycle = toRecycleBin && IsFSFolder();
    HRESULT result;
    if (recycle) {
        result = MoveItemsToTrash(*this, indices, trash);
        // QFile's native trash boundary has no shell error UI. Preserve the
        // source shell branch's refresh after a partial result.
        if (result != S_OK)
            MessageBox_Error_HRESULT_Caption(result, LangString(IDS_ERROR_DELETING));
    } else {
        CThreadFolderOperations op(FOLDER_TYPE_DELETE);
        op.FolderOperations = _folderOperations;
        op.Indices = indices;
        auto *folder = dynamic_cast<NFsFolder::CFSFolder *>((IFolderFolder *)_folder);
        const auto clearCheckBreak = qScopeGuard([folder] {
            if (folder)
                folder->CheckBreak = {};
        });
        if (folder)
            folder->CheckBreak = [&op](const QString &path) {
                op.Sync.Set_FilePath(path.toStdWString().c_str());
                return op.Sync.CheckStop() == S_OK;
            };
        result = op.DoOperation(*this, LangString(IDS_DELETING), LangString(IDS_ERROR_DELETING));
    }
    if (!recycle)
        RefreshTitleAlways();
    // Interarchive Move retains its transaction guards until rollback settles;
    // that caller publishes the captured selection after releasing them.
    if (recycle || !explicitIndices)
        RefreshListCtrl(state);
    return result;
}
