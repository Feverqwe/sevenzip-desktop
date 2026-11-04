// Adapted from 7-Zip PanelOperations.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "filemanager/UpdateCallback100.h"

class CPanel;

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
    CMyComPtr2<IProgress, CUpdateCallback100Imp> UpdateCallback;

    explicit CThreadFolderOperations(EFolderOpType opType) : OpType(opType) {}
    HRESULT DoOperation(CPanel &panel, const UString &progressTitle, const UString &titleError);
};
