// Comment reading adapted from 7-Zip UI/FileManager/FSFolder.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "Windows/FileIO.h"
#include "filemanager/providers/FSFolder.h"

namespace NFsFolder {

static CFSTR const kDescriptionFileName = FTEXT("descript.ion");

bool CFSFolder::LoadComments() {
    _comments.Clear();
    _commentsAreLoaded = true;
    NWindows::NFile::NIO::CInFile file;
    if (!file.Open(_path + kDescriptionFileName))
        return false;
    UInt64 len;
    if (!file.GetLength(len))
        return false;
    if (len >= (1 << 28))
        return false;
    AString s;
    char *p = s.GetBuf((unsigned)(size_t)len);
    size_t processedSize;
    if (!file.ReadFull(p, (unsigned)(size_t)len, processedSize))
        return false;
    s.ReleaseBuf_CalcLen((unsigned)(size_t)len);
    if (processedSize != len)
        return false;
    file.Close();
    UString unicodeString;
    if (!ConvertUTF8ToUnicode(s, unicodeString))
        return false;
    return _comments.ReadFromString(unicodeString);
}

Z7_COM7F_IMF(CFSFolder::SetProperty(UInt32, PROPID, const PROPVARIANT *, IProgress *)) {
    return E_NOTIMPL;
}
} // namespace NFsFolder
