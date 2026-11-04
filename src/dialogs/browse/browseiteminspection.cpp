// Inspection adapted from 7-Zip UI/FileManager/BrowseDialog2.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/browse/browseiteminspection.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"

using namespace NWindows;
using namespace NFile;
using namespace NFind;
namespace NTempBrowse {
static const UInt32 k_EnumerateDirsLimit = 200;
static const UInt32 k_EnumerateFilesLimit = 2000;

// POSIX upstream enumerators expose dirent then a no-follow FileInfo conversion.
bool BrowseNext(CEnumerator &enumerator, CFileInfo &fi, bool &found) {
#ifdef _WIN32
    return enumerator.Next(fi, found);
#else
    CDirEntry entry;
    if (!enumerator.Next(entry, found))
        return false;
    return !found || enumerator.Fill_FileInfo(entry, fi, false);
#endif
}
bool CBrowseEnumerator::NeedInterrupt() const {
    return bi.NumFiles >= k_EnumerateFilesLimit || bi.NumDirs >= k_EnumerateDirsLimit;
}

void CBrowseEnumerator::Enumerate(unsigned level) {
    Path.Add_PathSepar();
    const unsigned len = Path.Len();
    CObjectVector<FString> names;
    {
        CEnumerator enumerator;
        enumerator.SetDirPrefix(Path);
        bool found;
        while (BrowseNext(enumerator, fi, found) && found) {
            if (level == 0) {
                if (bi.NumRootItems == 0)
                    fi_SubFile = fi;
                bi.NumRootItems++;
            }
            if (fi.IsDir()) {
                bi.NumDirs++;
                if (!fi.IsOsSymLink())
                    names.Add(fi.Name);
            } else {
                bi.NumFiles++;
                bi.Size += fi.Size;
            }
            if (level != 0 || bi.NumRootItems > 1)
                if (NeedInterrupt()) {
                    bi.WasInterrupted = true;
                    return;
                }
        }
    }
    FOR_VECTOR(i, names) {
        if (NeedInterrupt()) {
            bi.WasInterrupted = true;
            return;
        }
        Path.DeleteFrom(len);
        Path += names[i];
        Enumerate(level + 1);
    }
}

static void PrintPropsPrefix(UString &s, UInt32 id) {
    s.Add_LF();
    s += "    ";
    AddLangString(s, id);
    s += ": ";
}
static void PrintProps_Size(UString &s, UInt64 size) {
    PrintPropsPrefix(s, IDS_PROP_SIZE);
    AddSizeValue(s, size);
}
static void PrintProps_MTime(UString &s, const CFileInfo &fi) {
    PrintPropsPrefix(s, IDS_PROP_MTIME);
    char t[64];
    FILETIME ft;
    FiTime_To_FILETIME(fi.MTime, ft);
    ConvertUtcFileTimeToString(ft, t);
    s += t;
}
void PrintProps_Name(UString &s, const CFileInfo &fi) {
    s += fs2us(fi.Name);
    if (fi.IsDir())
        s.Add_PathSepar();
}
static void PrintProps_Attrib(UString &s, const CFileInfo &fi) {
    PrintPropsPrefix(s, IDS_PROP_ATTRIBUTES);
    char props[64];
    ConvertWinAttribToString(props, fi.GetWinAttrib());
    s += props;
}
void PrintProps(UString &s, const CBrowseItem &bi, const CFileInfo &fi, const CFileInfo *fi2) {
    PrintProps_Name(s, fi);
    PrintProps_Attrib(s, fi);
    if (bi.NumDirs != 0) {
        PrintPropsPrefix(s, IDS_PROP_FOLDERS);
        s.Add_UInt32(bi.NumDirs);
        if (bi.WasInterrupted)
            s += "+";
    }
    if (bi.NumFiles != 0) {
        PrintPropsPrefix(s, IDS_PROP_FILES);
        s.Add_UInt32(bi.NumFiles);
        if (bi.WasInterrupted)
            s += "+";
    }
    {
        PrintProps_Size(s, bi.Size);
        if (bi.WasInterrupted)
            s += "+";
    }
    PrintProps_MTime(s, fi);
    if (fi2) {
        s.Add_LF();
        s += "----------------";
        s.Add_LF();
        PrintProps_Name(s, *fi2);
        PrintProps_Attrib(s, *fi2);
        if (!fi2->IsDir())
            PrintProps_Size(s, fi2->Size);
        PrintProps_MTime(s, *fi2);
    }
}

} // namespace NTempBrowse
