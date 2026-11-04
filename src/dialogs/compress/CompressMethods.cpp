// Adapted format and method selection from 7-Zip CPP/7zip/UI/GUI/CompressDialog.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/compress/CompressDialog.h"
#include <QCollator>
#include <QComboBox>

namespace NCompressDialog {
static const char *const kMethodsNames[] = {"Copy",    "LZMA",      "LZMA2", "PPMd",   "BZip2",
                                            "Deflate", "Deflate64", "ZSTD",  "SHA256", "SHA1",
                                            "CRC32",   "CRC64",     "GNU",   "POSIX"};

const char *GetMethodName(int methodID) {
    if (methodID >= 0 && (unsigned)methodID < Z7_ARRAY_SIZE(kMethodsNames))
        return kMethodsNames[methodID];
    return nullptr;
}

static const EMethodID g_7zMethods[] = {kLZMA2,
                                        kLZMA,
                                        kPPMd,
                                        kBZip2,
                                        kDeflate,
                                        kDeflate64
                                        // , kZSTD
                                        ,
                                        kCopy};

static const EMethodID g_7zSfxMethods[] = {kCopy, kLZMA, kLZMA2, kPPMd};

bool IsMethodSupportedBySfx(int methodID) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_7zSfxMethods); i++)
        if (methodID == g_7zSfxMethods[i])
            return true;
    return false;
}

static const EMethodID g_ZipMethods[] = {kDeflate, kDeflate64, kBZip2, kLZMA, kZSTD};

static const EMethodID g_GZipMethods[] = {kDeflate};

static const EMethodID g_BZip2Methods[] = {kBZip2};

static const EMethodID g_XzMethods[] = {kLZMA2};

/*
static const EMethodID g_ZstdMethods[] =
{
  kZSTD
};
*/

/*
static const EMethodID g_SwfcMethods[] =
{
  kDeflate
  // kLZMA
};
*/

static const EMethodID g_TarMethods[] = {kGnu, kPosix};

static const EMethodID g_HashMethods[] = {
    kSha256, kSha1
    // , kCrc32
    // , kCrc64
};

#define METHODS_PAIR(x) Z7_ARRAY_SIZE(x), x

static const CFormatInfo g_Formats[] = {
    {"",
     // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
     ((UInt32)1 << 10) - 1,
     // (UInt32)(Int32)-1,
     0, NULL, kFF_MultiThread | kFF_MemUse},
    {
        "7z",
        // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        (1 << 10) - 1, METHODS_PAIR(g_7zMethods),
        kFF_Filter | kFF_Solid | kFF_MultiThread | kFF_Encrypt | kFF_EncryptFileNames | kFF_MemUse |
            kFF_SFX
        // | kFF_Time_Win
    },
    {
        "Zip", (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        METHODS_PAIR(g_ZipMethods), kFF_MultiThread | kFF_Encrypt | kFF_MemUse
        // | kFF_Time_Win | kFF_Time_Unix | kFF_Time_DOS
    },
    {
        "GZip", (1 << 1) | (1 << 5) | (1 << 7) | (1 << 9), METHODS_PAIR(g_GZipMethods), kFF_MemUse
        // | kFF_Time_Unix
    },
    {"BZip2", (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9), METHODS_PAIR(g_BZip2Methods),
     kFF_MultiThread | kFF_MemUse},
    {"xz",
     // (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
     (1 << 10) - 1 - (1 << 0), // store (1 << 0) is not supported
     METHODS_PAIR(g_XzMethods), kFF_Solid | kFF_MultiThread | kFF_MemUse},
    /*
    {
      "zstd",
      // (1 << (MY_ZSTD_LEVEL_MAX + 1)) - 1,
      (1 << (9 + 1)) - 1,
      METHODS_PAIR(g_ZstdMethods),
      // kFF_Solid |
      kFF_MultiThread
      | kFF_MemUse
    },
    */
    /*
      {
        "Swfc",
        (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        METHODS_PAIR(g_SwfcMethods),
        0
      },
    */
    {
        "Tar", (1 << 0), METHODS_PAIR(g_TarMethods), 0
        // kFF_Time_Unix | kFF_Time_Win // | kFF_Time_1ns
    },
    {
        "wim", (1 << 0), 0, NULL, 0
        // | kFF_Time_Win
    },
    {"Hash", (0 << 0), METHODS_PAIR(g_HashMethods), 0}};

static unsigned GetStaticFormatIndex(QStringView name) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_Formats); i++)
        if (name.compare(QLatin1StringView(g_Formats[i].Name), Qt::CaseInsensitive) == 0)
            return i;
    return 0;
}

const CFormatInfo &GetFormatInfo(QStringView name) { return g_Formats[GetStaticFormatIndex(name)]; }

QStringList GetMethodNames(QStringView name, bool isSFX) {
    const CFormatInfo &fi = GetFormatInfo(name);
    const bool is7z = name.compare(QLatin1StringView("7z"), Qt::CaseInsensitive) == 0;
    QStringList methods;
    for (unsigned m = 0; m < fi.NumMethods; m++) {
        const int methodID = fi.MethodIDs[m];
        if (isSFX && !IsMethodSupportedBySfx(methodID))
            continue;
        const char *method = kMethodsNames[methodID];
        if (is7z)
            if (methodID == kCopy || methodID == kDeflate || methodID == kDeflate64)
                continue;
        methods.append(QString::fromLatin1(method));
    }
    return methods;
}

int GetMethodID(QStringView name, QStringView format) {
    const auto &fi = GetFormatInfo(format);
    for (unsigned i = 0; i < fi.NumMethods; i++)
        if (name == QLatin1StringView(kMethodsNames[fi.MethodIDs[i]]))
            return fi.MethodIDs[i];
    return -1;
}

} // namespace NCompressDialog

void CCompressDialog::SetMethods(const CObjectVector<CCodecInfoUser> &userCodecs) {
    using namespace NCompressDialog;
    ExternalMethods.Clear();
    {
        FOR_VECTOR(i, userCodecs) {
            const CCodecInfoUser &c = userCodecs[i];
            if (!c.EncoderIsAssigned || !c.IsFilter_Assigned || c.IsFilter || c.NumStreams != 1)
                continue;
            unsigned k;
            for (k = 0; k < Z7_ARRAY_SIZE(g_7zMethods); k++)
                if (c.Name.IsEqualTo_Ascii_NoCase(kMethodsNames[g_7zMethods[k]]))
                    break;
            if (k != Z7_ARRAY_SIZE(g_7zMethods))
                continue;
            ExternalMethods.Add(c.Name);
        }
    }
}

using namespace NCompressDialog;

// CB_ADDSTRING/CBS_SORT is a Win32 presentation service, with no portable
// upstream implementation. Preserve its selected item and returned insertion
// index; the original OnInit below owns fallback/registry/explicit selection.
static int AddSortedFormat(QComboBox &control, const UString &name, unsigned arcIndex) {
    const QString text = QString::fromWCharArray(name.Ptr()).toLower();
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    int index = 0;
    while (index < control.count() && collator.compare(control.itemText(index), text) <= 0)
        ++index;
    const bool hadSelection = control.currentIndex() >= 0;
    control.insertItem(index, text, arcIndex);
    if (!hadSelection)
        control.setCurrentIndex(-1);
    return index;
}

// Format population/selection block from original OnInit. Other initialization
// phases bind Qt controls in CreateArchiveDialog. ArcIndices is then projected
// from combo item data for source Browse/history consumers that no longer own
// an HWND; fallback still uses the first original native index before sorting.
bool CCompressDialog::OnInit(QComboBox &formatControl, const NCompression::CInfo &registry) {
    formatControl.clear();
    const bool needSetMain = (Info.FormatIndex < 0);
    FOR_VECTOR(i, ArcIndices) {
        const unsigned arcIndex = ArcIndices[i];
        const CArcInfoEx &ai = (*ArcFormats)[arcIndex];
        const int index = AddSortedFormat(formatControl, ai.Name, arcIndex);
        if (!needSetMain) {
            if (Info.FormatIndex == (int)arcIndex)
                formatControl.setCurrentIndex(index);
            continue;
        }
        if (i == 0 || ai.Name.IsEqualTo_NoCase(UString(registry.ArcType.toStdWString().c_str()))) {
            formatControl.setCurrentIndex(index);
            Info.FormatIndex = (int)arcIndex;
        }
    }
    ArcIndices.Clear();
    for (int i = 0; i < formatControl.count(); ++i)
        ArcIndices.Add(formatControl.itemData(i).toUInt());
    return formatControl.currentIndex() >= 0;
}

UInt32 CCompressDialog::GetComboValue(const QComboBox &control, int defMax) {
    if (control.count() <= defMax)
        return (UInt32)(Int32)-1;
    // Native GetItemData_of_CurSel returns CB_ERR when GetCurSel is absent.
    if (control.currentIndex() < 0)
        return (UInt32)(Int32)-1;
    return control.currentData().toUInt();
}

// Original SetMethod2/GetMethodID/GetMethodSpec bodies. Qt supplies the control,
// current level/registry and MethodChanged presentation callback; method IDs,
// auto sentinel, external-method ordering and selection branches remain source.
void CCompressDialog::SetMethod2(QComboBox &control, UInt32 level,
                                 const NCompression::CInfo &registry,
                                 const std::function<void()> &methodChanged, int keepMethodId) {
    m_Method = &control;
    m_Method->clear();
    _auto_MethodId = -1;
    const CFormatInfo &fi = GetFormatInfo(Format);
    const CArcInfoEx &ai = Get_ArcInfoEx();
    if (level == 0 && !ai.Flags_HashHandler()) {
        if (!ai.Is_Tar() && !ai.Is_Zstd()) {
            methodChanged();
            return;
        }
    }
    UString defaultMethod;
    {
        const int index = FindRegistryFormat(registry, QString::fromWCharArray(ai.Name.Ptr()));
        if (index >= 0) {
            const NCompression::CFormatOptions &fo = registry.Formats[index];
            defaultMethod = fo.Method.toStdWString().c_str();
        }
    }
    const bool isSfx = IsSFX();
    bool weUseSameMethod = false;
    const bool is7z = ai.Is_7z();
    for (unsigned m = 0;; m++) {
        int methodID;
        const char *method;
        if (m < fi.NumMethods) {
            methodID = fi.MethodIDs[m];
            method = kMethodsNames[methodID];
            if (is7z)
                if (methodID == kCopy || methodID == kDeflate || methodID == kDeflate64)
                    continue;
        } else {
            if (!is7z)
                break;
            const unsigned extIndex = m - fi.NumMethods;
            if (extIndex >= ExternalMethods.Size())
                break;
            methodID = (int)(Z7_ARRAY_SIZE(kMethodsNames) + extIndex);
            method = ExternalMethods[extIndex].Ptr();
        }
        if (isSfx)
            if (!IsMethodSupportedBySfx(methodID))
                continue;
        AString s(method);
        int writtenMethodId = methodID;
        if (m == 0) {
            _auto_MethodId = methodID;
            writtenMethodId = -1;
            s.Insert(0, "*  ");
        }
        const bool hadSelection = m_Method->currentIndex() >= 0;
        m_Method->addItem(QString::fromLatin1(s.Ptr()), writtenMethodId);
        // CComboBox::AddString does not select an item; QComboBox does.
        // Keep selection solely under the original branches below.
        if (!hadSelection)
            m_Method->setCurrentIndex(-1);
        const int itemIndex = m_Method->count() - 1;
        if (keepMethodId == methodID) {
            m_Method->setCurrentIndex(itemIndex);
            weUseSameMethod = true;
            continue;
        }
        if ((defaultMethod.IsEqualTo_Ascii_NoCase(method) || m == 0) && !weUseSameMethod)
            m_Method->setCurrentIndex(itemIndex);
    }
    if (!weUseSameMethod)
        methodChanged();
}

int CCompressDialog::GetMethodID_RAW() const {
    if (!m_Method)
        return MethodID;
    if (m_Method->count() <= 0)
        return -1;
    if (m_Method->currentIndex() < 0)
        return -1; // Original GetItemData_of_CurSel returns CB_ERR.
    return (int)(Int32)(UInt32)m_Method->currentData().toUInt();
}

int CCompressDialog::GetMethodID() const {
    if (!m_Method)
        return MethodID;
    int raw = GetMethodID_RAW();
    if (raw < 0)
        return _auto_MethodId;
    return raw;
}

UString CCompressDialog::GetMethodSpec(UString &estimatedName) const {
    estimatedName.Empty();
    if (!m_Method) {
        estimatedName = HashMethodSpec;
        return estimatedName;
    }
    if (m_Method->count() < 1)
        return estimatedName;
    const int methodIdRaw = GetMethodID_RAW();
    int methodId = methodIdRaw;
    if (methodIdRaw < 0)
        methodId = _auto_MethodId;
    UString s;
    if (methodId >= 0) {
        if ((unsigned)methodId < Z7_ARRAY_SIZE(kMethodsNames))
            estimatedName = kMethodsNames[methodId];
        else
            estimatedName =
                ExternalMethods[(unsigned)methodId - (unsigned)Z7_ARRAY_SIZE(kMethodsNames)];
        if (methodIdRaw >= 0)
            s = estimatedName;
    }
    return s;
}

UString CCompressDialog::GetMethodSpec() const {
    UString estimatedName;
    UString s = GetMethodSpec(estimatedName);
    return s;
}

bool CCompressDialog::IsMethodEqualTo(const UString &s) const {
    UString estimatedName;
    const UString shortName = GetMethodSpec(estimatedName);
    if (s.IsEmpty())
        return shortName.IsEmpty();
    return s.IsEqualTo_NoCase(estimatedName);
}
