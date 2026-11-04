// Resource option records adapted from 7-Zip CompressDialog.cpp/.h.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyTypes.h"
#include "archive/operations/ZipRegistry.h"
#include <QList>
#include <QString>
#include <QStringView>

namespace NCompressDialog {
struct CInfo;

struct CDictionaryOptions {
    UInt32 AutoDict = UInt32(-1);
    bool HasAuto = false;
    int Selected = 0;
    QList<quint64> Values;
};
CDictionaryOptions SetDictionary2(int methodID, UInt32 level, UInt32 defaultDict = UInt32(-1));
QString Combo_AddDict2(quint64 sizeShow);

struct COrderOptions {
    UInt32 AutoOrder = 1;
    int Selected = 0;
    QList<int> Values;
};
bool GetOrderMode(int methodID);
COrderOptions SetOrder2(int methodID, UInt32 level, UInt32 defaultOrder = UInt32(-1));
struct CSolidOptions {
    UInt64 AutoSolid = 1 << 20;
    int Selected = 0;
    QList<UInt32> Values;
};
CSolidOptions SetSolidBlockSize2(QStringView format, UInt32 level, int methodId, UInt64 dict,
                                 UInt32 defaultBlockSize = UInt32(-1));
QString Add_Size(UInt64 val);
void OnOK(UInt32 solidLogSize, CInfo &Info);

struct CSystemInfo {
    UInt32 NumCPUs = 1;
    UInt32 NumHardwareThreads = 1;
    bool RamSizeDefined = false;
    UInt64 RamSize = 0;
    UInt64 RamUsageAuto = 0;
    UInt64 RamSizeReduced = 1 << 26;
};
struct CThreadOptions {
    UInt32 AutoNumThreads = 1;
    bool HasAuto = false;
    QList<UInt32> Values;
};
CSystemInfo GetSystemInfo();
CThreadOptions SetNumThreads2(QStringView format, int methodID, UInt32 level, UInt64 dict64,
                              UInt32 blockSizeLog, const CSystemInfo &system,
                              const NCompression::CMemUse &memUse = {});
UInt64 GetMemoryUsage_Threads_Dict_DecompMem(QStringView format, int methodId, UInt32 level,
                                             UInt32 numThreads, UInt64 dict64, UInt32 blockSizeLog,
                                             UInt64 &decompressMemory);
QString AddMemUsage(UInt64 v);
struct CMemComboItem {
    QString User;
    QString Spec;
};
QList<CMemComboItem> SetMemUseCombo(const QString &spec = {}, int *selected = nullptr);
UInt64 Get_MemUse_Bytes(const NCompression::CMemUse &mu, const CSystemInfo &system);

} // namespace NCompressDialog
