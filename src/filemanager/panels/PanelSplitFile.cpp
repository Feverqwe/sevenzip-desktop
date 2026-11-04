// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelSplitFile.cpp.
// Upstream 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Copyright (C) 1999-2026 Igor Pavlov. GNU LGPL 2.1 or later.
// File Manager Split/Combine commands; native workers belong to fileops/volumes.
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/FileName.h"
#include "dialogs/copy/CopyDialog.h"
#include "dialogs/split/SplitDialog.h"
#include "filemanager/App.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "filemanager/panels/Panel.h"
#include "fileops/volumes/nativevolumeworker.h"
#include "fileops/volumes/qtvolumeservices.h"
#include "localization/LangUtils.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

using namespace NWindows;
using namespace NFile;
using namespace NDir;

void CApp::Split(IQtVolumeServices &services) {
    const unsigned srcPanelIndex = GetFocusedPanelIndex();
    CPanel &srcPanel = Panels[srcPanelIndex];
    if (!srcPanel.Is_IO_FS_Folder()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    CRecordVector<UInt32> indices;
    srcPanel.Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return;
    if (indices.Size() != 1) {
        srcPanel.MessageBox_Error_LangID(IDS_SELECT_ONE_FILE);
        return;
    }
    const unsigned index = indices[0];
    if (srcPanel.IsItem_Folder(index)) {
        srcPanel.MessageBox_Error_LangID(IDS_SELECT_ONE_FILE);
        return;
    }
    const UString itemName = srcPanel.GetItemName(index);

    const UString srcPath = srcPanel.GetFsPath() + srcPanel.GetItemPrefix(index);
    UString path = srcPath;
    unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
    CPanel &destPanel = Panels[destPanelIndex];
    if (NumPanels > 1)
        if (destPanel.IsFSFolder())
            path = destPanel.GetFsPath();
    CSplitDialog splitDialog(ToQString(srcPanel.GetItemRelPath(index)), ToQString(path), _window);
    if (splitDialog.exec() != QDialog::Accepted)
        return;

    NFind::CFileInfo fileInfo;
    if (!fileInfo.Find(us2fs(srcPath + itemName))) {
        srcPanel.MessageBox_Error(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot find file")));
        return;
    }
    if (fileInfo.Size <= splitDialog.VolumeSizes.FrontItem()) {
        srcPanel.MessageBox_Error_LangID(IDS_SPLIT_VOL_MUST_BE_SMALLER);
        return;
    }
    const UInt64 numVolumes = GetNumberOfVolumes(fileInfo.Size, splitDialog.VolumeSizes);
    if (numVolumes >= 100) {
        if (QMessageBox::question(_window, ToQString(LangString(IDS_SPLIT_CONFIRM_TITLE)),
                                  ToQString(MyFormatNew(IDS_SPLIT_CONFIRM_MESSAGE, numVolumes)),
                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel) !=
            QMessageBox::Yes)
            return;
    }

    path = ToUString(splitDialog.Path);
    NName::NormalizeDirPathPrefix(path);
    if (QFileInfo(QDir::cleanPath(ToQString(path))).isSymLink() || !CreateComplexDir(us2fs(path))) {
        const HRESULT lastError = GetLastError_noZero_HRESULT();
        srcPanel.MessageBox_Error_2Lines_Message_HRESULT(
            MyFormatNew(IDS_CANNOT_CREATE_FOLDER, path), lastError);
        return;
    }

    {
        CSplitVolumePlan spliter;
        spliter.NumVolumes = numVolumes;

        const UString title = LangString(IDS_SPLITTING);

        spliter.FilePath = us2fs(srcPath + itemName);
        spliter.VolBasePath = us2fs(path + srcPanel.GetItemName_for_Copy(index));
        spliter.VolumeSizes = splitDialog.VolumeSizes;

        // if (splitDialog.VolumeSizes.Size() == 0) return;

        // CPanel::CDisableTimerProcessing disableTimerProcessing1(srcPanel);
        // CPanel::CDisableTimerProcessing disableTimerProcessing2(destPanel);

        services.StartSplit(spliter, title, [this] { RefreshTitleAlways(); });
    }

    // disableNotify.Restore();
    // disableNotify.Restore();
    // srcPanel.SetFocusToList();
    // srcPanel.RefreshListCtrlSaveFocused();
}

void CApp::Combine(IQtVolumeServices &services) {
    const unsigned srcPanelIndex = GetFocusedPanelIndex();
    CPanel &srcPanel = Panels[srcPanelIndex];
    if (!srcPanel.IsFSFolder()) {
        srcPanel.MessageBox_Error_LangID(IDS_OPERATION_IS_NOT_SUPPORTED);
        return;
    }
    CRecordVector<UInt32> indices;
    srcPanel.Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return;
    const unsigned index = indices[0];
    if (indices.Size() != 1 || srcPanel.IsItem_Folder(index)) {
        srcPanel.MessageBox_Error_LangID(IDS_COMBINE_SELECT_ONE_FILE);
        return;
    }
    const UString itemName = srcPanel.GetItemName(index);

    UString srcPath = srcPanel.GetFsPath() + srcPanel.GetItemPrefix(index);
    UString path = srcPath;
    unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
    CPanel &destPanel = Panels[destPanelIndex];
    if (NumPanels > 1)
        if (destPanel.IsFSFolder())
            path = destPanel.GetFsPath();

    CVolSeqName volSeqName;
    if (!volSeqName.ParseName(itemName)) {
        srcPanel.MessageBox_Error_LangID(IDS_COMBINE_CANT_DETECT_SPLIT_FILE);
        return;
    }

    {
        CCombineVolumePlan combiner;

        combiner.LoadParts(srcPath, itemName, volSeqName);
        if (combiner.Names.Size() == 1) {
            srcPanel.MessageBox_Error_LangID(IDS_COMBINE_CANT_FIND_MORE_THAN_ONE_PART);
            return;
        }

        if (combiner.TotalSize == 0) {
            srcPanel.MessageBox_Error(
                QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "No data")));
            return;
        }

        combiner.InputDirPrefix = us2fs(srcPath);
        const UString info = combiner.GetInfo();
        {
            const QString suffix = ' ' + ToQString(srcPanel.GetItemRelPath(index));
            CCopyDialog copyDialog(ToQString(LangString(IDS_COMBINE)) + suffix,
                                   ToQString(LangString(IDS_COMBINE_TO)), ToQString(path),
                                   ToQString(info), _window);
            copyDialog.BindLanguage(IDS_COMBINE, IDS_COMBINE_TO, suffix);
            copyDialog.setObjectName("combineDialog");
            if (copyDialog.exec() != QDialog::Accepted)
                return;
            path = copyDialog.Value;
        }

        NName::NormalizeDirPathPrefix(path);
        if (QFileInfo(QDir::cleanPath(ToQString(path))).isSymLink() ||
            !CreateComplexDir(us2fs(path))) {
            const HRESULT lastError = GetLastError_noZero_HRESULT();
            srcPanel.MessageBox_Error_2Lines_Message_HRESULT(
                MyFormatNew(IDS_CANNOT_CREATE_FOLDER, path), lastError);
            return;
        }

        const UString outName = volSeqName.GetCombinedName();

        NFind::CFileInfo fileInfo;
        UString destFilePath = path + outName;
        combiner.OutputPath = us2fs(destFilePath);
        if (fileInfo.Find(combiner.OutputPath)) {
            srcPanel.MessageBox_Error(MyFormatNew(IDS_FILE_EXIST, destFilePath));
            return;
        }

        const UString title = LangString(IDS_COMBINING);

        // CPanel::CDisableTimerProcessing disableTimerProcessing1(srcPanel);
        // CPanel::CDisableTimerProcessing disableTimerProcessing2(destPanel);

        services.StartCombine(combiner, title, [this] { RefreshTitleAlways(); });
    }

    // disableNotify.Restore();
    // disableNotify.Restore();
    // srcPanel.SetFocusToList();
    // srcPanel.RefreshListCtrlSaveFocused();
}
