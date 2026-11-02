#pragma once
#include "Common/MyString.h"
#include "Common/MyVector.h"
#include "archive/resources/qtarchiveextractstatus.h"

class CDirItems;
HRESULT PrepareQtArchiveTransferInputs(CDirItems &items, const FString &folderPrefix,
                                       const UString &updatePrefix,
                                       const QtArchiveTransferInputs &inputs,
                                       CRecordVector<Byte> &altStreamItems);
