// Inspection adapted from 7-Zip UI/FileManager/BrowseDialog2.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include "Windows/FileFind.h"
#include "common/collections/MyVector.h"

struct CBrowseItem {
    unsigned MainFileIndex;
    int SubFileIndex;
    bool WasInterrupted;
    UInt32 NumFiles, NumDirs, NumRootItems;
    UInt64 Size;
    void Construct() {
        SubFileIndex = -1;
        WasInterrupted = false;
        NumFiles = NumDirs = NumRootItems = 0;
        Size = 0;
    }
};

namespace NTempBrowse {
struct CBrowseEnumerator {
    FString Path;
    NWindows::NFile::NFind::CFileInfo fi, fi_SubFile;
    CBrowseItem bi;
    CBrowseEnumerator() { bi.Construct(); }
    void Enumerate(unsigned level);
    bool NeedInterrupt() const;
};
bool BrowseNext(NWindows::NFile::NFind::CEnumerator &enumerator,
                NWindows::NFile::NFind::CFileInfo &fi, bool &found);
void PrintProps_Name(UString &s, const NWindows::NFile::NFind::CFileInfo &fi);
void PrintProps(UString &s, const CBrowseItem &bi, const NWindows::NFile::NFind::CFileInfo &fi,
                const NWindows::NFile::NFind::CFileInfo *fi2);
} // namespace NTempBrowse
