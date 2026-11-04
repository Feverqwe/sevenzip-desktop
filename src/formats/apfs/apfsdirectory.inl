// Project directory integration around the authoritative SDK APFS decoders.
// Included in the reader's private namespace; no parallel filesystem decoder.
HRESULT CDatabase::ResolveObjectMap(CObjectMap &omap, oid_t oid, CVol *vol, omap_val &value) {
    if (!omap.LazyRoot) {
        const int index = omap.FindKey(oid);
        if (index < 0)
            return S_FALSE;
        value = omap.Vals[(unsigned)index];
        return S_OK;
    }
    const auto cached = omap.LazyValues.find(oid);
    if (cached != omap.LazyValues.end()) {
        value = cached->second;
        return value.flags & OMAP_VAL_DELETED ? S_FALSE : S_OK;
    }
    CMap query;
    query.Subtype = OBJECT_TYPE_OMAP;
    query.HasRange = true;
    query.FirstId = query.LastId = oid;
    RINOK(ReadMap(omap.LazyRoot, false, vol, NULL, query, 0))
    CObjectMap selected;
    if (!selected.Parse(query.Pairs, omap.LazyXid))
        return S_FALSE;
    const int index = selected.FindKey(oid);
    if (index < 0)
        return S_FALSE;
    value = selected.Vals[(unsigned)index];
    omap.LazyValues.emplace(oid, value);
    return S_OK;
}

HRESULT CDatabase::ReadDirectoryPairs(CMap &map, CVol &vol, UInt64 directory, UInt64 root,
                                      bool noHeader, const Byte *hash) {
    std::map<UInt64, CObjectVector<CKeyValPair>> groups;
    std::set<UInt64> pending{directory};
    while (!pending.empty()) {
        const UInt64 id = *pending.begin();
        pending.erase(pending.begin());
        if (groups.find(id) != groups.end())
            continue;
        map.Pairs.Clear();
        map.NumNodes = 0;
        map.HasRange = true;
        map.FirstId = map.LastId = id;
        RINOK(ReadMap(root, noHeader, &vol, hash, map, 0))
        auto &records = groups[id];
        FOR_VECTOR(i, map.Pairs) {
            const auto &pair = map.Pairs[i];
            if (pair.Key.Size() < 8)
                return S_FALSE;
            j_key_t key;
            key.Parse(pair.Key);
            if (key.GetType() == APFS_TYPE_DIR_REC) {
                if (id != directory)
                    continue;
                if (pair.Val.Size() < 8)
                    return S_FALSE;
                pending.insert(Get64(pair.Val));
            }
            if (key.GetType() == APFS_TYPE_XATTR && pair.Val.Size() == 12 + k_SizeOf_j_dstream &&
                (Get16(pair.Val) & XATTR_DATA_STREAM) && !(Get16(pair.Val) & XATTR_DATA_EMBEDDED))
                pending.insert(Get64(pair.Val + 4));
            records.Add(pair);
        }
    }
    map.Pairs.Clear();
    for (const auto &group : groups)
        FOR_VECTOR(i, group.second)
    map.Pairs.Add(group.second[i]);
    return S_OK;
}

bool CDatabase::IsDirectoryIndex(UInt32 index) const {
    if (index == UINT32_MAX)
        return true;
    if (index >= Refs2.Size())
        return false;
    const auto &entry = Refs2[index];
    if (IsViNotDef(entry.RefIndex))
        return true;
    const auto &volume = Vols[entry.VolIndex];
    const auto &ref = volume.Refs[entry.RefIndex];
    if (ref.IsAltStream())
        return false;
    return IsViDef(ref.NodeIndex)
               ? volume.Nodes[ref.NodeIndex].IsDir()
               : IsViDef(ref.ItemIndex) && volume.Items[ref.ItemIndex].Val.IsFlags_Dir();
}

void CDatabase::IndexDirectoryChildren(unsigned first) {
    for (unsigned i = first; i < Refs2.Size(); ++i) {
        DirectoryItems[Refs2[i].DirectoryParent].Add(i);
        if (IsDirectoryIndex(i))
            DirectoryChildren[Refs2[i].DirectoryParent].Add(i);
    }
}

Z7_COM7F_IMF(CHandler::GetDirectoryItems(UInt32 parent, UInt32 *count, UInt32 *items)) {
    if (!count || !_stream || !IsDirectoryIndex(parent))
        return E_INVALIDARG;
    if (!DirectoryMode)
        return E_NOTIMPL;
    if (!LoadedDirectories.count(parent))
        return E_FAIL;
    const auto found = DirectoryItems.find(parent);
    const UInt32 required = found == DirectoryItems.end() ? 0 : found->second.Size();
    const UInt32 capacity = *count;
    *count = required;
    if (!items)
        return S_OK;
    if (capacity < required)
        return E_INVALIDARG;
    for (UInt32 i = 0; i < required; ++i)
        items[i] = found->second[i];
    return S_OK;
}
Z7_COM7F_IMF(CHandler::GetDirectoryInputStream(IInStream **stream)) {
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    if (!_stream)
        return E_FAIL;
    CMyComPtr<IInStream> input = _stream;
    *stream = input.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CHandler::SetDirectoryMode(BoolInt enabled)) {
    if (_stream)
        return E_FAIL;
    DirectoryMode = enabled != 0;
    return S_OK;
}
Z7_COM7F_IMF(CHandler::IsDirectoryLoaded(UInt32 parent, BoolInt recursive, BoolInt *loaded)) {
    if (!loaded)
        return E_INVALIDARG;
    *loaded = false;
    if (!_stream)
        return E_FAIL;
    if (!IsDirectoryIndex(parent))
        return E_INVALIDARG;
    *loaded = !DirectoryMode || (recursive ? CompleteDirectories.count(parent)
                                           : LoadedDirectories.count(parent)) != 0;
    return S_OK;
}

namespace {
// The SDK cache invalidates failed fills. Scope it to one directory operation:
// a retry does not inherit this operation's cached pages. Repeated B-tree paths
// during a large recursive walk do not repeatedly hit the network.
class CDirectoryCache final : public CCachedInStream {
    CMyComPtr<IInStream> Input;
    unsigned BlockBits;
    HRESULT ReadBlock(UInt64 index, Byte *data, size_t size) override {
        RINOK(InStream_SeekSet(Input, index << BlockBits))
        return ReadStream_FALSE(Input, data, size);
    }

  public:
    CDirectoryCache(IInStream *input, unsigned blockBits) : Input(input), BlockBits(blockBits) {}
};

class CDirectoryProgress final : public IArchiveOpenCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveOpenCallback)
    IProgress *progress_;

  public:
    explicit CDirectoryProgress(IProgress *progress) : progress_(progress) {}
    Z7_COM7F_IMF(SetTotal(const UInt64 *files, const UInt64 *bytes)) override {
        return progress_ ? progress_->SetTotal(files ? *files : bytes ? *bytes : 0) : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *files, const UInt64 *bytes)) override {
        return progress_ ? progress_->SetCompleted(files ? files : bytes) : S_OK;
    }
};
} // namespace

Z7_COM7F_IMF(CHandler::LoadDirectory(UInt32 parent, BoolInt recursive, IProgress *progress)) {
    COM_TRY_BEGIN
    if (!_stream || !IsDirectoryIndex(parent))
        return E_INVALIDARG;
    if (!DirectoryMode)
        return S_OK;
    if (recursive && CompleteDirectories.count(parent))
        return S_OK;
    if (!recursive && LoadedDirectories.count(parent))
        return S_OK;
    CMyComPtr2<IInStream, CDirectoryCache> cache;
    cache.SetFromCls(new CDirectoryCache(OpenInStream, sb.block_size_Log));
    if (!cache->Alloc(sb.block_size_Log, 22 - sb.block_size_Log))
        return E_OUTOFMEMORY;
    cache->Init(sb.block_count << sb.block_size_Log);
    CMyComPtr<IArchiveOpenCallback> callback = new CDirectoryProgress(progress);
    struct Restore {
        CHandler &Handler;
        IInStream *Input;
        Restore(CHandler &handler, IInStream *input) : Handler(handler), Input(input) {}
        ~Restore() {
            Handler.OpenCallback = NULL;
            Handler.DirectoryMapStream = Input;
        }
    } restore{*this, DirectoryMapStream};
    DirectoryMapStream = cache;
    OpenCallback = callback;
    CUIntVector pending;
    pending.Add(parent);
    for (unsigned next = 0; next < pending.Size(); ++next) {
        if (progress)
            RINOK(progress->SetCompleted(NULL))
        const UInt32 directory = pending[next];
        if (LoadedDirectories.count(directory) == 0) {
            const auto entry = Refs2[directory];
            const auto &volume = Vols[entry.VolIndex];
            const auto &ref = volume.Refs[entry.RefIndex];
            if (IsViNotDef(ref.NodeIndex))
                return S_FALSE;
            const UInt64 id = volume.NodeIDs[ref.NodeIndex];
            const oid_t fsOid = volume.DirectoryFsOid;
            UInt32 ancestor = entry.DirectoryParent;
            for (unsigned level = 0; ancestor != UINT32_MAX; ++level) {
                if (level > 1024 || ancestor >= Refs2.Size())
                    return S_FALSE;
                const auto &a = Refs2[ancestor];
                const auto &v = Vols[a.VolIndex];
                if (v.DirectoryFsOid == fsOid && IsViDef(a.RefIndex)) {
                    const auto &r = v.Refs[a.RefIndex];
                    if (IsViDef(r.NodeIndex) && v.NodeIDs[r.NodeIndex] == id)
                        return S_FALSE;
                }
                ancestor = a.DirectoryParent;
            }
            const unsigned oldVolumes = Vols.Size(), oldRefs = Refs2.Size();
            struct Rollback {
                CHandler &Handler;
                unsigned Volumes, Refs;
                UInt32 Directory;
                bool Commit = false;
                Rollback(CHandler &handler, unsigned volumes, unsigned refs, UInt32 directory)
                    : Handler(handler), Volumes(volumes), Refs(refs), Directory(directory) {}
                ~Rollback() {
                    if (!Commit) {
                        Handler.Vols.DeleteFrom(Volumes);
                        Handler.Refs2.DeleteFrom(Refs);
                        Handler.LoadedDirectories.erase(Directory);
                        Handler.DirectoryChildren.erase(Directory);
                        Handler.DirectoryItems.erase(Directory);
                    }
                }
            } rollback{*this, oldVolumes, oldRefs, directory};
            CObjectMap omap;
            RINOK(ReadObjectMap(sb.omap_oid, NULL, omap))
            DirectoryId = id;
            RINOK(OpenVolume(omap, fsOid))
            auto &slice = Vols.Back();
            slice.StartRef2Index = oldRefs;
            for (unsigned i = 0; i < slice.Refs.Size(); ++i) {
                CRef2 item;
                item.VolIndex = oldVolumes;
                item.RefIndex = i;
                const auto localParent = slice.Refs[i].ParentRefIndex;
                item.DirectoryParent = IsViDef(localParent) ? oldRefs + localParent : directory;
                Refs2.Add(item);
            }
            LoadedDirectories.insert(directory);
            IndexDirectoryChildren(oldRefs);
            rollback.Commit = true;
        }
        if (recursive) {
            const auto children = DirectoryChildren.find(directory);
            if (children != DirectoryChildren.end())
                FOR_VECTOR(i, children->second)
            pending.Add(children->second[i]);
        }
    }
    if (recursive)
        for (unsigned i = 0; i < pending.Size(); ++i)
            CompleteDirectories.insert(pending[i]);
    return S_OK;
    COM_TRY_END
}
