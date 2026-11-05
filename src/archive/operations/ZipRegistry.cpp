// Adapted from 7-Zip CPP/7zip/UI/Common/ZipRegistry (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "archive/operations/ZipRegistry.h"
#include "Common/StringConvert.h"
#include "Common/StringToInt.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

// Adapted from 7-Zip UI/GUI/ExtractDialog.cpp; both dialog histories use it.
void AddUniqueString(CObjectVector<QString> &list, const QString &s) {
    const UString source(s.toStdWString().c_str());
    FOR_VECTOR(i, list)
    if (source.IsEqualTo_NoCase(UString(list[i].toStdWString().c_str())))
        return;
    list.Add(s);
}

// Source g_CS serializes whole settings operations, including group replacement.
static QMutex g_CS;

// A registry DWORD cannot contain an out-of-range value. QVariant::toUInt can
// narrow a wider stored integer, so validate before substituting this service.
static bool Key_Get_UInt32_IfOk(QSettings &key, const QString &name, UInt32 &value) {
    bool ok;
    const qulonglong number = key.value(name).toULongLong(&ok);
    if (!ok || number > UInt32(-1))
        return false;
    value = UInt32(number);
    return true;
}

// QSettings serializes bools as text in INI files; native registry values are
// DWORDs. Accept both representations without changing undefined-value defaults.
static bool Key_Get_bool_IfOk(QSettings &key, const QString &name, bool &value) {
    const QVariant stored = key.value(name);
    const QString text = stored.toString();
    if (text == QStringLiteral("true") || text == QStringLiteral("false")) {
        value = text == QStringLiteral("true");
        return true;
    }
    UInt32 number = 0;
    const bool ok = Key_Get_UInt32_IfOk(key, name, number);
    if (ok)
        value = number != 0;
    return ok;
}

// Native CContextMenuInfo defaults and defined-value branches. The explicit
// application settings file replaces the process-wide Windows registry key.
void CContextMenuInfo::Save(const QString &path) const {
    QMutexLocker lock(&g_CS);
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup("Options");
    const auto setPair = [&key](const QString &name, const CBoolPair &pair) {
        if (pair.Def)
            key.setValue(name, pair.Val);
    };
    setPair("CascadedMenu", Cascaded);
    setPair("MenuIcons", MenuIcons);
    setPair("ElimDupExtract", ElimDup);
    if (WriteZone == (UInt32)(Int32)-1)
        key.remove("WriteZoneIdExtract");
    else
        key.setValue("WriteZoneIdExtract", WriteZone);
    if (Flags_Def)
        key.setValue("ContextMenu", Flags);
}

void CContextMenuInfo::Load(const QString &path) {
    QMutexLocker lock(&g_CS);
    Cascaded.Val = true;
    Cascaded.Def = false;
    MenuIcons.Val = false;
    MenuIcons.Def = false;
    ElimDup.Val = true;
    ElimDup.Def = false;
    WriteZone = (UInt32)(Int32)-1;
    Flags = (UInt32)(Int32)-1;
    Flags_Def = false;
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup("Options");
    const auto getPair = [&key](const QString &name, CBoolPair &pair) {
        pair.Def = Key_Get_bool_IfOk(key, name, pair.Val);
    };
    getPair("CascadedMenu", Cascaded);
    getPair("ElimDupExtract", ElimDup);
    getPair("MenuIcons", MenuIcons);
    Key_Get_UInt32_IfOk(key, "WriteZoneIdExtract", WriteZone);
    Flags_Def = Key_Get_UInt32_IfOk(key, "ContextMenu", Flags);
}

// Registry BoolPair services adapted to the same explicit QSettings key.
static void Key_Set_BoolPair(QSettings &key, const char *name, const CBoolPair &pair) {
    if (pair.Def)
        key.setValue(QLatin1String(name), pair.Val);
}
static void Key_Set_BoolPair_Delete_IfNotDef(QSettings &key, const char *name,
                                             const CBoolPair &pair) {
    if (pair.Def)
        key.setValue(QLatin1String(name), pair.Val);
    else
        key.remove(QLatin1String(name));
}
static void Key_Get_BoolPair(QSettings &key, const char *name, CBoolPair &pair) {
    pair.Def = false;
    pair.Val = false;
    pair.Def = Key_Get_bool_IfOk(key, QLatin1String(name), pair.Val);
}
static void Key_Get_BoolPair_true(QSettings &key, const char *name, CBoolPair &pair) {
    pair.Val = true;
    pair.Def = Key_Get_bool_IfOk(key, QLatin1String(name), pair.Val);
}

// Source DWORD sentinel policy is shared by Extract and Compression.
static void Key_Set_UInt32(QSettings &key, const char *name, UInt32 value) {
    if (value == (UInt32)(Int32)-1)
        key.remove(QLatin1String(name));
    else
        key.setValue(QLatin1String(name), value);
}

namespace NWorkDir {
void CInfo::Save(const QString &path) const {
    QMutexLocker lock(&g_CS);
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup(QStringLiteral("Options"));
    key.setValue(QStringLiteral("WorkDirType"), (UInt32)Mode);
    key.setValue(QStringLiteral("WorkDirPath"), QString::fromWCharArray(fs2us(Path).Ptr()));
    key.setValue(QStringLiteral("TempRemovableOnly"), ForRemovableOnly);
}
void CInfo::Load(const QString &path) {
    QMutexLocker lock(&g_CS);
    SetDefault();
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup(QStringLiteral("Options"));
    UInt32 dirType;
    if (!Key_Get_UInt32_IfOk(key, QStringLiteral("WorkDirType"), dirType))
        return;
    switch (dirType) {
    case NMode::kSystem:
    case NMode::kCurrent:
    case NMode::kSpecified:
        Mode = (NMode::EEnum)dirType;
    }
    if (key.contains(QStringLiteral("WorkDirPath"))) {
        const auto value = key.value(QStringLiteral("WorkDirPath")).toString().toStdWString();
        Path = us2fs(value.c_str());
    } else {
        Path.Empty();
        if (Mode == NMode::kSpecified)
            Mode = NMode::kSystem;
    }
    Key_Get_bool_IfOk(key, QStringLiteral("TempRemovableOnly"), ForRemovableOnly);
}
} // namespace NWorkDir

namespace NCompression {
static bool ParseMemUse(const wchar_t *s, CMemUse &mu) {
    mu.Clear();

    bool percentMode = false;
    {
        const wchar_t c = *s;
        if (MyCharLower_Ascii(c) == 'p') {
            percentMode = true;
            s++;
        }
    }
    const wchar_t *end;
    UInt64 number = ConvertStringToUInt64(s, &end);
    if (end == s)
        return false;

    wchar_t c = *end;

    if (percentMode) {
        if (c != 0)
            return false;
        mu.IsPercent = true;
        mu.Val = number;
        return true;
    }

    if (c == 0) {
        mu.Val = number;
        return true;
    }

    c = MyCharLower_Ascii(c);

    const wchar_t c1 = end[1];

    if (c == '%') {
        if (c1 != 0)
            return false;
        mu.IsPercent = true;
        mu.Val = number;
        return true;
    }

    if (c == 'b') {
        if (c1 != 0)
            return false;
        mu.Val = number;
        return true;
    }

    if (c1 != 0)
        if (MyCharLower_Ascii(c1) != 'b' || end[2] != 0)
            return false;

    unsigned numBits;
    switch (c) {
    case 'k':
        numBits = 10;
        break;
    case 'm':
        numBits = 20;
        break;
    case 'g':
        numBits = 30;
        break;
    case 't':
        numBits = 40;
        break;
    default:
        return false;
    }
    if (number >= ((UInt64)1 << (64 - numBits)))
        return false;
    mu.Val = number << numBits;
    return true;
}

void CMemUse::Parse(const QString &s) { IsDefined = ParseMemUse(s.toStdWString().c_str(), *this); }

static const char *const kOptionsKeyName = "Compression/Options";
static const char *const kMemUse = sizeof(size_t) == 4 ? "MemUse32" : "MemUse64";

static void SetRegString(QSettings &key, const char *name, const QString &value) {
    if (value.isEmpty())
        key.remove(QLatin1String(name));
    else
        key.setValue(QLatin1String(name), value);
}
static void Key_Get_UInt32(QSettings &key, const char *name, UInt32 &value) {
    value = (UInt32)(Int32)-1;
    Key_Get_UInt32_IfOk(key, QLatin1String(name), value);
}

void CInfo::Save(const QString &path) const {
    QMutexLocker lock(&g_CS);
    QSettings optionsKey(path, QSettings::IniFormat);
    optionsKey.setValue("Compression/Archiver", ArcType);
    optionsKey.setValue("Compression/ShowPassword", ShowPassword);
    optionsKey.setValue("Compression/EncryptHeaders", EncryptHeaders);
    optionsKey.setValue("Compression/Level", Level);
    optionsKey.beginGroup("Compression");
    Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "Security", NtSecurity);
    Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "AltStreams", AltStreams);
    Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "HardLinks", HardLinks);
    Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "SymLinks", SymLinks);
    Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "PreserveATime", PreserveATime);
    optionsKey.endGroup();
    QStringList arcPaths;
    FOR_VECTOR(i, ArcPaths)
    arcPaths.append(ArcPaths[i]);
    optionsKey.setValue("Compression/ArcHistory", arcPaths);
    optionsKey.beginGroup(QLatin1String(kOptionsKeyName));
    // Only this ported options group is replaced. Other application settings survive.
    optionsKey.remove(QString());
    FOR_VECTOR(i, Formats) {
        const CFormatOptions &fo = Formats[i];
        optionsKey.beginGroup(fo.FormatID);
        SetRegString(optionsKey, "Method", fo.Method);
        SetRegString(optionsKey, "Options",
                     QString::fromWCharArray(fo.Options.Ptr(), fo.Options.Len()));
        SetRegString(optionsKey, "EncryptionMethod", fo.EncryptionMethod);
        SetRegString(optionsKey, kMemUse, fo.MemUse);
        Key_Set_UInt32(optionsKey, "Level", fo.Level);
        Key_Set_UInt32(optionsKey, "Dictionary", fo.Dictionary);
        Key_Set_UInt32(optionsKey, "Order", fo.Order);
        Key_Set_UInt32(optionsKey, "BlockSize", fo.BlockLogSize);
        Key_Set_UInt32(optionsKey, "NumThreads", fo.NumThreads);
        Key_Set_UInt32(optionsKey, "TimePrec", fo.TimePrec);
        Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "MTime", fo.MTime);
        Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "ATime", fo.ATime);
        Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "CTime", fo.CTime);
        Key_Set_BoolPair_Delete_IfNotDef(optionsKey, "SetArcMTime", fo.SetArcMTime);
        optionsKey.endGroup();
    }
}

void CInfo::Load(const QString &path) {
    QMutexLocker lock(&g_CS);
    ArcPaths.Clear();
    Formats.Clear();
    Level = 5;
    ArcType = QStringLiteral("7z");
    ShowPassword = false;
    EncryptHeaders = false;
    QSettings optionsKey(path, QSettings::IniFormat);
    Key_Get_bool_IfOk(optionsKey, "Compression/ShowPassword", ShowPassword);
    Key_Get_bool_IfOk(optionsKey, "Compression/EncryptHeaders", EncryptHeaders);
    Key_Get_UInt32_IfOk(optionsKey, "Compression/Level", Level);
    optionsKey.beginGroup("Compression");
    Key_Get_BoolPair(optionsKey, "Security", NtSecurity);
    Key_Get_BoolPair(optionsKey, "AltStreams", AltStreams);
    Key_Get_BoolPair(optionsKey, "HardLinks", HardLinks);
    Key_Get_BoolPair(optionsKey, "SymLinks", SymLinks);
    Key_Get_BoolPair(optionsKey, "PreserveATime", PreserveATime);
    optionsKey.endGroup();
    if (optionsKey.contains("Compression/Archiver"))
        ArcType = optionsKey.value("Compression/Archiver").toString();
    for (const QString &s : optionsKey.value("Compression/ArcHistory").toStringList())
        ArcPaths.Add(s);
    optionsKey.beginGroup(QLatin1String(kOptionsKeyName));
    const QStringList formatIDs = optionsKey.childGroups();
    for (int i = 0; i < formatIDs.size(); i++) {
        CFormatOptions fo;
        fo.FormatID = formatIDs[i];
        optionsKey.beginGroup(fo.FormatID);
        fo.Method = optionsKey.value("Method").toString();
        const auto parameters = optionsKey.value("Options").toString().toStdWString();
        fo.Options.SetFrom(parameters.data(), unsigned(parameters.size()));
        fo.EncryptionMethod = optionsKey.value("EncryptionMethod").toString();
        fo.MemUse = optionsKey.value(QLatin1String(kMemUse)).toString();
        Key_Get_UInt32(optionsKey, "Level", fo.Level);
        Key_Get_UInt32(optionsKey, "Dictionary", fo.Dictionary);
        Key_Get_UInt32(optionsKey, "Order", fo.Order);
        Key_Get_UInt32(optionsKey, "BlockSize", fo.BlockLogSize);
        Key_Get_UInt32(optionsKey, "NumThreads", fo.NumThreads);
        Key_Get_UInt32(optionsKey, "TimePrec", fo.TimePrec);
        Key_Get_BoolPair(optionsKey, "MTime", fo.MTime);
        Key_Get_BoolPair(optionsKey, "ATime", fo.ATime);
        Key_Get_BoolPair(optionsKey, "CTime", fo.CTime);
        Key_Get_BoolPair(optionsKey, "SetArcMTime", fo.SetArcMTime);
        Formats.Add(fo);
        optionsKey.endGroup();
    }
}
} // namespace NCompression

namespace NExtract {
UInt32 Read_LimitGB(const QString &path) {
    QMutexLocker lock(&g_CS);
    UInt32 v = (UInt32)(Int32)-1;
    if (path.isEmpty())
        return v;
    QSettings key(path, QSettings::IniFormat);
    Key_Get_UInt32_IfOk(key, "Extraction/MemLimit", v);
    return v;
}

void Save_LimitGB(const QString &path, UInt32 limit_GB) {
    QMutexLocker lock(&g_CS);
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup("Extraction");
    Key_Set_UInt32(key, "MemLimit", limit_GB);
}

bool Read_ShowPassword(const QString &path) {
    QMutexLocker lock(&g_CS);
    bool showPassword = false;
    if (path.isEmpty())
        return showPassword;
    QSettings key(path, QSettings::IniFormat);
    Key_Get_bool_IfOk(key, "Extraction/ShowPassword", showPassword);
    return showPassword;
}

void Save_ShowPassword(const QString &path, bool showPassword) {
    QMutexLocker lock(&g_CS);
    if (path.isEmpty())
        return;
    QSettings key(path, QSettings::IniFormat);
    key.setValue("Extraction/ShowPassword", showPassword);
}

void CInfo::Save(const QString &path) const {
    QMutexLocker lock(&g_CS);
    QSettings key(path, QSettings::IniFormat);
    key.beginGroup("Extraction");
    if (PathMode_Force)
        key.setValue("ExtractMode", UInt32(PathMode));
    if (OverwriteMode_Force)
        key.setValue("OverwriteMode", UInt32(OverwriteMode));
    Key_Set_BoolPair(key, "SplitDest", SplitDest);
    Key_Set_BoolPair(key, "ShowPassword", ShowPassword);
    Key_Set_BoolPair(key, "ElimDup", ElimDup);
    Key_Set_BoolPair(key, "Security", NtSecurity);
    QStringList paths;
    FOR_VECTOR(i, Paths)
    paths.append(Paths[i]);
    key.setValue("PathHistory", paths);
}

void CInfo::Load(const QString &path) {
    QMutexLocker lock(&g_CS);
    PathMode = NPathMode::kCurPaths;
    PathMode_Force = false;
    OverwriteMode = NOverwriteMode::kAsk;
    OverwriteMode_Force = false;
    SplitDest.Val = true;
    SplitDest.Def = false;
    ShowPassword.Init();
    Paths.Clear();
    QSettings key(path, QSettings::IniFormat);
    // Migration of the former Qt history; all further writes use the upstream group.
    const QStringList legacyPaths = key.value("history/extract").toStringList();
    key.beginGroup("Extraction");
    const QStringList paths = key.value("PathHistory", legacyPaths).toStringList();
    Key_Get_BoolPair(key, "ElimDup", ElimDup);
    Key_Get_BoolPair(key, "Security", NtSecurity);
    for (const QString &s : paths)
        Paths.Add(s);
    UInt32 v;
    if (Key_Get_UInt32_IfOk(key, "ExtractMode", v) && v <= NPathMode::kAbsPaths) {
        PathMode = (NPathMode::EEnum)v;
        PathMode_Force = true;
    }
    if (Key_Get_UInt32_IfOk(key, "OverwriteMode", v) && v <= NOverwriteMode::kRenameExisting) {
        OverwriteMode = (NOverwriteMode::EEnum)v;
        OverwriteMode_Force = true;
    }
    Key_Get_BoolPair_true(key, "SplitDest", SplitDest);
    Key_Get_BoolPair(key, "ShowPassword", ShowPassword);
}
} // namespace NExtract
