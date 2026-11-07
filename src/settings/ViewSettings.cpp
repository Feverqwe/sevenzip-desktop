// Adapted from 7-Zip CPP/7zip/UI/FileManager/ViewSettings.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// QSettings replaces Registry. Callers supply the INI path; GUI-thread access
// needs no Win32 critical section. Only upstream-named keys are read.
#include "settings/ViewSettings.h"
#include "Common/MyBuffer.h"
#include "platform/cpu/CpuArch.h"

#include <QSettings>

#define Set32(p, v) SetUi32(((Byte *)p), v)
#define SetBool(p, v) Set32(p, ((v) ? 1 : 0))
#define Get32(p, dest) dest = GetUi32((const Byte *)p);
#define Get32_LONG(p, dest) dest = (Int32)GetUi32((const Byte *)p);
#define GetBool(p, dest) dest = (GetUi32(p) != 0);

static const char *const kPositionValueName = "Position";
static const char *const kPanelsInfoValueName = "Panels";
static const UInt32 kWindowPositionHeaderSize = 5 * 4;
static const UInt32 kPanelsInfoHeaderSize = 3 * 4;
static const UInt32 kListViewHeaderSize = 3 * 4;
static const UInt32 kColumnInfoSize = 3 * 4;
static const UInt32 kListViewVersion = 1;

static QString ColumnSettingsKey(const UString &id) {
    return QStringLiteral("Columns/@") + QString::fromLatin1(ToQString(id).toUtf8().toHex());
}

void CListViewInfo::Save(const UString &id, const QString &settingsPath) const {
    const UInt32 dataSize = kListViewHeaderSize + kColumnInfoSize * Columns.Size();
    CByteArr buf(dataSize);
    Set32(buf, kListViewVersion);
    Set32(buf + 4, SortID);
    SetBool(buf + 8, Ascending);
    FOR_VECTOR(i, Columns) {
        const CColumnInfo &column = Columns[i];
        Byte *p = buf + kListViewHeaderSize + i * kColumnInfoSize;
        Set32(p, column.PropID);
        SetBool(p + 4, column.IsVisible);
        Set32(p + 8, column.Width);
    }
    QSettings key(settingsPath, QSettings::IniFormat);
    key.setValue(ColumnSettingsKey(id),
                 QByteArray(reinterpret_cast<const char *>((const Byte *)buf), dataSize));
}

void CListViewInfo::Read(const UString &id, const QString &settingsPath) {
    Clear();
    QSettings key(settingsPath, QSettings::IniFormat);
    const QVariant value = key.value(ColumnSettingsKey(id));
    if (value.metaType().id() != QMetaType::QByteArray)
        return;
    const QByteArray bytes = value.toByteArray();
    unsigned size = unsigned(bytes.size());
    if (size < kListViewHeaderSize)
        return;
    const Byte *buf = reinterpret_cast<const Byte *>(bytes.constData());
    UInt32 version;
    Get32(buf, version);
    if (version != kListViewVersion)
        return;
    Get32(buf + 4, SortID);
    GetBool(buf + 8, Ascending);
    IsLoaded = true;
    size -= kListViewHeaderSize;
    if (size % kColumnInfoSize != 0)
        return;
    if (size > 1000 * kColumnInfoSize)
        return;
    const unsigned numItems = size / kColumnInfoSize;
    Columns.ClearAndReserve(numItems);
    for (unsigned i = 0; i < numItems; i++) {
        CColumnInfo column;
        const Byte *p = buf + kListViewHeaderSize + i * kColumnInfoSize;
        Get32(p, column.PropID);
        GetBool(p + 4, column.IsVisible);
        Get32(p + 8, column.Width);
        Columns.AddInReserved(column);
    }
}

void CWindowInfo::Save(const QString &settingsPath) const {
    QSettings key(settingsPath, QSettings::IniFormat);
    {
        Byte buf[kWindowPositionHeaderSize];
        Set32(buf, (UInt32)rect.left);
        Set32(buf + 4, (UInt32)rect.top);
        Set32(buf + 8, (UInt32)rect.right);
        Set32(buf + 12, (UInt32)rect.bottom);
        SetBool(buf + 16, maximized);
        key.setValue(kPositionValueName,
                     QByteArray(reinterpret_cast<const char *>(buf), kWindowPositionHeaderSize));
    }
    {
        Byte buf[kPanelsInfoHeaderSize];
        Set32(buf, numPanels);
        Set32(buf + 4, currentPanel);
        Set32(buf + 8, splitterPos);
        key.setValue(kPanelsInfoValueName,
                     QByteArray(reinterpret_cast<const char *>(buf), kPanelsInfoHeaderSize));
    }
}

static bool QueryBuf(QSettings &key, const char *name, CByteBuffer &buf, UInt32 dataSize) {
    const QVariant value = key.value(QString::fromLatin1(name));
    if (value.metaType().id() != QMetaType::QByteArray)
        return false;
    const QByteArray bytes = value.toByteArray();
    if (bytes.size() != dataSize)
        return false;
    buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()), dataSize);
    return true;
}

void CWindowInfo::Read(bool &windowPosDefined, bool &panelInfoDefined,
                       const QString &settingsPath) {
    windowPosDefined = false;
    panelInfoDefined = false;
    QSettings key(settingsPath, QSettings::IniFormat);
    CByteBuffer buf;
    if (QueryBuf(key, kPositionValueName, buf, kWindowPositionHeaderSize)) {
        Get32_LONG(buf, rect.left) Get32_LONG(buf + 4, rect.top) Get32_LONG(buf + 8, rect.right)
            Get32_LONG(buf + 12, rect.bottom) GetBool(buf + 16, maximized) windowPosDefined = true;
    }
    if (QueryBuf(key, kPanelsInfoValueName, buf, kPanelsInfoHeaderSize)) {
        Get32(buf, numPanels) Get32(buf + 4, currentPanel) Get32(buf + 8, splitterPos)
            panelInfoDefined = true;
    }
}

static const char *const kCopyHistoryValueName = "CopyHistory";
static const char *const kFolderHistoryValueName = "FolderHistory";
static const char *const kFastFoldersValueName = "FolderShortcuts";

void CListMode::Save(const QString &settingsPath) const {
    UInt32 t = 0;
    for (int i = 0; i < 2; i++)
        t |= (Panels[i] & 0xFF) << (i * 8);
    QSettings key(settingsPath, QSettings::IniFormat);
    key.setValue("ListMode", t);
}

void CListMode::Read(const QString &settingsPath) {
    Init();
    QSettings key(settingsPath, QSettings::IniFormat);
    bool ok = false;
    const qulonglong value = key.value("ListMode").toULongLong(&ok);
    if (!ok || value > 0xFFFFFFFFu)
        return;
    UInt32 t = static_cast<UInt32>(value);
    for (int i = 0; i < 2; i++) {
        // Old Small Icons and List settings fall back to the table.
        Panels[i] = (t & 0xFF) == 0 ? 0 : 3;
        t >>= 8;
    }
}

static UString GetPanelPathName(UInt32 panelIndex) {
    UString s(L"PanelPath");
    s.Add_UInt32(panelIndex);
    return s;
}

void SavePanelPath(UInt32 panel, const UString &path, const QString &settingsPath) {
    QSettings key(settingsPath, QSettings::IniFormat);
    key.setValue(ToQString(GetPanelPathName(panel)), ToQString(path));
}

bool ReadPanelPath(UInt32 panel, UString &path, const QString &settingsPath) {
    QSettings key(settingsPath, QSettings::IniFormat);
    const QString name = ToQString(GetPanelPathName(panel));
    if (!key.contains(name))
        return false;
    path = ToUString(key.value(name).toString());
    return true;
}

static void SaveStringList(const char *valueName, const UStringVector &folders,
                           const QString &settingsPath) {
    QStringList strings;
    FOR_VECTOR(i, folders)
    strings.append(ToQString(folders[i]));
    QSettings key(settingsPath, QSettings::IniFormat);
    key.setValue(valueName, strings);
}

static void ReadStringList(const char *valueName, UStringVector &folders,
                           const QString &settingsPath) {
    folders.Clear();
    QSettings key(settingsPath, QSettings::IniFormat);
    const QStringList strings = key.value(valueName).toStringList();
    for (const QString &s : strings)
        folders.Add(ToUString(s));
}

void SaveFolderHistory(const UStringVector &folders, const QString &settingsPath) {
    SaveStringList(kFolderHistoryValueName, folders, settingsPath);
}

void ReadFolderHistory(UStringVector &folders, const QString &settingsPath) {
    ReadStringList(kFolderHistoryValueName, folders, settingsPath);
}

void SaveFastFolders(const UStringVector &folders, const QString &settingsPath) {
    SaveStringList(kFastFoldersValueName, folders, settingsPath);
}

void ReadFastFolders(UStringVector &folders, const QString &settingsPath) {
    ReadStringList(kFastFoldersValueName, folders, settingsPath);
}

void SaveCopyHistory(const UStringVector &folders, const QString &settingsPath) {
    SaveStringList(kCopyHistoryValueName, folders, settingsPath);
}

void ReadCopyHistory(UStringVector &folders, const QString &settingsPath) {
    ReadStringList(kCopyHistoryValueName, folders, settingsPath);
}

void AddUniqueStringToHeadOfList(UStringVector &list, const UString &s) {
    for (unsigned i = 0; i < list.Size();) {
        if (s.IsEqualTo_NoCase(list[i]))
            list.Delete(i);
        else
            i++;
    }
    list.Insert(0, s);
}
