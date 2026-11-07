// Adapted from 7-Zip CPP/7zip/UI/FileManager/FormatUtils.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/FormatUtils.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/OverwriteDialogRes.h"
#include "Common/IntToString.h"
#include "localization/LangUtils.h"
#include "localization/qtlanguageservices.h"

#define INT_TO_STR_SPEC(v)                                                                         \
    while (v >= 10) {                                                                              \
        temp[i++] = (Byte)('0' + (unsigned)(v % 10));                                              \
        v /= 10;                                                                                   \
    }                                                                                              \
    *s++ = (Byte)('0' + (unsigned)v);

static void ConvertSizeToString(UInt64 val, wchar_t *s) noexcept {
    Byte temp[32];
    unsigned i = 0;
    if (val <= (UInt32)0xFFFFFFFF) {
        UInt32 val32 = (UInt32)val;
        INT_TO_STR_SPEC(val32)
    } else {
        INT_TO_STR_SPEC(val)
    }
    if (i < 3) {
        if (i != 0) {
            *s++ = temp[(size_t)i - 1];
            if (i == 2)
                *s++ = temp[0];
        }
        *s = 0;
        return;
    }
    unsigned r = i % 3;
    if (r != 0) {
        s[0] = temp[--i];
        if (r == 2)
            s[1] = temp[--i];
        s += r;
    }
    do {
        s[0] = ' ';
        s[1] = temp[(size_t)i - 1];
        s[2] = temp[(size_t)i - 2];
        s[3] = temp[(size_t)i - 3];
        s += 4;
    } while (i -= 3);
    *s = 0;
}

#undef INT_TO_STR_SPEC

QString ConvertSizeToString(UInt64 value) {
    wchar_t s[32];
    ConvertSizeToString(value, s);
    return QString::fromWCharArray(s);
}

bool IsTimestampProp(UINT propID) noexcept {
    return propID == kpidMTime || propID == kpidCTime || propID == kpidATime ||
           propID == kpidChangeTime;
}

bool IsByteSizeProp(UINT propID) noexcept {
    switch (propID) {
    case kpidSize:
    case kpidPackSize:
    case kpidPhySize:
    case kpidHeadersSize:
    case kpidTotalSize:
    case kpidFreeSpace:
    case kpidClusterSize:
    case kpidAltStreamsSize:
    case kpidVirtualSize:
    case kpidUnpackSize:
    case kpidTotalPhySize:
    case kpidTailSize:
    case kpidEmbeddedStubSize:
        return true;
    }
    return false;
}

bool IsSizeProp(UINT propID) noexcept {
    if (IsByteSizeProp(propID))
        return true;
    switch (propID) {
    case kpidNumSubDirs:
    case kpidNumSubFiles:
    case kpidOffset:
    case kpidLinks:
    case kpidNumBlocks:
    case kpidNumVolumes:
    case kpidNumErrors:
    case kpidNumStreams:
    case kpidNumAltStreams:
        return true;
    }
    return false;
}

void AddSizeValue(UString &s, UInt64 value) {
    {
        s += MyFormatNew(IDS_FILE_SIZE, value);
    }
    if (value >= (1 << 10)) {
        char c;
        if (value >= ((UInt64)10 << 30)) {
            value >>= 30;
            c = 'G';
        } else if (value >= (10 << 20)) {
            value >>= 20;
            c = 'M';
        } else {
            value >>= 10;
            c = 'K';
        }
        s += " : ";
        s.Add_UInt64(value);
        s.Add_Space();
        s.Add_Char(c);
        s += "iB";
    }
}

// Original shared list-size formatter used by BrowseDialog2.
wchar_t *Browse_ConvertSizeToString(UInt64 v, wchar_t *s Z7_lifetimebound) {
    char c = 0;
    if (v >= ((UInt64)10000 << 20)) {
        v >>= 30;
        c = 'G';
    } else if (v >= ((UInt64)10000 << 10)) {
        v >>= 20;
        c = 'M';
    } else if (v >= ((UInt64)10000 << 0)) {
        v >>= 10;
        c = 'K';
    }
    s = ConvertUInt64ToString(v, s);
    if (c != 0) {
        *s++ = ' ';
        *s++ = (wchar_t)c;
        *s++ = 'B';
        *s = 0;
    }
    return s;
}

UString NumberToString(UInt64 number) {
    wchar_t numberString[32];
    ConvertUInt64ToString(number, numberString);
    return numberString;
}

UString MyFormatNew(const UString &format, const UString &argument) {
    const QString result = QString::fromWCharArray(format).arg(QString::fromWCharArray(argument));
    return UString(result.toStdWString().c_str());
}

UString MyFormatNew(UINT resourceID, UInt64 count) {
    return UString(QtTranslatedResourceCount(resourceID, count).toStdWString().c_str());
}

UString MyFormatNew(UINT resourceID, const UString &argument) {
    return MyFormatNew(LangString(resourceID), argument);
}
