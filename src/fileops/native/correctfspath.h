// Adapted from 7-Zip BrowseDialog.cpp/.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"

bool CorrectFsPath(const UString &relBase, const UString &path, UString &result);
bool HasEmptyWindowsPathComponent(const UString &path, const UString &resolved);
