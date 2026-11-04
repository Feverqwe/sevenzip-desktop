// Declarations for the comparison adapted from 7-Zip PanelSort.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include <QStringView>

int CompareFileNames_ForFolderList(const char16_t *s1, const char16_t *s2);
int CompareFileNames_ForFolderList(const wchar_t *s1, const wchar_t *s2);
int CompareFileNames_ForFolderList(QStringView s1, QStringView s2);
QStringView GetExtensionPtr(QStringView name);
