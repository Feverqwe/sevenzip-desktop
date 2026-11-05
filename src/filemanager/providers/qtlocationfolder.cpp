#include "filemanager/providers/qtlocationfolder.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "archive/agent/Agent.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/patterns/Wildcard.h"
#include <QDir>
#include <QFile>

int QtArchiveDirectoryIndex(const CProxyArc &proxy, const QString &path) {
    if (proxy.Dirs.IsEmpty())
        return -1;
    unsigned dirIndex = k_Proxy_RootDirIndex;
    UStringVector parts;
    SplitPathToParts(ToUString(path), parts);
    if (path.isEmpty())
        return int(dirIndex);
    FOR_VECTOR(i, parts) {
        const int next = proxy.FindSubDir(dirIndex, parts[i]);
        if (next < 0)
            return -1;
        dirIndex = unsigned(next);
    }
    return int(dirIndex);
}

QString QtFilesystemPath(const FString &path) {
#ifdef _WIN32
    return QDir::fromNativeSeparators(QString::fromWCharArray(path));
#else
    // Native source bytes remain unchanged; Qt supplies its POSIX decoding
    // (including macOS NFD → NFC) at the presentation boundary.
    return QFile::decodeName(path.Ptr());
#endif
}

HRESULT CreateFilesystemFolder(const QString &path, IFolderFolder **resultFolder) {
    COM_TRY_BEGIN
    *resultFolder = nullptr;
    if (path.isEmpty())
        return E_INVALIDARG;
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    FString fullPath;
    if (!NWindows::NFile::NName::GetFullPath(us2fs(native.c_str()), fullPath))
        return E_FAIL;
    NWindows::NFile::NName::NormalizeDirPathPrefix(fullPath);
    auto *folderSpec = new NFsFolder::CFSFolder;
    CMyComPtr<IFolderFolder> folder = folderSpec;
    RINOK(folderSpec->Init(fullPath))
    *resultFolder = folder.Detach();
    return S_OK;
    COM_TRY_END
}

HRESULT CreateArchiveMetadata(const QList<ArchiveEntry> &entries, IFolderFolder **metadata) {
    COM_TRY_BEGIN
    if (!metadata)
        return E_INVALIDARG;
    *metadata = nullptr;
    CObjectVector<CPanelItem> items;
    for (const ArchiveEntry &entry : entries) {
        CPanelItem item;
        item.RelPath = entry.path;
        item.MTimeText = entry.modified;
        item.IsFolder = entry.directory;
        item.Crc = entry.crc;
        for (auto i = entry.rawProperties.cbegin(); i != entry.rawProperties.cend(); ++i)
            item.RawProperties.insert(PROPID(i.key()), i.value());
        for (auto i = entry.properties.cbegin(); i != entry.properties.cend(); ++i)
            item.ArchiveProperties.Add({ToUString(i.key()), ToUString(i.value())});
        item.SizeDefined = entry.sizeValue.has_value() || entry.size >= 0;
        if (item.SizeDefined)
            item.Size = entry.sizeValue.value_or(UInt64(entry.size));
        item.PackSizeDefined = entry.packedSizeValue.has_value() || entry.packedSize >= 0;
        if (item.PackSizeDefined)
            item.PackSize = entry.packedSizeValue.value_or(UInt64(entry.packedSize));
        items.Add(item);
    }
    CMyComPtr<IFolderFolder> props =
        new QtPanelFolder(QString(), items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
    *metadata = props.Detach();
    return S_OK;
    COM_TRY_END
}

HRESULT CreateArchiveFolder(const QList<ArchiveEntry> &entries, const UString &archiveType,
                            IFolderFolder **resultFolder, IUnknown *engineResource) {
    COM_TRY_BEGIN
    if (!resultFolder)
        return E_INVALIDARG;
    *resultFolder = nullptr;
    CMyComPtr<IFolderFolder> props;
    RINOK(CreateArchiveMetadata(entries, &props))
    auto *agentSpec = new CAgent;
    CMyComPtr<IFolderArcProps> agent = agentSpec;
    RINOK(agentSpec->Init(props, archiveType, engineResource))
    agentSpec->AttachEngineResource(engineResource);
    return agentSpec->BindToRootFolder(resultFolder);
    COM_TRY_END
}
