#pragma once

#include "Common/MyString.h"
#include "common/collections/MyVector.h"

class QComboBox;
class QString;

bool ParseVolumeSizes(const UString &s, CRecordVector<UInt64> &values);
bool ParseVolumeSizesText(const QString &text, CRecordVector<UInt64> &values);
void AddVolumeItems(QComboBox &volumeCombo);
