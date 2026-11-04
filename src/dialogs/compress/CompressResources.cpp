// Native resource choices and estimates adapted from 7-Zip CompressDialog.cpp.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/compress/CompressResources.h"
#include "Windows/System.h"
#include "archive/compression/MethodProps.h"
#include "dialogs/compress/CompressDialog.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"
#include <QCoreApplication>
#include <QLocale>

namespace NCompressDialog {
static const UInt32 kLzmaMaxDictSize = (UInt32)15 << 28;
static const UInt32 kSolidLog_NoSolid = 0;
static const UInt32 kSolidLog_FullSolid = 64;

// LZMA/LZMA2 numeric branch from CCompressDialog::SetDictionary2 (0766b73).
// Registry and Win32 insertion are handled by the Qt adapter.
CDictionaryOptions SetDictionary2(int methodID, UInt32 level, UInt32 defaultDict) {
    CDictionaryOptions options;
    switch (methodID) {
    case kLZMA:
    case kLZMA2: {
        options.HasAuto = true;
        {
            options.AutoDict = level <= 4 ? (UInt32)1 << (level * 2 + 16)
                               : level <= sizeof(size_t) / 2 + 4
                                   ? (UInt32)1 << (level + 20)
                                   : (UInt32)1 << (sizeof(size_t) / 2 + 24);
        }

        if (defaultDict != UInt32(-1) && defaultDict >= ((UInt32)15 << 28))
            defaultDict = kLzmaMaxDictSize;
        const size_t kLzmaMaxDictSize_Up = (size_t)1 << (20 + sizeof(size_t) / 4 * 6);

        for (unsigned i = (16 - 1) * 2; i <= (32 - 1) * 2; i++) {
            if (i < (20 - 1) * 2 && i != (16 - 1) * 2 && i != (18 - 1) * 2)
                continue;
            if (i == (20 - 1) * 2 + 1)
                continue;
            const size_t dict_up = (size_t)(2 + (i & 1)) << (i / 2);
            size_t dict = dict_up;
            if (dict_up >= kLzmaMaxDictSize)
                dict = kLzmaMaxDictSize; // we reduce dictionary

            options.Values.append(quint64(dict));
            if (defaultDict != UInt32(-1))
                if (dict <= defaultDict || options.Selected <= 0)
                    options.Selected = options.Values.size();
            if (dict_up >= kLzmaMaxDictSize_Up)
                break;
        }

        break;
    }

    case kPPMd: {
        options.HasAuto = true;
        options.AutoDict = (UInt32)1 << (level + 19);

        const UInt32 kPpmd_Default_4g = (UInt32)0 - ((UInt32)1 << 10);
        const size_t kPpmd_MaxDictSize_Up = (size_t)1 << (29 + sizeof(size_t) / 8);
        if (defaultDict != UInt32(-1) && defaultDict >= ((UInt32)15 << 28))
            defaultDict = kPpmd_Default_4g;

        for (unsigned i = (20 - 1) * 2; i <= (32 - 1) * 2; i++) {
            if (i == (20 - 1) * 2 + 1)
                continue;

            const size_t dict_up = (size_t)(2 + (i & 1)) << (i / 2);
            size_t dict = dict_up;
            if (dict_up >= kPpmd_Default_4g)
                dict = kPpmd_Default_4g;

            options.Values.append(quint64(dict));
            if (defaultDict != UInt32(-1))
                if (dict <= defaultDict || options.Selected <= 0)
                    options.Selected = options.Values.size();
            if (dict_up >= kPpmd_MaxDictSize_Up)
                break;
        }
        break;
    }

    case kBZip2: {
        options.HasAuto = true;
        {
            if (level >= 5)
                options.AutoDict = (900 << 10);
            else if (level >= 3)
                options.AutoDict = (500 << 10);
            else
                options.AutoDict = (100 << 10);
        }

        for (unsigned i = 1; i <= 9; i++) {
            const UInt32 dict = ((UInt32)i * 100) << 10;
            options.Values.append(quint64(dict));
            if (defaultDict != UInt32(-1))
                if (i <= defaultDict / 100000 || options.Selected <= 0)
                    options.Selected = options.Values.size();
        }
        break;
    }

    case kDeflate:
    case kDeflate64: {
        options.HasAuto = true;
        const UInt32 dict = (methodID == kDeflate ? (UInt32)(1 << 15) : (UInt32)(1 << 16));
        options.AutoDict = dict;
        // EnableItem(IDC_COMPRESS_DICTIONARY, false);
        break;
    }

    case kCopy:
        options.AutoDict = 0;
        options.Values.append(0);
        break;
    default:
        break;
    }
    return options;
}
// Source Combo_AddDict2 text body; the Qt caller owns item insertion and data.
QString Combo_AddDict2(quint64 sizeShow) {
    char c = 0;
    unsigned moveBits = 0;
    if ((sizeShow & 0xFFFFF) == 0) {
        moveBits = 20;
        c = 'M';
    } else if ((sizeShow & 0x3FF) == 0) {
        moveBits = 10;
        c = 'K';
    }
    AString s;
    s.Add_UInt64(sizeShow >> moveBits);
    s.Add_Space();
    if (c != 0)
        s.Add_Char(c);
    s.Add_Char('B');
    return QString::fromLatin1(s.Ptr());
}

// LZMA, Deflate and PPMd branches copied from CCompressDialog::SetOrder2 (0766b73).
// The caller replaces registry selection and ComboBox insertion with Qt data.
COrderOptions SetOrder2(int methodID, UInt32 level, UInt32 defaultOrder) {
    COrderOptions options;
    switch (methodID) {
    case kLZMA:
    case kLZMA2: {
        options.AutoOrder = (level < 7 ? 32 : 64);
        for (unsigned i = 2 * 2; i < 8 * 2; i++) {
            UInt32 order = ((UInt32)(2 + (i & 1)) << (i / 2));
            if (order > 256)
                order = 273;
            options.Values.append(int(order));
            if (defaultOrder != UInt32(-1))
                if (order <= defaultOrder || options.Selected <= 0)
                    options.Selected = options.Values.size();
        }
        break;
    }
    case kDeflate:
    case kDeflate64: {
        {
            if (level >= 9)
                options.AutoOrder = 128;
            else if (level >= 7)
                options.AutoOrder = 64;
            else
                options.AutoOrder = 32;
        }
        for (unsigned i = 2 * 2; i < 8 * 2; i++) {
            UInt32 order = ((UInt32)(2 + (i & 1)) << (i / 2));
            if (order > 256)
                order = (methodID == kDeflate64 ? 257 : 258);
            options.Values.append(int(order));
            if (defaultOrder != UInt32(-1))
                if (order <= defaultOrder || options.Selected <= 0)
                    options.Selected = options.Values.size();
        }

        break;
    }

    case kPPMd: {
        {
            if (level >= 9)
                options.AutoOrder = 32;
            else if (level >= 7)
                options.AutoOrder = 16;
            else if (level >= 5)
                options.AutoOrder = 6;
            else
                options.AutoOrder = 4;
        }

        for (unsigned i = 0;; i++) {
            UInt32 order = i + 2;
            if (i >= 2)
                order = (4 + ((i - 2) & 3)) << ((i - 2) / 4);
            options.Values.append(int(order));
            if (defaultOrder != UInt32(-1))
                if (order <= defaultOrder || options.Selected <= 0)
                    options.Selected = options.Values.size();
            if (order >= 32)
                break;
        }
        break;
    }

    // case kBZip2:
    default:
        break;
    }
    return options;
}
bool GetOrderMode(int methodID) {
    switch (methodID) {
    case kPPMd:
        return true;
    default:
        break;
    }
    return false;
}

static UInt64 Get_Lzma2_ChunkSize(UInt64 dict) {
    // we use same default chunk sizes as defined in 7z encoder and lzma2 encoder
    UInt64 cs = (UInt64)dict << 2;
    const UInt32 kMinSize = (UInt32)1 << 20;
    const UInt32 kMaxSize = (UInt32)1 << 28;
    if (cs < kMinSize)
        cs = kMinSize;
    if (cs > kMaxSize)
        cs = kMaxSize;
    if (cs < dict)
        cs = dict;
    cs += (kMinSize - 1);
    cs &= ~(UInt64)(kMinSize - 1);
    return cs;
}

static void Add_Size(AString &s, UInt64 val) {
    unsigned moveBits = 0;
    char c = 0;
    if ((val & 0x3FFFFFFF) == 0) {
        moveBits = 30;
        c = 'G';
    } else if ((val & 0xFFFFF) == 0) {
        moveBits = 20;
        c = 'M';
    } else if ((val & 0x3FF) == 0) {
        moveBits = 10;
        c = 'K';
    }
    s.Add_UInt64(val >> moveBits);
    s.Add_Space();
    if (moveBits != 0)
        s.Add_Char(c);
    s.Add_Char('B');
}

QString Add_Size(UInt64 val) {
    AString s;
    Add_Size(s, val);
    return QString::fromLatin1(s.Ptr());
}

CSolidOptions SetSolidBlockSize2(QStringView format, UInt32 level, int methodId, UInt64 dict,
                                 UInt32 defaultBlockSize) {
    CSolidOptions options;
    const CFormatInfo &fi = GetFormatInfo(format);
    if (!fi.Solid_())
        return options;
    if (level == 0)
        return options;
    if (dict == (UInt64)(Int64)-1) {
        dict = 1 << 25; // default dict for unknown methods
                        // return;
    }

    const bool is7z = format.compare(QLatin1StringView("7z"), Qt::CaseInsensitive) == 0;
    const UInt64 cs = Get_Lzma2_ChunkSize(dict);

    // Solid Block Size
    UInt64 blockSize = cs; // for xz

    if (is7z) {
        // we use same default block sizes as defined in 7z encoder
        UInt64 kMaxSize = (UInt64)1 << 32;
        if (methodId == kLZMA2) {
            blockSize = cs << 6;
            kMaxSize = (UInt64)1 << 34;
        } else {
            UInt64 dict2 = dict;
            if (methodId == kBZip2) {
                dict2 /= 100000;
                if (dict2 < 1)
                    dict2 = 1;
                dict2 *= 100000;
            }
            blockSize = dict2 << 7;
        }

        const UInt32 kMinSize = (UInt32)1 << 24;
        if (blockSize < kMinSize)
            blockSize = kMinSize;
        if (blockSize > kMaxSize)
            blockSize = kMaxSize;
    }

    options.AutoSolid = blockSize;

    if (is7z) {
        options.Values.append(kSolidLog_NoSolid);
        if (defaultBlockSize == kSolidLog_NoSolid)
            options.Selected = options.Values.size();
    }
    for (unsigned i = 20; i <= 36; i++) {
        options.Values.append(i);
        const int index = options.Values.size();
        if (defaultBlockSize != UInt32(-1))
            if (i <= defaultBlockSize || index <= 1)
                options.Selected = index;
    }
    options.Values.append(kSolidLog_FullSolid);
    if (defaultBlockSize == kSolidLog_FullSolid)
        options.Selected = options.Values.size();
    return options;
}

// Solid conversion block copied from CCompressDialog::OnOK.
void OnOK(UInt32 solidLogSize, CInfo &Info) {
    Info.SolidBlockSize = 0;
    if (solidLogSize == (UInt32)(Int32)-1)
        Info.SolidIsSpecified = false;
    else if (solidLogSize > 0)
        Info.SolidBlockSize =
            (solidLogSize >= 64) ? (UInt64)(Int64)-1 : ((UInt64)1 << solidLogSize);
}
UInt64 GetMemoryUsage_Threads_Dict_DecompMem(QStringView format, int methodId, UInt32 level,
                                             UInt32 numThreads, UInt64 dict64, UInt32 blockSizeLog,
                                             UInt64 &decompressMemory) {
    decompressMemory = (UInt64)(Int64)-1;

    if (level == 0 && format.compare(QLatin1StringView("zstd"), Qt::CaseInsensitive) != 0) {
        decompressMemory = (1 << 20);
        return decompressMemory;
    }
    UInt64 size = 0;

    const CFormatInfo &fi = GetFormatInfo(format);
    if (fi.Filter_() && level >= 9)
        size += (12 << 20) * 2 + (5 << 20);
    // UInt32 numThreads = GetNumThreads2();

    UInt32 numMainZipThreads = 1;

    if (format.compare(QLatin1StringView("zip"), Qt::CaseInsensitive) == 0) {
        UInt32 numSubThreads = 1;
        if (methodId == kLZMA && numThreads > 1 && level >= 5)
            numSubThreads = 2;
        numMainZipThreads = numThreads / numSubThreads;
        if (numMainZipThreads > 1)
            size += (UInt64)numMainZipThreads * ((size_t)sizeof(size_t) << 23);
        else
            numMainZipThreads = 1;
    }

    if (dict64 == (UInt64)(Int64)-1 && methodId != kZSTD)
        return (UInt64)(Int64)-1;

    switch (methodId) {
    case kZSTD: {
        const size_t encoderMemory = ZSTD_estimateCStreamSize((int)level);
        if (ZSTD_isError(encoderMemory))
            return (UInt64)(Int64)-1;
        const ZSTD_compressionParameters params = ZSTD_getCParams((int)level, 0, 0);
        decompressMemory = (UInt64)1 << params.windowLog;
        return size + (UInt64)encoderMemory * numMainZipThreads;
    }
    case kLZMA:
    case kLZMA2: {
        const UInt32 dict = (dict64 >= kLzmaMaxDictSize ? kLzmaMaxDictSize : (UInt32)dict64);
        UInt32 hs = dict - 1;
        hs |= (hs >> 1);
        hs |= (hs >> 2);
        hs |= (hs >> 4);
        hs |= (hs >> 8);
        hs >>= 1;
        if (hs >= (1 << 24))
            hs >>= 1;
        hs |= (1 << 16) - 1;
        // if (numHashBytes >= 5)
        if (level < 5)
            hs |= (256 << 10) - 1;
        hs++;
        UInt64 size1 = (UInt64)hs * 4;
        size1 += (UInt64)dict * 4;
        if (level >= 5)
            size1 += (UInt64)dict * 4;
        size1 += (2 << 20);

        UInt32 numThreads1 = 1;
        if (numThreads > 1 && level >= 5) {
            size1 += (2 << 20) + (4 << 20);
            numThreads1 = 2;
        }

        UInt32 numBlockThreads = numThreads / numThreads1;

        UInt64 chunkSize = 0; // it's solid chunk

        if (methodId != kLZMA && numBlockThreads != 1) {
            chunkSize = Get_Lzma2_ChunkSize(dict);

            if (format.compare(QLatin1StringView("xz"), Qt::CaseInsensitive) == 0) {
                if (blockSizeLog != (UInt32)(Int32)-1) {
                    if (blockSizeLog == kSolidLog_FullSolid) {
                        numBlockThreads = 1;
                        chunkSize = 0;
                    } else if (blockSizeLog != kSolidLog_NoSolid)
                        chunkSize = (UInt64)1 << blockSizeLog;
                }
            }
        }

        if (chunkSize == 0) {
            const UInt32 kBlockSizeMax = (UInt32)0 - (UInt32)(1 << 16);
            UInt64 blockSize = (UInt64)dict + (1 << 16) + (numThreads1 > 1 ? (1 << 20) : 0);
            blockSize += (blockSize >> (blockSize < ((UInt32)1 << 30) ? 1 : 2));
            if (blockSize >= kBlockSizeMax)
                blockSize = kBlockSizeMax;
            size += numBlockThreads * (size1 + blockSize);
        } else {
            size += numBlockThreads * (size1 + chunkSize);
            const UInt32 numPackChunks = numBlockThreads + (numBlockThreads / 8) + 1;
            if (chunkSize < ((UInt32)1 << 26))
                numBlockThreads++;
            if (chunkSize < ((UInt32)1 << 24))
                numBlockThreads++;
            if (chunkSize < ((UInt32)1 << 22))
                numBlockThreads++;
            size += numPackChunks * chunkSize;
        }

        decompressMemory = dict + (2 << 20);
        return size;
    }

        /*
        case kZSTD:
        {
          CZstdEncProps props;
          ZstdEncProps_Init(&props);
          // props.level_zstd = level;
          props.level_7z = level;
          props.nbWorkers = numThreads;
          ZstdEncProps_SetDictProps_From_CompressDialog(&props, *this);
          ZstdEncProps_NormalizeFull(&props);
          size = ZstdEncProps_GetMemUsage(&props);
          decompressMemory = (UInt64)1 << props.windowLog;
          return size;
        }
        */

    case kPPMd: {
        decompressMemory = dict64 + (2 << 20);
        return size + decompressMemory;
    }

    case kDeflate:
    case kDeflate64: {
        UInt64 size1 = 3 << 20;
        // if (level >= 7)
        size1 += (1 << 20);
        size += size1 * numMainZipThreads;
        decompressMemory = (2 << 20);
        return size;
    }

    case kBZip2: {
        decompressMemory = (7 << 20);
        UInt64 memForOneThread = (10 << 20);
        return size + memForOneThread * numThreads;
    }

    default:
        break;
    }

    return (UInt64)(Int64)-1;
}

CSystemInfo GetSystemInfo() {
    CSystemInfo info;
    NWindows::NSystem::CProcessAffinity threadsInfo;
    threadsInfo.InitST();
#ifndef Z7_ST
#ifdef _WIN32
    threadsInfo.Get_and_return_NumProcessThreads_and_SysThreads(info.NumCPUs,
                                                                info.NumHardwareThreads);
#else
    // POSIX CProcessAffinity exposes the primitive getters; copy the Windows normalization.
    UInt32 num1 = 0, num2 = 0;
    if (threadsInfo.Get()) {
        num1 = threadsInfo.GetNumProcessThreads();
        num2 = threadsInfo.GetNumSystemThreads();
    }
    if (num1 == 0)
        num1 = NWindows::NSystem::GetNumberOfProcessors();
    if (num1 == 0)
        num1 = 1;
    if (num2 < num1)
        num2 = num1;
    info.NumCPUs = num1;
    info.NumHardwareThreads = num2;
#endif
#endif
    size_t size = (size_t)sizeof(size_t) << 29;
    info.RamSizeDefined = NWindows::NSystem::GetRamSize(size);
    {
        unsigned bits = sizeof(size_t) * 8;
        if (bits == 32) {
            const UInt32 limit2 = (UInt32)7 << 28;
            if (size > limit2)
                size = limit2;
        }
    }
    info.RamSize = size;
    const size_t kMinUseSize = 1 << 26;
    if (size < kMinUseSize)
        size = kMinUseSize;
    info.RamSizeReduced = size;
    info.RamUsageAuto = Calc_From_Val_Percents(size, 80);
    return info;
}

CThreadOptions SetNumThreads2(QStringView format, int methodID, UInt32 level, UInt64 dict64,
                              UInt32 blockSizeLog, const CSystemInfo &system,
                              const NCompression::CMemUse &memUse) {
    CThreadOptions options;
    const CFormatInfo &fi = GetFormatInfo(format);
    if (!fi.MultiThread_())
        return options;
    options.HasAuto = true;
    const UInt32 numCPUs = system.NumCPUs;
    const UInt32 numHardwareThreads = system.NumHardwareThreads;
    const bool isZip = format.compare(QLatin1StringView("zip"), Qt::CaseInsensitive) == 0;
    UInt32 numAlgoThreadsMax = numHardwareThreads * 2; // for unknow methods
    if (isZip)
        numAlgoThreadsMax =
            8 << (sizeof(size_t) / 2); // 32 threads for 32-bit : 128 threads for 64-bit
    else if (format.compare(QLatin1StringView("xz"), Qt::CaseInsensitive) == 0)
        numAlgoThreadsMax = 256 * 2; // MTCODER_THREADS_MAX * 2
    else
        switch (methodID) {
        case kLZMA:
            numAlgoThreadsMax = 2;
            break;
        case kLZMA2:
            numAlgoThreadsMax = 256 * 2;
            break; // MTCODER_THREADS_MAX * 2
        case kBZip2:
            numAlgoThreadsMax = 64;
            break;
        // case kZSTD: numAlgoThreadsMax = num_ZSTD_threads_MAX; break;
        case kCopy:
        case kPPMd:
        case kDeflate:
        case kDeflate64:
            numAlgoThreadsMax = 1;
            break;
        default:
            break;
        }
    UInt32 autoThreads = numCPUs;
    if (autoThreads > numAlgoThreadsMax)
        autoThreads = numAlgoThreadsMax;

    const UInt64 memUse_Limit = Get_MemUse_Bytes(memUse, system);

    if (system.RamSizeDefined)
        if (autoThreads > 1
            // || (autoThreads == 0 && methodID == kZSTD)
        ) {
            if (isZip) {
                for (; autoThreads > 1; autoThreads--) {
                    UInt64 decompressMemory;
                    const UInt64 usage = GetMemoryUsage_Threads_Dict_DecompMem(
                        format, methodID, level, autoThreads, dict64, blockSizeLog,
                        decompressMemory);
                    if (usage <= memUse_Limit)
                        break;
                }
            } else if (methodID == kLZMA2) {
                const UInt32 numThreads1 = (level >= 5 ? 2 : 1);
                UInt32 numBlockThreads = autoThreads / numThreads1;
                for (; numBlockThreads > 1; numBlockThreads--) {
                    autoThreads = numBlockThreads * numThreads1;
                    UInt64 decompressMemory;
                    const UInt64 usage = GetMemoryUsage_Threads_Dict_DecompMem(
                        format, methodID, level, autoThreads, dict64, blockSizeLog,
                        decompressMemory);
                    if (usage <= memUse_Limit)
                        break;
                }
                autoThreads = numBlockThreads * numThreads1;
            }
            /*
            else if (methodID == kZSTD)
            {
              if (num_ZSTD_threads_MAX != 0)
              {
                CZstdEncProps props;
                ZstdEncProps_Init(&props);
                // props.level_zstd = level;
                props.level_7z = level;
                ZstdEncProps_SetDictProps_From_CompressDialog(&props, *this);
                autoThreads = ZstdEncProps_GetNumThreads_for_MemUsageLimit(&props, memUse_Limit,
            autoThreads);
              }
            }
            */
        }

    options.AutoNumThreads = autoThreads;

    if (numAlgoThreadsMax != autoThreads || autoThreads != 1)
        for (UInt32 i = 1; i <= numHardwareThreads * 2 && i <= numAlgoThreadsMax; i++)
            options.Values.append(i);
    return options;
}

QString AddMemUsage(UInt64 v) {
    const char *post;
    if (v <= ((UInt64)16 << 30)) {
        v = (v + (1 << 20) - 1) >> 20;
        post = QT_TRANSLATE_NOOP("MemoryUnits", "%1 MB");
    } else if (v <= ((UInt64)64 << 40)) {
        v = (v + (1 << 30) - 1) >> 30;
        post = QT_TRANSLATE_NOOP("MemoryUnits", "%1 GB");
    } else {
        const UInt64 v2 = v + ((UInt64)1 << 40) - 1;
        if (v <= v2)
            v = v2;
        v >>= 40;
        post = QT_TRANSLATE_NOOP("MemoryUnits", "%1 TB");
    }
    return QCoreApplication::translate("MemoryUnits", post).arg(QLocale().toString(qulonglong(v)));
}

static void AddMemSize(QString &res, UInt64 size) {
    char c;
    unsigned moveBits = 0;
    if (size >= ((UInt64)1 << 31) && (size & 0x3FFFFFFF) == 0) {
        moveBits = 30;
        c = 'G';
    } else // if (size >= ((UInt32)1 << 21) && (size & 0xFFFFF) == 0)
    {
        moveBits = 20;
        c = 'M';
    }
    // else { moveBits = 10; c = 'K'; }
    res += QString::number(size >> moveBits);
    res += ' ';
    if (moveBits != 0)
        res += QLatin1Char(c);
    res += 'B';
}

static int AddMemComboItem(QList<CMemComboItem> &items, UInt64 val, bool isPercent = false,
                           bool isDefault = false) {
    QString sUser;
    QString sRegistry;
    if (isPercent) {
        QString s;
        s += QString::number(val);
        s += '%';
        if (isDefault)
            sUser = QStringLiteral("*  "); // Source k_Auto_Prefix.
        else
            sRegistry = s;
        sUser += s;
    } else {
        AddMemSize(sUser, val);
        sRegistry = sUser;
        for (;;) {
            const int pos = sRegistry.indexOf(' ');
            if (pos < 0)
                break;
            sRegistry.remove(pos, 1);
        }
        if (!sRegistry.isEmpty())
            if (sRegistry.back() == 'B')
                sRegistry.chop(1);
    }
    const int dataIndex = items.size();
    items.append({sUser, sRegistry});
    return dataIndex;
}

QList<CMemComboItem> SetMemUseCombo(const QString &spec, int *selected) {
    int curSel = 0;
    QList<CMemComboItem> items;
    UInt64 curMem_Bytes = 0;
    UInt64 curMem_Percents = 0;
    bool needSetCur_Bytes = false;
    bool needSetCur_Percents = false;
    {
        if (!spec.isEmpty()) {
            NCompression::CMemUse mu;
            mu.Parse(spec);
            if (mu.IsDefined) {
                if (mu.IsPercent) {
                    curMem_Percents = mu.Val;
                    needSetCur_Percents = true;
                } else {
                    curMem_Bytes = mu.Val;
                    needSetCur_Bytes = true;
                }
            }
        }
    }

    // 80% - is auto usage limit in handlers
    AddMemComboItem(items, 80, true, true);

    {
        for (unsigned i = 10;; i += 10) {
            UInt64 size = i;
            if (i > 100)
                size = (UInt64)(Int64)-1;
            if (needSetCur_Percents && size >= curMem_Percents) {
                curSel = AddMemComboItem(items, curMem_Percents, true);
                needSetCur_Percents = false;
                if (size == curMem_Percents)
                    continue;
            }
            if (size == (UInt64)(Int64)-1)
                break;
            AddMemComboItem(items, size, true);
        }
    }
    {
        for (unsigned i = (27) * 2;; i++) {
            UInt64 size = (UInt64)(2 + (i & 1)) << (i / 2);
            if (i > (20 + sizeof(size_t) * 3 - 1) * 2)
                size = (UInt64)(Int64)-1;
            if (needSetCur_Bytes && size >= curMem_Bytes) {
                curSel = AddMemComboItem(items, curMem_Bytes);
                needSetCur_Bytes = false;
                if (size == curMem_Bytes)
                    continue;
            }
            if (size == (UInt64)(Int64)-1)
                break;
            AddMemComboItem(items, size);
        }
    }
    if (selected)
        *selected = curSel;
    return items;
}

UInt64 Get_MemUse_Bytes(const NCompression::CMemUse &mu, const CSystemInfo &system) {
    if (mu.IsDefined)
        return mu.GetBytes(system.RamSizeReduced);
    return system.RamUsageAuto;
}

} // namespace NCompressDialog
