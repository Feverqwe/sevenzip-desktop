// PanelOperations.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source operations with Qt confirmation, joined workers and list publication.

#include "7zip/UI/Common/StdAfx.h"

#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"

#include "Windows/ErrorMsg.h"
#include "dialogs/combo/ComboDialog.h"
#include "dialogs/delete/qtdeleteconfirmation.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelFolderOperations.h"
#include "filemanager/providers/qtfolderpropertycapabilities.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashoperation.h"
#include "platform/trash/qttrashservice.h"
#include "ui/diagnostics/qtdiagnostictext.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QMessageBox>
#include <utility>

static const UInt32 kParentIndex = (UInt32)-1;

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

static bool ConfirmDeleteItems(const CPanel &panel, const CRecordVector<UInt32> &indices,
                               bool recycle) {
    QtDeleteConfirmation::Request request;
    request.mode = recycle               ? QtDeleteConfirmation::Mode::Trash
                   : panel.IsArcFolder() ? QtDeleteConfirmation::Mode::Archive
                                         : QtDeleteConfirmation::Mode::Permanent;
    request.location = ToQString(panel.GetCurrentFolderPrefix());
    if (indices.Size() == 1) {
        QFileIconProvider icons;
        request.icon = panel.IsFSFolder()
                           ? icons.icon(QFileInfo(ToQString(panel.GetItemFullPath(indices[0]))))
                           : icons.icon(panel.IsItem_Folder(indices[0]) ? QFileIconProvider::Folder
                                                                        : QFileIconProvider::File);
    }
    FOR_VECTOR(i, indices) {
        request.names.append(ToQString(panel.GetItemRelPath2(indices[i])));
        const bool filesystemLink =
            panel.IsFSFolder() && QtIsFileSystemLink(ToQString(panel.GetItemFullPath(indices[i])));
        request.includesFolders |= panel.IsItem_Folder(indices[i]) && !filesystemLink;
    }
    QtDeleteConfirmation dialog(std::move(request), panel.GetParent());
    return dialog.exec() == QDialog::Accepted;
}

QtArchiveUpdateResult CPanel::DeleteItems(bool toRecycleBin, IQtTrashService &trash,
                                          CSelectedState &state,
                                          const CRecordVector<UInt32> *explicitIndices,
                                          bool confirm, QtDeleteResultReview resultReview) {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CRecordVector<UInt32> indices;
    if (explicitIndices)
        indices = *explicitIndices;
    else
        Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return {S_FALSE, false};
    SaveSelectedState(state);
    if (!CheckBeforeUpdate(IDS_ERROR_DELETING))
        return {E_ABORT, false};
    const bool recycle = toRecycleBin && IsFSFolder();
    UStringVector recyclePaths;
    if (recycle)
        GetFilePaths(indices, recyclePaths);
    if (confirm && !ConfirmDeleteItems(*this, indices, recycle))
        return {E_ABORT, false};

    CDisableNotify disableNotify(*this);
    QtArchiveUpdateResult result{E_FAIL, std::nullopt};
    if (recycle) {
        result.Result =
            QtMoveItemsToTrash(recyclePaths, trash, GetParent(),
                               resultReview == QtDeleteResultReview::Caller, &result.Diagnostics);
        // The joined Trash operation owns progress/errors. Preserve the source
        // shell branch's refresh after a partial result or cancellation.
    } else {
        CThreadFolderOperations op(FOLDER_TYPE_DELETE);
        op.DeferResultReview = resultReview == QtDeleteResultReview::Caller;
        op.FolderOperations = _folderOperations;
        op.Indices = indices;
        result.Result =
            op.DoOperation(*this, LangString(IDS_DELETING), LangString(IDS_ERROR_DELETING));
        result.Published = op.UpdateCallback->Published;
        FOR_VECTOR(i, op.Sync.Messages)
        result.Diagnostics.append(ToQString(op.Sync.Messages[i]));
        if (!op.Sync.FinalMessage.ErrorMessage.Message.IsEmpty())
            result.Diagnostics.append(ToQString(op.Sync.FinalMessage.ErrorMessage.Message));
    }
    if (!recycle)
        RefreshTitleAlways();
    // Interarchive Move retains its transaction guards until rollback settles;
    // that caller publishes the captured selection after releasing them.
    if (recycle || !explicitIndices)
        RefreshListCtrl(state);
    return result;
}
