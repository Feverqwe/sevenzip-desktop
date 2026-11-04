#include "Common/ComTry.h"
#include "archive/agent/Agent.h"
#include "archive/opening/archivedirectoryaccess.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include <map>
#include <memory>
#include <utility>

namespace {
CMyComPtr<IArchiveDirectoryAccess> Access(const CAgentFolder &folder) {
    auto *resource = dynamic_cast<QtArchiveFileResource *>(folder.GetEngineResource());
    CMyComPtr<IArchiveDirectoryAccess> access;
    if (resource && resource->NativeItemProperties())
        resource->NativeItemProperties()->QueryInterface(IID_IArchiveDirectoryAccess,
                                                         (void **)&access);
    return access;
}
} // namespace

bool CAgentFolder::DirectoryItemsNeedLoad(bool recursive) const {
    if (!IsBound() || !_proxy2 || _proxy2->IsAltDir(_proxyDirIndex))
        return false;
    auto access = Access(*this);
    if (!access)
        return false;
    auto *resource = static_cast<QtArchiveFileResource *>(GetEngineResource());
    UInt32 count = 0;
    if (resource->NativeItemProperties()->GetNumberOfItems(&count) == S_OK &&
        count != _proxy2->NumFiles)
        return true;
    BoolInt loaded = false;
    return access->IsDirectoryLoaded(UInt32(_proxy2->Dirs[_proxyDirIndex].ArcIndex), recursive,
                                     &loaded) == S_OK &&
           !loaded;
}

HRESULT CAgentFolder::LoadDirectoryItems(bool recursive, IProgress *progress) {
    COM_TRY_BEGIN
    if (!IsBound() || !_proxy2 || _proxy2->IsAltDir(_proxyDirIndex))
        return S_OK;
    auto access = Access(*this);
    if (!access)
        return S_OK;
    auto *resource = static_cast<QtArchiveFileResource *>(GetEngineResource());
    const auto reader = resource->NativeReaderLease();
    if (!reader)
        return E_FAIL;
    ArchiveNativeReadScope read(reader, progress);
    RINOK(
        access->LoadDirectory(UInt32(_proxy2->Dirs[_proxyDirIndex].ArcIndex), recursive, progress))
    UInt32 count = 0;
    RINOK(resource->NativeItemProperties()->GetNumberOfItems(&count))
    if (count != _proxy2->NumFiles)
        RINOK(_agentSpec->RefreshDirectoryProxy(progress))
    return S_OK;
    COM_TRY_END
}

bool CAgentFolder::DirectoryItemSubtreeComplete(UInt32 nativeIndex) const {
    auto access = Access(*this);
    if (!access)
        return true;
    // A retained export view can expand the shared reader before this panel
    // republishes its proxy. Do not expose that proxy's partial sums as final.
    auto *resource = static_cast<QtArchiveFileResource *>(GetEngineResource());
    UInt32 count = 0;
    if (!_proxy2 || resource->NativeItemProperties()->GetNumberOfItems(&count) != S_OK ||
        count != _proxy2->NumFiles)
        return false;
    BoolInt loaded = false;
    return access->IsDirectoryLoaded(nativeIndex, true, &loaded) == S_OK && loaded;
}

HRESULT CAgentFolder::LoadDirectorySelection(const UInt32 *indices, UInt32 count,
                                             IProgress *progress) {
    COM_TRY_BEGIN
    if (!_proxy2)
        return S_OK;
    auto access = Access(*this);
    if (!access)
        return S_OK;
    CUIntVector directories;
    for (UInt32 i = 0; i < count; ++i) {
        const int native = GetRealIndex(indices[i]);
        if (native < 0)
            return E_INVALIDARG;
        if (_proxy2->Files[unsigned(native)].IsDir())
            directories.Add(unsigned(native));
    }
    if (directories.IsEmpty())
        return S_OK;
    auto *resource = static_cast<QtArchiveFileResource *>(GetEngineResource());
    ArchiveNativeReadScope read(resource->NativeReaderLease(), progress);
    for (unsigned i = 0; i < directories.Size(); ++i)
        RINOK(access->LoadDirectory(directories[i], true, progress))
    UInt32 nativeCount = 0;
    RINOK(resource->NativeItemProperties()->GetNumberOfItems(&nativeCount))
    if (nativeCount == _proxy2->NumFiles)
        return S_OK;
    return _agentSpec->RefreshDirectoryProxy(progress);
    COM_TRY_END
}

HRESULT CAgent::RefreshDirectoryProxy(IProgress *progress) {
    if (!_proxy2)
        return E_FAIL;
    auto fresh = std::make_unique<CProxyArc2>();
    fresh->MemUsage_Limit = _proxy2->MemUsage_Limit;
    _archiveLink.Arcs.Back().Ask_AltStream = true;
    RINOK(fresh->Load(GetArc(), progress))
    // Preserve every published folder index, including alternate-stream
    // folders. New native items append, but a fresh proxy groups directories
    // and ADS separately and would otherwise renumber existing ADS folders.
    using Key = std::pair<int, bool>;
    std::map<Key, unsigned> old;
    for (unsigned i = 0; i < _proxy2->Dirs.Size(); ++i)
        old.emplace(Key{_proxy2->Dirs[i].ArcIndex, _proxy2->IsAltDir(i)}, i);
    CUIntVector remap;
    remap.ClearAndSetSize(fresh->Dirs.Size());
    unsigned next = _proxy2->Dirs.Size();
    for (unsigned i = 0; i < fresh->Dirs.Size(); ++i) {
        const auto found = old.find(Key{fresh->Dirs[i].ArcIndex, fresh->IsAltDir(i)});
        remap[i] = found == old.end() ? next++ : found->second;
    }
    if (next != fresh->Dirs.Size())
        return E_FAIL;
    CObjectVector<CProxyDir2> ordered;
    for (unsigned i = 0; i < fresh->Dirs.Size(); ++i)
        ordered.AddNew();
    for (unsigned i = 0; i < fresh->Dirs.Size(); ++i)
        ordered[remap[i]] = fresh->Dirs[i];
    for (unsigned i = 0; i < fresh->NumFiles; ++i) {
        auto &file = fresh->Files[i];
        if (file.DirIndex >= 0)
            file.DirIndex = int(remap[unsigned(file.DirIndex)]);
        if (file.AltDirIndex >= 0)
            file.AltDirIndex = int(remap[unsigned(file.AltDirIndex)]);
    }
    fresh->Dirs.Swap(ordered);
    _proxy2->Dirs.Swap(fresh->Dirs);
    std::swap(_proxy2->Files, fresh->Files);
    std::swap(_proxy2->NumFiles, fresh->NumFiles);
    std::swap(_proxy2->MemUsage, fresh->MemUsage);
    return S_OK;
}
