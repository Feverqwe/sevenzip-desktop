#pragma once
#include "7zip/UI/Common/UpdatePair.h"

// The native pairing algorithm also accepts confirmed staged streams on Unix.
void GetUpdatePairInfoList(const CDirItems &dirItems, const CObjectVector<CArcItem> &arcItems,
                           NFileTimeType::EEnum fileTimeType,
                           CRecordVector<CUpdatePair> &updatePairs,
                           const CRecordVector<Byte> &altStreamItems);
