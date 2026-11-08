// PanelOperations.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source operations with Qt confirmation, joined workers and list publication.

#include "7zip/UI/Common/StdAfx.h"

#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"

#include "Windows/ErrorMsg.h"
#include "dialogs/delete/qtdeleteconfirmation.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelFolderOperations.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashoperation.h"
#include "platform/trash/qttrashservice.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <utility>

static bool ConfirmDeleteItems(const CPanel &panel, const CRecordVector<UInt32> &indices,
                               QtDeleteConfirmation::Mode mode) {
    QtDeleteConfirmation::Request request;
    request.mode = mode;
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

HRESULT CPanel::MoveItemsToTrash(IQtTrashService &trash, CSelectedState &state,
                                 const CRecordVector<UInt32> *explicitIndices, bool confirm,
                                 unsigned *moved, std::optional<QStringList> *review) {
    if (moved)
        *moved = 0;
    if (review)
        review->reset();
    if (!IsFSFolder())
        return E_NOTIMPL;
    CDisableTimerProcessing disableTimerProcessing(*this);
    CRecordVector<UInt32> indices;
    if (explicitIndices)
        indices = *explicitIndices;
    else
        Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return S_FALSE;
    SaveSelectedState(state);
    if (!CheckBeforeUpdate(IDS_ERROR_DELETING))
        return E_ABORT;
    QHash<QString, UInt32> selectedFolders;
    if (GetFlatMode()) {
        FOR_VECTOR(i, indices) {
            const UInt32 index = indices[i];
            if (IsItem_Folder(index))
                selectedFolders.insert(QDir::fromNativeSeparators(ToQString(GetItemRelPath(index))),
                                       index);
        }
    }
    const auto trashRoot = [&](UInt32 index) {
        UInt32 root = index;
        if (selectedFolders.isEmpty())
            return root;
        QString prefix = QDir::fromNativeSeparators(ToQString(GetItemPrefix(index)));
        while (!prefix.isEmpty()) {
            prefix.chop(1);
            root = selectedFolders.value(prefix, root);
            prefix.truncate(prefix.lastIndexOf('/') + 1);
        }
        return root;
    };
    CRecordVector<UInt32> roots;
    FOR_VECTOR(i, indices) {
        if (trashRoot(indices[i]) == indices[i])
            roots.Add(indices[i]);
    }
    CRecordVector<UInt32> precedingRows;
    for (int row = 0; row < state.FocusedItem; ++row)
        precedingRows.Add(trashRoot(GetRealItemIndex(row)));
    UStringVector paths;
    GetFilePaths(roots, paths);
    if (confirm && !ConfirmDeleteItems(*this, indices, QtDeleteConfirmation::Mode::Trash))
        return E_ABORT;

    CDisableNotify disableNotify(*this);
    unsigned movedCount = 0;
    QStringList diagnostics;
    const HRESULT result =
        QtMoveItemsToTrash(paths, trash, GetParent(), review != nullptr, &diagnostics, &movedCount);
    if (review && result != S_OK)
        *review = std::move(diagnostics);
    if (moved)
        *moved = movedCount;
    if (movedCount && state.FocusedItem > 0) {
        // When the focused name disappears, keep its position among surviving rows.
        const auto *first = roots.ConstData();
        const QSet<UInt32> moved(first, first + movedCount);
        FOR_VECTOR(i, precedingRows) {
            if (moved.contains(precedingRows[i]))
                --state.FocusedItem;
        }
    }
    RefreshListCtrl(state);
    return result;
}

QtArchiveUpdateResult CPanel::RemoveItemsAfterMove(const CRecordVector<UInt32> &indices,
                                                   CSelectedState &state, bool confirm) {
    CDisableTimerProcessing disableTimerProcessing(*this);
    if (indices.IsEmpty())
        return {S_FALSE, false};
    SaveSelectedState(state);
    if (!CheckBeforeUpdate(IDS_ERROR_DELETING))
        return {E_ABORT, false};
    if (confirm && !ConfirmDeleteItems(*this, indices, QtDeleteConfirmation::Mode::Archive))
        return {E_ABORT, false};

    CDisableNotify disableNotify(*this);
    CThreadFolderOperations op(FOLDER_TYPE_DELETE);
    op.DeferResultReview = true;
    op.FolderOperations = _folderOperations;
    op.Indices = indices;
    QtArchiveUpdateResult result{E_FAIL, std::nullopt};
    result.Result = op.DoOperation(*this, LangString(IDS_DELETING), LangString(IDS_ERROR_DELETING));
    result.Published = op.UpdateCallback->Published;
    FOR_VECTOR(i, op.Sync.Messages)
    result.Diagnostics.append(ToQString(op.Sync.Messages[i]));
    if (!op.Sync.FinalMessage.ErrorMessage.Message.IsEmpty())
        result.Diagnostics.append(ToQString(op.Sync.FinalMessage.ErrorMessage.Message));
    RefreshTitleAlways();
    // The Move caller publishes the captured selection after its transaction settles.
    return result;
}
