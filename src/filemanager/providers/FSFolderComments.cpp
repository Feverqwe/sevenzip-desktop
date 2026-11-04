// Comment persistence adapted from 7-Zip UI/FileManager/FSFolder.cpp.
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

bool CFSFolder::SaveComments() {
    AString utf;
    {
        UString unicode;
        _comments.SaveToString(unicode);
        ConvertUnicodeToUTF8(unicode, utf);
    }
    if (!utf.IsAscii())
        utf.Insert(0, "\xEF\xBB\xBF"
                      "\r\n");

    FString path = _path + kDescriptionFileName;
    NWindows::NFile::NIO::COutFile file;
#ifdef _WIN32
    // We must set same attrib. COutFile::CreateAlways can fail, if file has another attrib.
    DWORD attrib = FILE_ATTRIBUTE_NORMAL;
    {
        NWindows::NFile::NFind::CFileInfo fi;
        if (fi.Find(path))
            attrib = fi.Attrib;
    }
    if (!file.Create_ALWAYS_with_Attribs(path, attrib))
        return false;
    UInt32 processed;
    file.Write(utf, utf.Len(), processed);
#else
    // The portable source writer keeps existing POSIX permissions on truncation.
    if (!file.Create_ALWAYS(path))
        return false;
    size_t processed;
    file.write_full(utf, utf.Len(), processed);
#endif
    _commentsAreLoaded = false;
    return true;
}

Z7_COM7F_IMF(CFSFolder::SetProperty(UInt32 index, PROPID propID, const PROPVARIANT *value,
                                    IProgress * /* progress */)) {
    if (index >= Files.Size())
        return E_INVALIDARG;
    CDirItem &fi = Files[index];
    if (fi.Parent >= 0)
        return E_NOTIMPL;
    switch (propID) {
    case kpidComment: {
        UString filename = fs2us(fi.Name);
        filename.Trim();
        if (value->vt == VT_EMPTY)
            _comments.DeletePair(filename);
        else if (value->vt == VT_BSTR) {
            CTextPair pair;
            pair.ID = filename;
            pair.ID.Trim();
            pair.Value.SetFromBstr(value->bstrVal);
            pair.Value.Trim();
            if (pair.Value.IsEmpty())
                _comments.DeletePair(filename);
            else
                _comments.AddPair(pair);
        } else
            return E_INVALIDARG;
        SaveComments();
        break;
    }
    default:
        return E_NOTIMPL;
    }
    return S_OK;
}
} // namespace NFsFolder
