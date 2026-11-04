// HfsHandler.cpp

#include "7zip/Archive/StdAfx.h"

#include "platform/cpu/CpuArch.h"

#include "Common/ComTry.h"
#include "Common/MyString.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "formats/hfs/hfsreader.h"
#include "settings/pagelink.h"
#include <errno.h>

#include "Windows/PropVariantUtils.h"

#include "7zip/Common/LimitedStreams.h"
#include "7zip/Common/RegisterArc.h"
#include "7zip/Common/StreamObjects.h"
#include "7zip/Common/StreamUtils.h"

#include "formats/hfs/HfsHandler.h"

/* if HFS_SHOW_ALT_STREAMS is defined, the handler will show attribute files
   and resource forks. In most cases it looks useless. So we disable it. */

#define HFS_SHOW_ALT_STREAMS

#define Get16(p) GetBe16(p)
#define Get32(p) GetBe32(p)
#define Get64(p) GetBe64(p)

#define Get16a(p) GetBe16a(p)
#define Get32a(p) GetBe32a(p)

namespace NArchive {
namespace NHfs {

static const char *const kResFileName = "rsrc"; // "com.apple.ResourceFork";

struct CExtent {
    UInt32 Pos;
    UInt32 NumBlocks;
};

struct CIdExtents {
    UInt32 ID;
    UInt32 StartBlock;
    UInt32 EndBlock;
    CRecordVector<CExtent> Extents;
};

struct CFork {
    UInt64 Size;
    UInt32 NumBlocks;
    // UInt32 ClumpSize;
    CRecordVector<CExtent> Extents;

    CFork() : Size(0), NumBlocks(0) {}

    void Parse(const Byte *p);

    bool IsEmpty() const { return Size == 0 && NumBlocks == 0 && Extents.Size() == 0; }

    UInt64 Calc_NumBlocks_from_Extents() const;
    bool Check_NumBlocks() const;

    bool Check_Size_with_NumBlocks(unsigned blockSizeLog) const {
        return Size <= ((UInt64)NumBlocks << blockSizeLog);
    }

    bool IsOk(unsigned blockSizeLog) const {
        // Allocated blocks can extend beyond logical EOF.
        return Check_NumBlocks() && Check_Size_with_NumBlocks(blockSizeLog);
    }

    bool Upgrade(const CObjectVector<CIdExtents> &items, UInt32 id);
    bool UpgradeAndTest(const CObjectVector<CIdExtents> &items, UInt32 id, unsigned blockSizeLog) {
        if (!Upgrade(items, id))
            return false;
        return IsOk(blockSizeLog);
    }
};

static const unsigned kNumFixedExtents = 8;
static const unsigned kForkRecSize = 16 + kNumFixedExtents * 8;

void CFork::Parse(const Byte *p) {
    Extents.Clear();
    Size = Get64(p);
    // ClumpSize = Get32(p + 8);
    NumBlocks = Get32(p + 12);
    p += 16;
    for (unsigned i = 0; i < kNumFixedExtents; i++, p += 8) {
        CExtent e;
        e.Pos = Get32(p);
        e.NumBlocks = Get32(p + 4);
        if (e.NumBlocks != 0)
            Extents.Add(e);
    }
}

UInt64 CFork::Calc_NumBlocks_from_Extents() const {
    UInt64 num = 0;
    FOR_VECTOR(i, Extents)
    num += Extents[i].NumBlocks;
    return num;
}

bool CFork::Check_NumBlocks() const {
    UInt32 num = NumBlocks;
    FOR_VECTOR(i, Extents) {
        const UInt32 cur = Extents[i].NumBlocks;
        if (num < cur)
            return false;
        num -= cur;
    }
    return num == 0;
}

struct CIdIndexPair {
    UInt32 ID;
    unsigned Index;

    int Compare(const CIdIndexPair &a) const;
};

#define RINOZ(x)                                                                                   \
    {                                                                                              \
        const int _t_ = (x);                                                                       \
        if (_t_ != 0)                                                                              \
            return _t_;                                                                            \
    }

int CIdIndexPair::Compare(const CIdIndexPair &a) const {
    RINOZ(MyCompare(ID, a.ID))
    return MyCompare(Index, a.Index);
}

static int FindItemIndex(const CRecordVector<CIdIndexPair> &items, UInt32 id) {
    unsigned left = 0, right = items.Size();
    while (left != right) {
        const unsigned mid = (left + right) / 2;
        const UInt32 midVal = items[mid].ID;
        if (id == midVal)
            return (int)items[mid].Index;
        if (id < midVal)
            right = mid;
        else
            left = mid + 1;
    }
    return -1;
}

static int Find_in_IdExtents(const CObjectVector<CIdExtents> &items, UInt32 id) {
    unsigned left = 0, right = items.Size();
    while (left != right) {
        const unsigned mid = (left + right) / 2;
        const UInt32 midVal = items[mid].ID;
        if (id == midVal)
            return (int)mid;
        if (id < midVal)
            right = mid;
        else
            left = mid + 1;
    }
    return -1;
}

bool CFork::Upgrade(const CObjectVector<CIdExtents> &items, UInt32 id) {
    const int index = Find_in_IdExtents(items, id);
    if (index < 0)
        return true;
    const CIdExtents &item = items[index];
    if (Calc_NumBlocks_from_Extents() != item.StartBlock)
        return false;
    Extents += item.Extents;
    return true;
}

struct CVolHeader {
    unsigned BlockSizeLog;
    UInt32 BlockSize;
    UInt32 NumFiles;
    UInt32 NumFolders;
    UInt32 NumBlocks;
    UInt32 NumFreeBlocks;

    bool Is_Hsfx_ver5;
    // UInt32 Attr;
    // UInt32 LastMountedVersion;
    // UInt32 JournalInfoBlock;

    UInt32 CTime;
    UInt32 MTime;
    // UInt32 BackupTime;
    // UInt32 CheckedTime;

    // UInt32 WriteCount;
    // UInt32 FinderInfo[8];
    // UInt64 VolID;

    UInt32 GetBlockSize() const { return BlockSize; }
    UInt64 GetPhySize() const { return (UInt64)NumBlocks * BlockSize; }
    UInt64 GetFreeSize() const { return (UInt64)NumFreeBlocks * BlockSize; }
    bool IsHfsX() const { return Is_Hsfx_ver5; }
};

inline void HfsTimeToFileTime(UInt32 hfsTime, FILETIME &ft) {
    UInt64 v = ((UInt64)11644473600 + (UInt64)sevenzip_hfsplus_time(hfsTime)) * 10000000;
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
}

enum ERecordType {
    RECORD_TYPE_FOLDER = 1,
    RECORD_TYPE_FILE,
    RECORD_TYPE_FOLDER_THREAD,
    RECORD_TYPE_FILE_THREAD
};

struct CItem {
    UString Name;
    CByteBuffer DiskName;

    UInt32 ParentID;

    UInt16 Type;
    UInt16 FileMode;
    // UInt16 Flags;
    // UInt32 Valence;
    UInt32 ID;
    UInt32 CTime;
    UInt32 MTime;
    UInt32 AttrMTime;
    UInt32 ATime;
    // UInt32 BackupDate;

    UInt32 OwnerID;
    UInt32 GroupID;
    UInt32 Special;
    UInt32 FileType;
    UInt32 FileCreator;
    Byte Permissions[16];
    Byte FinderInfo[32];
    Byte AdminFlags;
    Byte OwnerFlags;
    UInt32 NumLinks;
    UInt32 DeviceMajor;
    UInt32 DeviceMinor;
    unsigned DataIndex;
    bool PrivateDir;
    bool UnsupportedLink;
    bool LinkError;

    CFork DataFork;
    CFork ResourceFork;

    // for compressed attribute (decmpfs)
    int decmpfs_AttrIndex;
    CCompressHeader CompressHeader;

    CItem() : PrivateDir(false), UnsupportedLink(false), LinkError(false), decmpfs_AttrIndex(-1) {}
    bool IsDir() const { return Type == RECORD_TYPE_FOLDER; }
    bool IsHardlink() const {
        return !IsDir() && FileType == 0x686c6e6b && FileCreator == 0x6866732b;
    }
    // const CFork *GetFork(bool isResource) const { return (isResource ? &ResourceFork: &DataFork);
    // }
};

struct CAttr {
    UInt32 ID;
    bool Fork_defined;

    // UInt32 Size;    // for (Fork_defined == false) case
    // size_t DataPos; // for (Fork_defined == false) case
    CByteBuffer Data;

    CFork Fork;

    UString Name;

    UInt64 GetSize() const {
        if (Fork_defined)
            return Fork.Size;
        return Data.Size();
    }

    CAttr()
        : Fork_defined(false)
    // Size(0),
    // DataPos(0),
    {}
};

struct CThread {
    UInt32 ID;
    UInt32 ParentID;
    UInt16 Type;
    CByteBuffer Name;
};

struct CNameIndexPair {
    UInt32 Parent;
    const CByteBuffer *Name;
    bool Binary;
    unsigned Index;
    int Compare(const CNameIndexPair &a) const {
        RINOZ(MyCompare(Parent, a.Parent))
        return sevenzip_hfsplus_compare(*Name, *a.Name, Binary);
    }
};

static sevenzip_hfsplus_record GetLinkRecord(const CItem &item) {
    sevenzip_hfsplus_record record = {item.ID,       item.ParentID,    item.CTime,  item.Special,
                                      item.FileType, item.FileCreator, item.IsDir()};
    return record;
}

struct CLinkCatalog {
    const CObjectVector<CItem> &Items;
    CObjectVector<CNameIndexPair> Names;
    static int CompareNames(void *const *a, void *const *b, void *) {
        return ((const CNameIndexPair *)*a)->Compare(*(const CNameIndexPair *)*b);
    }
    CLinkCatalog(const CObjectVector<CItem> &items, UInt32 hiddenId, bool binary) : Items(items) {
        FOR_VECTOR(i, Items) {
            if (Items[i].ParentID != hiddenId)
                continue;
            CNameIndexPair &pair = Names.AddNew();
            pair.Parent = Items[i].ParentID;
            pair.Name = &Items[i].DiskName;
            pair.Binary = binary;
            pair.Index = i;
        }
        Names.Sort(CompareNames, NULL);
    }
    static int Lookup(void *context, uint32_t parent, const char *name,
                      sevenzip_hfsplus_record *record, unsigned *index) {
        const CLinkCatalog &catalog = *(const CLinkCatalog *)context;
        CNameIndexPair key;
        key.Parent = parent;
        const unsigned len = (unsigned)strlen(name);
        CByteBuffer diskName(2 + len * 2);
        Byte *bytes = diskName;
        bytes[0] = 0;
        bytes[1] = (Byte)len;
        for (unsigned i = 0; i < len; i++) {
            bytes[2 + i * 2] = 0;
            bytes[3 + i * 2] = (Byte)name[i];
        }
        key.Name = &diskName;
        key.Binary = catalog.Names.IsEmpty() ? false : catalog.Names[0].Binary;
        unsigned left = 0, right = catalog.Names.Size();
        while (left != right) {
            const unsigned mid = left + (right - left) / 2;
            const int compare = key.Compare(catalog.Names[mid]);
            if (compare == 0) {
                *index = catalog.Names[mid].Index;
                *record = GetLinkRecord(catalog.Items[*index]);
                return 0;
            }
            if (compare < 0)
                right = mid;
            else
                left = mid + 1;
        }
        return -1;
    }
};

static const int kAttrIndex_Item = -1;
static const int kAttrIndex_Resource = -2;

struct CRef {
    unsigned ItemIndex;
    int AttrIndex;
    int Parent;

    void Construct() {
        AttrIndex = kAttrIndex_Item;
        Parent = -1;
    }
    bool IsResource() const { return AttrIndex == kAttrIndex_Resource; }
    bool IsAltStream() const { return AttrIndex != kAttrIndex_Item; }
    bool IsItem() const { return AttrIndex == kAttrIndex_Item; }
};

class CDatabase {
    HRESULT ReadFile(const CFork &fork, CByteBuffer &buf, IInStream *inStream);
    HRESULT LoadExtentFile(const CFork &fork, IInStream *inStream,
                           CObjectVector<CIdExtents> *overflowExtentsArray);
    HRESULT LoadAttrs(const CFork &fork, IInStream *inStream, IArchiveOpenCallback *progress);
    HRESULT LoadCatalog(const CFork &fork, const CObjectVector<CIdExtents> *overflowExtentsArray,
                        IInStream *inStream, IArchiveOpenCallback *progress);
    bool Parse_decmpgfs(unsigned attrIndex, CItem &item, bool &skip);
    HRESULT ResolveHardlinks();
    HRESULT LoadMetadata();
    HRESULT BuildRefs(CRecordVector<CIdIndexPair> &IdToIndexMap);

  public:
    bool CheckFork(const CFork &fork) const;
    CRecordVector<CRef> Refs;
    CObjectVector<CItem> Items;
    CObjectVector<CAttr> Attrs;
    const CItem &GetDataItem(const CRef &ref) const {
        return Items[Items[ref.ItemIndex].DataIndex];
    }

    // CByteBuffer AttrBuf;

    CVolHeader Header;
    bool HeadersError;
    bool UnsupportedFeature;
    bool ThereAreAltStreams;
    bool CatalogCaseSensitive;
    // bool CaseSensetive;
    UInt32 MethodsMask;
    UString ResFileName;

    UInt64 SpecOffset;
    // UInt64 PhySize;
    UInt64 PhySize2;
    UInt64 ArcFileSize;
    AString ReadLimitations;

    void Clear() {
        SpecOffset = 0;
        // PhySize = 0;
        PhySize2 = 0;
        ArcFileSize = 0;
        MethodsMask = 0;
        HeadersError = false;
        UnsupportedFeature = false;
        ThereAreAltStreams = false;
        CatalogCaseSensitive = false;
        ReadLimitations.Empty();
        // CaseSensetive = false;

        Refs.Clear();
        Items.Clear();
        Attrs.Clear();
        // AttrBuf.Free();
    }

    UInt64 Get_UnpackSize_of_Ref(const CRef &ref) const {
        if (ref.AttrIndex >= 0)
            return Attrs[ref.AttrIndex].GetSize();
        const CItem &item = GetDataItem(ref);
        if (ref.IsResource())
            return item.ResourceFork.Size;
        if (item.IsDir())
            return 0;
        else if (item.CompressHeader.IsCorrect)
            return item.CompressHeader.UnpackSize;
        return item.DataFork.Size;
    }

    void GetItemPath(unsigned index, NWindows::NCOM::CPropVariant &path) const;
    HRESULT Open2(IInStream *inStream, IArchiveOpenCallback *progress);
};

HRESULT CDatabase::ResolveHardlinks() {
    UInt32 hiddenId = 0, hiddenCreation = 0, rootCreation = 0;
    bool rootFound = false;
    FOR_VECTOR(i, Items) {
        const CItem &item = Items[i];
        if (item.ID == 2 && item.IsDir()) {
            rootCreation = item.CTime;
            rootFound = true;
        }
        if (item.ParentID == 2 && item.PrivateDir) {
            if (hiddenId != 0 || !item.IsDir())
                return S_FALSE;
            hiddenId = item.ID;
            hiddenCreation = item.CTime;
        }
    }
    if (!rootFound)
        return S_FALSE;
    CLinkCatalog catalog(Items, hiddenId, CatalogCaseSensitive);
    FOR_VECTOR(i, Items) {
        CItem &item = Items[i];
        if (hfsplus_lookup(GetLinkRecord(item), i, hiddenId, hiddenCreation, rootCreation,
                           CLinkCatalog::Lookup, &catalog, &item.DataIndex) != 0) {
            item.LinkError = true;
            HeadersError = true;
        }
    }
    return S_OK;
}

HRESULT CDatabase::LoadMetadata() {
    CBoolArr ready(Items.Size());
    for (unsigned i = 0; i < Items.Size(); i++)
        ready[i] = false;
    FOR_VECTOR(i, Items) {
        const unsigned index = Items[i].DataIndex;
        if (ready[index])
            continue;
        ready[index] = true;
        CItem &item = Items[index];
        sevenzip_hfsplus_inode metadata;
        // Archive metadata has stable root credentials and the usual Linux umask,
        // independent of the desktop process's credentials or mount overrides.
        if (hfsplus_cat_read_inode(item.Permissions, item.IsDir(), &metadata))
            return S_FALSE;
        const sevenzip_hfsplus_permissions &permissions = metadata.permissions;
        item.OwnerID = permissions.owner;
        item.GroupID = permissions.group;
        item.FileMode = permissions.mode;
        item.AdminFlags = permissions.rootflags;
        item.OwnerFlags = permissions.userflags;
        item.NumLinks = metadata.links;
        item.DeviceMajor = metadata.device_major;
        item.DeviceMinor = metadata.device_minor;
    }
    CRecordVector<CIdIndexPair> finderAttrs;
    FOR_VECTOR(i, Attrs) {
        if (Attrs[i].Name.IsEqualTo("com.apple.FinderInfo")) {
            CIdIndexPair pair;
            pair.ID = Attrs[i].ID;
            pair.Index = i;
            finderAttrs.Add(pair);
        }
    }
    finderAttrs.Sort2();
    FOR_VECTOR(i, Items) {
        const CItem &item = Items[i];
        if (item.DataIndex != i)
            continue;
        const int attrIndex = FindItemIndex(finderAttrs, item.ID);
        bool defined = false;
        for (unsigned j = 0; j < sizeof(item.FinderInfo); j++)
            defined |= item.FinderInfo[j] != 0;
        if (!defined && attrIndex < 0)
            continue;
        CAttr &attr = attrIndex < 0 ? Attrs.AddNew() : Attrs[(unsigned)attrIndex];
        attr.ID = item.ID;
        attr.Name = "com.apple.FinderInfo";
        attr.Fork_defined = false;
        attr.Data.CopyFrom(item.FinderInfo, sizeof(item.FinderInfo));
    }
    return S_OK;
}

enum {
    kHfsID_Root = 1,
    kHfsID_RootFolder = 2,
    kHfsID_ExtentsFile = 3,
    kHfsID_CatalogFile = 4,
    kHfsID_BadBlockFile = 5,
    kHfsID_AllocationFile = 6,
    kHfsID_StartupFile = 7,
    kHfsID_AttributesFile = 8,
    kHfsID_RepairCatalogFile = 14,
    kHfsID_BogusExtentFile = 15,
    kHfsID_FirstUserCatalogNode = 16
};

void CDatabase::GetItemPath(unsigned index, NWindows::NCOM::CPropVariant &path) const {
    unsigned len = 0;
    unsigned cur = index;

    for (;;) {
        const CRef &ref = Refs[cur];
        const UString *s;

        if (ref.IsResource())
            s = &ResFileName;
        else if (ref.AttrIndex >= 0)
            s = &Attrs[ref.AttrIndex].Name;
        else
            s = &Items[ref.ItemIndex].Name;

        len += s->Len();
        len++;
        cur = (unsigned)ref.Parent;
        if (ref.Parent < 0)
            break;
    }

    len--;
    wchar_t *p = path.AllocBstr(len);
    p[len] = 0;
    cur = index;

    for (;;) {
        const CRef &ref = Refs[cur];
        const UString *s;
        wchar_t delimChar = L':';

        if (ref.IsResource())
            s = &ResFileName;
        else if (ref.AttrIndex >= 0)
            s = &Attrs[ref.AttrIndex].Name;
        else {
            delimChar = WCHAR_PATH_SEPARATOR;
            s = &Items[ref.ItemIndex].Name;
        }

        unsigned curLen = s->Len();
        len -= curLen;

        const wchar_t *src = (const wchar_t *)*s;
        wchar_t *dest = p + len;
        for (unsigned j = 0; j < curLen; j++) {
            wchar_t c = src[j];
            // 18.06
            if (c == CHAR_PATH_SEPARATOR || c == '/' || c == ':')
                c = '_';
            dest[j] = c;
        }

        if (len == 0)
            break;
        p[--len] = delimChar;
        cur = (unsigned)ref.Parent;
    }
}

bool CDatabase::CheckFork(const CFork &fork) const {
    if (!fork.Check_NumBlocks() || fork.Size > (UInt64)fork.NumBlocks * Header.GetBlockSize() ||
        SpecOffset > ArcFileSize)
        return false;
    const UInt64 sourceBlocks = (ArcFileSize - SpecOffset) / Header.GetBlockSize();
    FOR_VECTOR(i, fork.Extents) {
        const CExtent &e = fork.Extents[i];
        if (e.Pos > Header.NumBlocks || e.NumBlocks > Header.NumBlocks - e.Pos ||
            e.Pos > sourceBlocks || e.NumBlocks > sourceBlocks - e.Pos)
            return false;
    }
    return true;
}

HRESULT CDatabase::ReadFile(const CFork &fork, CByteBuffer &buf, IInStream *inStream) {
    if (!CheckFork(fork))
        return S_FALSE;

    const size_t totalSize = (size_t)fork.Size;
    if (totalSize != fork.Size)
        return S_FALSE;
    buf.Alloc(totalSize);
    size_t pos = 0;
    FOR_VECTOR(i, fork.Extents) {
        if (pos == totalSize)
            break;
        const CExtent &e = fork.Extents[i];
        UInt64 size = (UInt64)e.NumBlocks << Header.BlockSizeLog;
        if (size > totalSize - pos)
            size = totalSize - pos;
        RINOK(InStream_SeekSet(inStream, SpecOffset + ((UInt64)e.Pos << Header.BlockSizeLog)))
        RINOK(ReadStream_FALSE(inStream, (Byte *)buf + pos, (size_t)size))
        pos += (size_t)size;
    }
    return pos == totalSize ? S_OK : S_FALSE;
}

static const unsigned kNodeDescriptor_Size = 14;

struct CNodeDescriptor {
    UInt32 fLink;
    UInt32 bLink;
    Byte Kind;
    Byte Height;
    unsigned NumRecords;

    bool Parse(const Byte *p, unsigned nodeSizeLog);
};

bool CNodeDescriptor::Parse(const Byte *p, unsigned nodeSizeLog) {
    fLink = Get32(p);
    bLink = Get32(p + 4);
    Kind = p[8];
    Height = p[9];
    NumRecords = Get16(p + 10);

    const size_t nodeSize = (size_t)1 << nodeSizeLog;
    if (kNodeDescriptor_Size + ((UInt32)NumRecords + 1) * 2 > nodeSize)
        return false;
    const size_t limit = nodeSize - ((UInt32)NumRecords + 1) * 2;

    p += nodeSize - 2;
    if (Get16(p) != kNodeDescriptor_Size)
        return false;

    for (unsigned i = 0; i < NumRecords; i++) {
        const UInt32 offs = Get16(p);
        p -= 2;
        const UInt32 offsNext = Get16(p);
        if ((offs | offsNext) & 1)
            return false;
        if (offs < kNodeDescriptor_Size || offs >= offsNext || offsNext > limit)
            return false;
    }
    return true;
}

struct CHeaderRec {
    // UInt16 TreeDepth;
    // UInt32 RootNode;
    UInt32 LeafRecords;
    UInt32 FirstLeafNode;
    UInt32 LastLeafNode;
    unsigned NodeSizeLog;
    // UInt16 MaxKeyLength;
    UInt32 TotalNodes;
    // UInt32 FreeNodes;
    // UInt16 Reserved1;
    // UInt32 ClumpSize;
    // Byte BtreeType;
    // Byte KeyCompareType;
    // UInt32 Attributes;
    // UInt32 Reserved3[16];

    HRESULT Parse2(const CByteBuffer &buf, unsigned maxKeyLength);
};

HRESULT CHeaderRec::Parse2(const CByteBuffer &buf, unsigned maxKeyLength) {
    if (buf.Size() < kNodeDescriptor_Size + 0x2A + 16 * 4)
        return S_FALSE;
    const Byte *p = (const Byte *)buf + kNodeDescriptor_Size;
    // TreeDepth = Get16(p);
    // RootNode = Get32(p + 2);
    LeafRecords = Get32(p + 6);
    FirstLeafNode = Get32(p + 0xA);
    LastLeafNode = Get32(p + 0xE);
    const UInt32 nodeSize = Get16(p + 0x12);

    unsigned i;
    for (i = 9; ((UInt32)1 << i) != nodeSize; i++)
        if (i == 16)
            return S_FALSE;
    NodeSizeLog = i;

    CNodeDescriptor desc;
    if (nodeSize > buf.Size() || !desc.Parse(buf, NodeSizeLog) || desc.Kind != 1 ||
        desc.Height != 0 || desc.NumRecords != 3)
        return S_FALSE;

    const UInt32 attributes = Get32(p + 0x26);
    if (Get16(p + 0x14) != maxKeyLength || !(attributes & 2) ||
        (maxKeyLength == 10 && (attributes & 4)) || (maxKeyLength == 516 && !(attributes & 4)) ||
        Get16((const Byte *)buf + nodeSize - 4) < kNodeDescriptor_Size + 106)
        return S_FALSE;
    TotalNodes = Get32(p + 0x16);
    // FreeNodes = Get32(p + 0x1A);
    // Reserved1 = Get16(p + 0x1E);
    // ClumpSize = Get32(p + 0x20);
    // BtreeType = p[0x24];
    // KeyCompareType = p[0x25];
    // Attributes = Get32(p + 0x26);
    /*
    for (int i = 0; i < 16; i++)
      Reserved3[i] = Get32(p + 0x2A + i * 4);
    */

    const UInt32 rootNode = Get32(p + 2);
    if (TotalNodes == 0 || (buf.Size() >> NodeSizeLog) < TotalNodes || rootNode >= TotalNodes ||
        FirstLeafNode >= TotalNodes || LastLeafNode >= TotalNodes ||
        ((rootNode == 0) != (Get16(p) == 0)) || ((FirstLeafNode == 0) != (LastLeafNode == 0)) ||
        ((FirstLeafNode == 0) != (LeafRecords == 0)) || ((rootNode == 0) != (FirstLeafNode == 0)))
        return S_FALSE;
    if (rootNode != 0) {
        const Byte *root = (const Byte *)buf + ((size_t)rootNode << NodeSizeLog);
        if (!desc.Parse(root, NodeSizeLog) || desc.Height != Get16(p) ||
            desc.Kind != (desc.Height == 1 ? 0xFF : 0))
            return S_FALSE;
    }

    return S_OK;
}

static const Byte kNodeType_Leaf = 0xFF;
// static const Byte kNodeType_Index  = 0;
// static const Byte kNodeType_Header = 1;
// static const Byte kNodeType_Mode   = 2;

static const Byte kExtentForkType_Data = 0;
static const Byte kExtentForkType_Resource = 0xFF;

/* It loads data extents from Extents Overflow File
   Most dmg installers are not fragmented. So there are no extents in Overflow File. */

HRESULT CDatabase::LoadExtentFile(const CFork &fork, IInStream *inStream,
                                  CObjectVector<CIdExtents> *overflowExtentsArray) {
    if (fork.NumBlocks == 0)
        return S_OK;
    CByteBuffer buf;
    RINOK(ReadFile(fork, buf, inStream))
    const Byte *p = (const Byte *)buf;

    // CNodeDescriptor nodeDesc;
    // nodeDesc.Parse(p);
    CHeaderRec hr;
    RINOK(hr.Parse2(buf, 10))

    UInt32 node = hr.FirstLeafNode;
    if (node == 0)
        return S_OK;
    if (hr.TotalNodes == 0)
        return S_FALSE;

    CByteArr usedBuf(hr.TotalNodes);
    memset(usedBuf, 0, hr.TotalNodes);

    UInt64 leafRecords = 0;
    UInt32 previousNode = 0;
    while (node != 0) {
        if (node >= hr.TotalNodes || usedBuf[node] != 0)
            return S_FALSE;
        usedBuf[node] = 1;

        const size_t nodeOffset = (size_t)node << hr.NodeSizeLog;
        CNodeDescriptor desc;
        if (!desc.Parse(p + nodeOffset, hr.NodeSizeLog))
            return S_FALSE;
        if (desc.Kind != kNodeType_Leaf || desc.Height != 1 || desc.bLink != previousNode)
            return S_FALSE;

        for (unsigned i = 0; i < desc.NumRecords; i++) {
            const UInt32 nodeSize = ((UInt32)1 << hr.NodeSizeLog);
            const Byte *r = p + nodeOffset + nodeSize - i * 2;
            const UInt32 offs = Get16(r - 2);
            UInt32 recSize = Get16(r - 4) - offs;
            const unsigned kKeyLen = 10;

            if (recSize != 2 + kKeyLen + kNumFixedExtents * 8)
                return S_FALSE;

            r = p + nodeOffset + offs;
            if (Get16(r) != kKeyLen)
                return S_FALSE;

            const Byte forkType = r[2];
            unsigned forkTypeIndex;
            if (forkType == kExtentForkType_Data)
                forkTypeIndex = 0;
            else if (forkType == kExtentForkType_Resource)
                forkTypeIndex = 1;
            else
                continue;
            CObjectVector<CIdExtents> &overflowExtents = overflowExtentsArray[forkTypeIndex];

            const UInt32 id = Get32(r + 4);
            const UInt32 startBlock = Get32(r + 8);
            r += 2 + kKeyLen;

            bool needNew = true;

            if (overflowExtents.Size() != 0) {
                CIdExtents &e = overflowExtents.Back();
                if (e.ID > id)
                    return S_FALSE;
                if (e.ID == id) {
                    if (e.EndBlock != startBlock)
                        return S_FALSE;
                    needNew = false;
                }
            }

            if (needNew) {
                CIdExtents &e = overflowExtents.AddNew();
                e.ID = id;
                e.StartBlock = startBlock;
            }

            CIdExtents &e = overflowExtents.Back();
            UInt64 endBlock = startBlock;

            for (unsigned k = 0; k < kNumFixedExtents; k++, r += 8) {
                CExtent ee;
                ee.Pos = Get32(r);
                ee.NumBlocks = Get32(r + 4);
                if (ee.NumBlocks != 0) {
                    endBlock += ee.NumBlocks;
                    if (endBlock > (UInt32)0xFFFFFFFF || ee.Pos > Header.NumBlocks ||
                        ee.NumBlocks > Header.NumBlocks - ee.Pos)
                        return S_FALSE;
                    e.Extents.Add(ee);
                }
            }
            e.EndBlock = (UInt32)endBlock;
        }

        leafRecords += desc.NumRecords;
        previousNode = node;
        node = desc.fLink;
    }
    if (previousNode != hr.LastLeafNode || leafRecords != hr.LeafRecords)
        return S_FALSE;
    return S_OK;
}

static bool LoadName(const Byte *data, unsigned len, UString &dest, bool xattr) {
    UInt16 units[255];
    unsigned size = Z7_ARRAY_SIZE(units);
    if (sevenzip_hfsplus_name(data, len, xattr, units, &size) != 0)
        return false;
    wchar_t *out = dest.GetBuf(size);
    for (unsigned i = 0; i < size; i++) {
        if (units[i] == 0)
            return false;
        out[i] = units[i];
    }
    dest.ReleaseBuf_SetEnd(size);
    return true;
}

static bool IsNameEqualTo(const Byte *data, const char *name) {
    for (unsigned i = 0;; i++) {
        const char c = name[i];
        if (c == 0)
            return true;
        if (Get16(data + i * 2) != (Byte)c)
            return false;
    }
}

static const UInt32 kAttrRecordType_Inline = 0x10;
static const UInt32 kAttrRecordType_Fork = 0x20;
// static const UInt32 kAttrRecordType_Extents = 0x30;

HRESULT CDatabase::LoadAttrs(const CFork &fork, IInStream *inStream,
                             IArchiveOpenCallback *progress) {
    if (fork.NumBlocks == 0)
        return S_OK;

    CByteBuffer AttrBuf;
    RINOK(ReadFile(fork, AttrBuf, inStream))
    const Byte *p = (const Byte *)AttrBuf;

    // CNodeDescriptor nodeDesc;
    // nodeDesc.Parse(p);
    CHeaderRec hr;
    RINOK(hr.Parse2(AttrBuf, 266))

    // CaseSensetive = (Header.IsHfsX() && hr.KeyCompareType == 0xBC);

    UInt32 node = hr.FirstLeafNode;
    if (node == 0)
        return S_OK;
    if (hr.TotalNodes == 0)
        return S_FALSE;

    CByteArr usedBuf(hr.TotalNodes);
    memset(usedBuf, 0, hr.TotalNodes);

    CFork resFork;

    UInt64 leafRecords = 0;
    UInt32 previousNode = 0;
    while (node != 0) {
        if (node >= hr.TotalNodes || usedBuf[node] != 0)
            return S_FALSE;
        usedBuf[node] = 1;

        const size_t nodeOffset = (size_t)node << hr.NodeSizeLog;
        CNodeDescriptor desc;
        if (!desc.Parse(p + nodeOffset, hr.NodeSizeLog))
            return S_FALSE;
        if (desc.Kind != kNodeType_Leaf || desc.Height != 1 || desc.bLink != previousNode)
            return S_FALSE;

        for (unsigned i = 0; i < desc.NumRecords; i++) {
            const UInt32 nodeSize = ((UInt32)1 << hr.NodeSizeLog);
            const Byte *r = p + nodeOffset + nodeSize - i * 2;
            const UInt32 offs = Get16(r - 2);
            UInt32 recSize = Get16(r - 4) - offs;
            const unsigned kHeadSize = 14;
            if (recSize < kHeadSize)
                return S_FALSE;

            r = p + nodeOffset + offs;
            const UInt32 keyLen = Get16(r);

            // UInt16 pad = Get16(r + 2);
            const UInt32 fileID = Get32(r + 4);
            const unsigned startBlock = Get32(r + 8);
            const unsigned nameLen = Get16(r + 12);

            if (keyLen + 2 > recSize || keyLen != kHeadSize - 2 + nameLen * 2)
                return S_FALSE;
            r += kHeadSize;
            recSize -= kHeadSize;

            const Byte *name = r;
            r += nameLen * 2;
            recSize -= nameLen * 2;

            if (recSize < 4)
                return S_FALSE;

            const UInt32 recordType = Get32(r);
            // Linux's attribute comparator uses CNID and name, not startBlock.
            if (startBlock != 0 && recordType != kAttrRecordType_Inline) {
                UnsupportedFeature = true;
                continue;
            }

            if (progress && (Attrs.Size() & 0xFFF) == 0) {
                const UInt64 numFiles = 0;
                RINOK(progress->SetCompleted(&numFiles, NULL))
            }

            if (Attrs.Size() >= ((UInt32)1 << 31))
                return S_FALSE;

            CAttr &attr = Attrs.AddNew();
            attr.ID = fileID;
            if (!LoadName(name, nameLen, attr.Name, true))
                return S_FALSE;

            if (recordType == kAttrRecordType_Fork) {
                // 22.00 : some hfs files contain it;
                /* spec: If the attribute has more than 8 extents, there will be additional
                    records (of type kAttrRecordType_Extents) for this attribute. */
                if (recSize != 8 + kForkRecSize)
                    return S_FALSE;
                if (Get32(r + 4) != 0) // reserved
                    return S_FALSE;
                attr.Fork.Parse(r + 8);
                attr.Fork_defined = true;
                continue;
            } else if (recordType != kAttrRecordType_Inline) {
                return S_FALSE;
            }

            const int linuxSize = __hfsplus_getxattr(r, recSize, NULL, 0);
            if (linuxSize >= 0) {
                attr.Data.Alloc((size_t)linuxSize);
                if (__hfsplus_getxattr(r, recSize, attr.Data, (size_t)linuxSize) != linuxSize)
                    return S_FALSE;
                continue;
            }
            // Preserve the SDK's larger inline values outside Linux's limit.
            const unsigned kRecordHeaderSize = 16;
            if (recSize < kRecordHeaderSize)
                return S_FALSE;
            if (Get32(r + 4) != 0 || Get32(r + 8) != 0) // reserved
                return S_FALSE;
            const UInt32 dataSize = Get32(r + 12);

            r += kRecordHeaderSize;
            recSize -= kRecordHeaderSize;

            if (recSize < dataSize)
                return S_FALSE;

            attr.Data.CopyFrom(r, dataSize);
            // attr.DataPos = nodeOffset + offs + 2 + keyLen + kRecordHeaderSize;
            // attr.Size = dataSize;
        }

        leafRecords += desc.NumRecords;
        previousNode = node;
        node = desc.fLink;
    }
    if (previousNode != hr.LastLeafNode || leafRecords != hr.LeafRecords)
        return S_FALSE;
    return S_OK;
}

bool CDatabase::Parse_decmpgfs(unsigned attrIndex, CItem &item, bool &skip) {
    const CAttr &attr = Attrs[attrIndex];
    skip = false;
    if (item.CompressHeader.IsCorrect || !item.DataFork.IsEmpty())
        return false;

    item.CompressHeader.Parse(attr.Data, attr.Data.Size());

    if (item.CompressHeader.IsCorrect) {
        item.decmpfs_AttrIndex = (int)attrIndex;
        skip = true;
        if (item.CompressHeader.Method < sizeof(MethodsMask) * 8)
            MethodsMask |= ((UInt32)1 << item.CompressHeader.Method);
    }

    return true;
}

HRESULT CDatabase::LoadCatalog(const CFork &fork,
                               const CObjectVector<CIdExtents> *overflowExtentsArray,
                               IInStream *inStream, IArchiveOpenCallback *progress) {
    CByteBuffer buf;
    RINOK(ReadFile(fork, buf, inStream))
    const Byte *p = (const Byte *)buf;

    // CNodeDescriptor nodeDesc;
    // nodeDesc.Parse(p);
    CHeaderRec hr;
    RINOK(hr.Parse2(buf, 516))
    CatalogCaseSensitive = Header.IsHfsX() && p[kNodeDescriptor_Size + 0x25] == 0xBC;

    CRecordVector<CIdIndexPair> IdToIndexMap;
    CObjectVector<CThread> threads;

    const unsigned reserveSize = (unsigned)(Header.NumFolders + 1 + Header.NumFiles);

    const unsigned kBasicRecSize = 0x58;
    const unsigned kMinRecSize = kBasicRecSize + 10;

    if ((UInt64)reserveSize * kMinRecSize < buf.Size()) {
        Items.ClearAndReserve(reserveSize);
        Refs.ClearAndReserve(reserveSize);
        IdToIndexMap.ClearAndReserve(reserveSize);
    }

    // CaseSensetive = (Header.IsHfsX() && hr.KeyCompareType == 0xBC);

    CByteArr usedBuf(hr.TotalNodes);
    if (hr.TotalNodes != 0)
        memset(usedBuf, 0, hr.TotalNodes);

    CFork resFork;

    UInt32 node = hr.FirstLeafNode;
    UInt32 numFiles = 0;
    UInt32 numFolders = 0;

    UInt64 leafRecords = 0;
    UInt32 previousNode = 0;
    while (node != 0) {
        if (node >= hr.TotalNodes || usedBuf[node] != 0)
            return S_FALSE;
        usedBuf[node] = 1;

        const size_t nodeOffset = (size_t)node << hr.NodeSizeLog;
        CNodeDescriptor desc;
        if (!desc.Parse(p + nodeOffset, hr.NodeSizeLog))
            return S_FALSE;
        if (desc.Kind != kNodeType_Leaf || desc.Height != 1 || desc.bLink != previousNode)
            return S_FALSE;

        for (unsigned i = 0; i < desc.NumRecords; i++) {
            const UInt32 nodeSize = (1 << hr.NodeSizeLog);
            const Byte *r = p + nodeOffset + nodeSize - i * 2;
            const UInt32 offs = Get16(r - 2);
            UInt32 recSize = Get16(r - 4) - offs;
            if (recSize < 6)
                return S_FALSE;

            r = p + nodeOffset + offs;
            UInt32 keyLen = Get16(r);
            UInt32 parentID = Get32(r + 2);
            if (keyLen < 6 || (keyLen & 1) != 0 || keyLen + 2 > recSize)
                return S_FALSE;
            r += 6;
            recSize -= 6;
            keyLen -= 6;

            unsigned nameLen = Get16(r);
            if (nameLen * 2 != (unsigned)keyLen)
                return S_FALSE;
            r += 2;
            recSize -= 2;

            r += nameLen * 2;
            recSize -= nameLen * 2;

            if (recSize < 2)
                return S_FALSE;
            UInt16 type = Get16(r);
            if (hfsplus_brec_read_cat(r, recSize))
                return S_FALSE;
            if (type == RECORD_TYPE_FOLDER_THREAD || type == RECORD_TYPE_FILE_THREAD) {
                if (nameLen != 0)
                    return S_FALSE;
                CThread &thread = threads.AddNew();
                thread.ID = parentID;
                thread.ParentID = Get32(r + 4);
                thread.Type = type;
                thread.Name.CopyFrom(r + 8, recSize - 8);
                continue;
            }

            if (type != RECORD_TYPE_FOLDER && type != RECORD_TYPE_FILE)
                continue;

            if (recSize < kBasicRecSize)
                return S_FALSE;

            CItem &item = Items.AddNew();
            item.DataIndex = Items.Size() - 1;
            item.ParentID = parentID;
            item.Type = type;
            // item.Flags = Get16(r + 2);
            // item.Valence = Get32(r + 4);
            item.ID = Get32(r + 8);
            {
                const Byte *name = r - (nameLen * 2);
                if (nameLen > 255)
                    return S_FALSE;
                item.DiskName.CopyFrom(name - 2, 2 + nameLen * 2);
                item.PrivateDir =
                    sevenzip_hfsplus_private_name(item.DiskName, CatalogCaseSensitive);
                if (!LoadName(name, nameLen, item.Name, false))
                    return S_FALSE;
                // macOS's root directory-hardlink store ends in CR. Preserve
                // the catalog name for lookups, but expose a portable archive
                // path, as for the file-hardlink store below.
                if (item.ParentID == 2 && type == RECORD_TYPE_FOLDER &&
                    item.Name == L".HFS+ Private Directory Data\r")
                    item.Name = "[HFS+ Private Directory Data]";
                if (item.Name.Len() <= 1) {
                    if (item.Name.IsEmpty() && nameLen == 21) {
                        if (GetUi32(name) == 0 && GetUi32(name + 4) == 0 &&
                            IsNameEqualTo(name + 8, "HFS+ Private Data")) {
                            // it's folder for "Hard Links" files
                            item.Name = "[HFS+ Private Data]";
                        }
                    }

                    // Some dmg files have ' ' folder item.
                    if (item.Name.IsEmpty() || item.Name[0] == L' ')
                        item.Name = "[]";
                }
            }

            item.CTime = Get32(r + 0xC);
            item.MTime = Get32(r + 0x10);
            item.AttrMTime = Get32(r + 0x14);
            item.ATime = Get32(r + 0x18);
            // item.BackupDate = Get32(r + 0x1C);

            item.OwnerID = Get32(r + 0x20);
            item.GroupID = Get32(r + 0x24);
            item.FileMode = Get16(r + 0x2A);
            item.Special = Get32(r + 0x2C);
            item.FileType = Get32(r + 0x30);
            item.FileCreator = Get32(r + 0x34);
            memcpy(item.Permissions, r + 0x20, sizeof(item.Permissions));
            item.AdminFlags = r[0x28];
            item.OwnerFlags = r[0x29];
            if (hfsplus_getxattr_finder_info(r, recSize, item.FinderInfo,
                                             sizeof(item.FinderInfo)) != 32)
                return S_FALSE;

            // const refIndex = Refs.Size();
            CIdIndexPair pair;
            pair.ID = item.ID;
            pair.Index = Items.Size() - 1;
            IdToIndexMap.Add(pair);

            recSize -= kBasicRecSize;
            r += kBasicRecSize;
            if (item.IsDir()) {
                numFolders++;
                if (recSize != 0)
                    return S_FALSE;
            } else {
                numFiles++;
                if (recSize != kForkRecSize * 2)
                    return S_FALSE;

                item.DataFork.Parse(r);

                if (!item.DataFork.UpgradeAndTest(overflowExtentsArray[0], item.ID,
                                                  Header.BlockSizeLog))
                    HeadersError = true;

                item.ResourceFork.Parse(r + kForkRecSize);
                if (!item.ResourceFork.IsEmpty()) {
                    if (!item.ResourceFork.UpgradeAndTest(overflowExtentsArray[1], item.ID,
                                                          Header.BlockSizeLog))
                        HeadersError = true;
                    // ThereAreAltStreams = true;
                }
            }
            if (progress && (Items.Size() & 0xFFF) == 0) {
                const UInt64 numItems = Items.Size();
                RINOK(progress->SetCompleted(&numItems, NULL))
            }
        }
        leafRecords += desc.NumRecords;
        previousNode = node;
        node = desc.fLink;
    }
    if (previousNode != hr.LastLeafNode || leafRecords != hr.LeafRecords)
        return S_FALSE;

    if (Header.NumFiles != numFiles || Header.NumFolders + 1 != numFolders)
        HeadersError = true;
    RINOK(ResolveHardlinks())
    RINOK(LoadMetadata())

    CRecordVector<CIdIndexPair> threadIds;
    FOR_VECTOR(i, threads) {
        CIdIndexPair pair;
        pair.ID = threads[i].ID;
        pair.Index = i;
        threadIds.Add(pair);
    }
    threadIds.Sort2();
    if (threads.Size() != Items.Size())
        return S_FALSE;
    for (unsigned i = 1; i < threadIds.Size(); i++)
        if (threadIds[i - 1].ID == threadIds[i].ID)
            return S_FALSE;
    FOR_VECTOR(i, Items) {
        const CItem &item = Items[i];
        const int threadIndex = FindItemIndex(threadIds, item.ID);
        if (threadIndex < 0)
            return S_FALSE;
        const CThread &thread = threads[(unsigned)threadIndex];
        if (thread.ParentID != item.ParentID ||
            thread.Type != (item.IsDir() ? RECORD_TYPE_FOLDER_THREAD : RECORD_TYPE_FILE_THREAD) ||
            sevenzip_hfsplus_compare(thread.Name, item.DiskName, CatalogCaseSensitive) != 0)
            return S_FALSE;
    }

    return BuildRefs(IdToIndexMap);
}

HRESULT CDatabase::BuildRefs(CRecordVector<CIdIndexPair> &IdToIndexMap) {
    IdToIndexMap.Sort2();
    for (unsigned i = 1; i < IdToIndexMap.Size(); i++)
        if (IdToIndexMap[i - 1].ID == IdToIndexMap[i].ID)
            return S_FALSE;

    CBoolArr skipAttr(Attrs.Size());
    {
        for (unsigned i = 0; i < Attrs.Size(); i++)
            skipAttr[i] = false;
    }

    {
        FOR_VECTOR(i, Attrs) {
            const CAttr &attr = Attrs[i];

            const int itemIndex = FindItemIndex(IdToIndexMap, attr.ID);
            if (itemIndex < 0) {
                HeadersError = true;
                continue;
            }

            if (attr.Name.IsEqualTo("com.apple.decmpfs")) {
                if (!Parse_decmpgfs(i, Items[itemIndex], skipAttr[i]))
                    HeadersError = true;
            }
        }
    }

    IdToIndexMap.ClearAndReserve(Items.Size());

    {
        FOR_VECTOR(i, Items) {
            const CItem &item = Items[Items[i].DataIndex];

            CIdIndexPair pair;
            pair.ID = Items[i].ID;
            pair.Index = Refs.Size();
            IdToIndexMap.Add(pair);

            CRef ref;
            ref.Construct();
            ref.ItemIndex = i;
            Refs.Add(ref);

#ifdef HFS_SHOW_ALT_STREAMS

            if (item.ResourceFork.IsEmpty())
                continue;
            if (item.CompressHeader.IsSupported && item.CompressHeader.IsMethod_Resource())
                continue;

            ThereAreAltStreams = true;
            ref.AttrIndex = kAttrIndex_Resource;
            ref.Parent = (int)(Refs.Size() - 1);
            Refs.Add(ref);

#endif
        }
    }

    IdToIndexMap.Sort2();

    {
        FOR_VECTOR(i, Refs) {
            CRef &ref = Refs[i];
            if (ref.IsResource())
                continue;
            const CItem &item = Items[ref.ItemIndex];
            ref.Parent = FindItemIndex(IdToIndexMap, item.ParentID);
            if (ref.Parent >= 0) {
                if (!Items[Refs[ref.Parent].ItemIndex].IsDir()) {
                    ref.Parent = -1;
                    HeadersError = true;
                }
            }
        }
    }

#ifdef HFS_SHOW_ALT_STREAMS
    {
        CRecordVector<CIdIndexPair> attrParents;
        FOR_VECTOR(i, Refs) {
            const CRef &ref = Refs[i];
            if (!ref.IsItem())
                continue;
            CIdIndexPair pair;
            pair.ID = Items[ref.ItemIndex].DataIndex;
            pair.Index = i;
            attrParents.Add(pair);
        }
        attrParents.Sort2();
        FOR_VECTOR(i, Attrs) {
            if (skipAttr[i])
                continue;
            const CAttr &attr = Attrs[i];

            const int refIndex = FindItemIndex(IdToIndexMap, attr.ID);
            if (refIndex < 0) {
                HeadersError = true;
                continue;
            }

            const unsigned source = Refs[refIndex].ItemIndex;
            unsigned left = 0, right = attrParents.Size();
            while (left != right) {
                const unsigned mid = left + (right - left) / 2;
                if (attrParents[mid].ID < source)
                    left = mid + 1;
                else
                    right = mid;
            }
            for (; left < attrParents.Size() && attrParents[left].ID == source; left++) {
                ThereAreAltStreams = true;
                CRef ref;
                ref.Construct();
                ref.AttrIndex = (int)i;
                ref.Parent = (int)attrParents[left].Index;
                ref.ItemIndex = Refs[ref.Parent].ItemIndex;
                Refs.Add(ref);
            }
        }
    }
#endif

    // A catalog parent must terminate at a root, including for alternate streams.
    CByteArr parentState(Refs.Size());
    memset(parentState, 0, Refs.Size());
    FOR_VECTOR(i, Refs) {
        int cur = (int)i;
        while (cur >= 0 && parentState[cur] == 0) {
            parentState[cur] = 1;
            cur = Refs[cur].Parent;
        }
        if (cur >= 0 && parentState[cur] == 1)
            return S_FALSE;
        cur = (int)i;
        while (cur >= 0 && parentState[cur] == 1) {
            parentState[cur] = 2;
            cur = Refs[cur].Parent;
        }
    }

    return S_OK;
}

static const unsigned kHeaderPadSize = 1 << 10;
static const unsigned kMainHeaderSize = 512;
static const unsigned kHfsHeaderSize = kHeaderPadSize + kMainHeaderSize;

static const unsigned k_Signature_LE16_HFS_BD = 'B' + ((unsigned)'D' << 8);
static const unsigned k_Signature_LE16_HPLUS = 'H' + ((unsigned)'+' << 8);
static const unsigned k_Signature_LE16_HFSX = 'H' + ((unsigned)'X' << 8);
static const UInt32 k_Signature_LE32_HFSP_VER4 = 'H' + ((UInt32)'+' << 8) + ((UInt32)4 << 24);
static const UInt32 k_Signature_LE32_HFSX_VER5 = 'H' + ((UInt32)'X' << 8) + ((UInt32)5 << 24);

API_FUNC_static_IsArc IsArc_HFS(const Byte *p, size_t size) {
    if (size < kHfsHeaderSize)
        return k_IsArc_Res_NEED_MORE;
    p += kHeaderPadSize;
    const UInt16 sig = GetUi16(p);
    if (sig == k_Signature_LE16_HFS_BD)
        return k_IsArc_Res_YES;
    return ((sig == k_Signature_LE16_HPLUS || sig == k_Signature_LE16_HFSX) &&
            sevenzip_hfsplus_volume_version(Get16(p + 2)))
               ? k_IsArc_Res_YES
               : k_IsArc_Res_NO;
}
}

HRESULT CDatabase::Open2(IInStream *inStream, IArchiveOpenCallback *progress) {
    Clear();
    UInt32 buf32[kHfsHeaderSize / 4];
    RINOK(ReadStream_FALSE(inStream, buf32, kHfsHeaderSize))
    const Byte *p = (const Byte *)buf32 + kHeaderPadSize;
    CVolHeader &h = Header;
    UInt64 embeddedSize = 0;

    while (GetUi16a(p) == k_Signature_LE16_HFS_BD) {
        /*
        It's header for old HFS format.
        We don't support old HFS format, but we support
        special HFS volume that contains embedded HFS+ volume.
        HFS MDB : Master directory block
        HFS VIB : Volume information block
        some old images contain boot data with "LK" signature at start of buf32.
        */

        if (GetUi16a(p + 0x7c) != k_Signature_LE16_HPLUS &&
            GetUi16a(p + 0x7c) != k_Signature_LE16_HFSX) {
            return S_FALSE;
        }
        /*
        h.CTime = Get32(p + 0x2);
        h.MTime = Get32(p + 0x6);

        h.NumFiles = Get32(p + 0x54);
        h.NumFolders = Get32(p + 0x58);

        if (h.NumFolders > ((UInt32)1 << 29) ||
            h.NumFiles > ((UInt32)1 << 30))
          return S_FALSE;
        if (progress)
        {
          UInt64 numFiles = (UInt64)h.NumFiles + h.NumFolders + 1;
          RINOK(progress->SetTotal(&numFiles, NULL))
        }
        h.NumFreeBlocks = Get16(p + 0x22);
        */

        // v24.09: blockSize in old HFS image can be non-power of 2.
        hfsplus_wd wrapper;
        if (!hfsplus_read_mdb(p, &wrapper))
            return S_FALSE;
        if (wrapper.unprotected)
            ReadLimitations =
                "The HFS wrapper lacks protection flags. Embedded volume data may be inconsistent.";
        const UInt32 blockSize = wrapper.ablk_size;
        if (blockSize == 0 || (blockSize & 0x1ff))
            return S_FALSE;
        const unsigned numBlocks = Get16a(p + 0x12); // drNmAlBlks
        // UInt16 drFreeBks = Get16a(p + 0x22); // number of unused allocation blocks
        /*
        we suppose that it has the following layout:
        {
          start data with header
          blocks[h.NumBlocks]
          end data with header (probably size_of_footer <= blockSize).
        }
        */
        // PhySize2 = ((UInt64)numBlocks + 2) * blockSize;
        const unsigned sector_of_FirstBlock = wrapper.ablk_start;
        const UInt32 startBlock = wrapper.embed_start;
        const UInt32 blockCount = wrapper.embed_count;
        if (startBlock > numBlocks || blockCount == 0 || blockCount > numBlocks - startBlock)
            return S_FALSE;
        const UInt64 nextSize = (UInt64)blockCount * blockSize;
        const UInt64 nextOffset =
            ((UInt64)sector_of_FirstBlock << 9) + (UInt64)startBlock * blockSize;
        RINOK(InStream_GetSize_SeekToEnd(inStream, ArcFileSize))
        if (!nextOffset || nextSize < kHfsHeaderSize || SpecOffset > ArcFileSize ||
            nextOffset > ArcFileSize - SpecOffset ||
            nextSize > ArcFileSize - SpecOffset - nextOffset ||
            (embeddedSize && (nextOffset > embeddedSize || nextSize > embeddedSize - nextOffset)))
            return S_FALSE;
        const UInt64 wrapperSize =
            ((UInt64)sector_of_FirstBlock << 9) + (UInt64)numBlocks * blockSize;
        if (wrapperSize > ArcFileSize - SpecOffset || (embeddedSize && wrapperSize > embeddedSize))
            return S_FALSE;
        const UInt64 wrapperEnd = SpecOffset + wrapperSize;
        PhySize2 = MyMax(PhySize2, wrapperEnd);
        const UInt32 tail = MyMax(UInt32(1 << 10), blockSize);
        if (ArcFileSize > wrapperEnd && ArcFileSize - wrapperEnd <= tail)
            PhySize2 = ArcFileSize;
        SpecOffset += nextOffset;
        embeddedSize = nextSize;
        if (progress)
            RINOK(progress->SetCompleted(NULL, &SpecOffset))
        RINOK(InStream_SeekSet(inStream, SpecOffset))
        RINOK(ReadStream_FALSE(inStream, buf32, kHfsHeaderSize))
    }

    // HFS+ / HFSX volume header (starting from offset==1024):
    {
        const UInt16 sig = GetUi16a(p);
        if ((sig != k_Signature_LE16_HPLUS && sig != k_Signature_LE16_HFSX) ||
            !sevenzip_hfsplus_volume_version(Get16a(p + 2)))
            return S_FALSE;
        h.Is_Hsfx_ver5 = sig == k_Signature_LE16_HFSX;
    }
    {
        const UInt32 blockSize = Get32a(p + 0x28);
        unsigned i;
        for (i = 9; ((UInt32)1 << i) != blockSize; i++)
            if (i == 31)
                return S_FALSE;
        h.BlockSizeLog = i;
        h.BlockSize = blockSize;
    }

    // Linux hfsplus_fill_super permits read-only access without UNMNT.
    if (!(Get32a(p + 4) & (1 << 8))) {
        if (!ReadLimitations.IsEmpty())
            ReadLimitations += '\n';
        ReadLimitations += "Volume was not properly unmounted. Some data may be corrupt.";
    }
    // h.Attr = Get32a(p + 4);
    // h.LastMountedVersion = Get32a(p + 8);
    // h.JournalInfoBlock = Get32a(p + 0xC);
    h.CTime = Get32a(p + 0x10);
    h.MTime = Get32a(p + 0x14);
    // h.BackupTime = Get32a(p + 0x18);
    // h.CheckedTime = Get32a(p + 0x1C);
    h.NumFiles = Get32a(p + 0x20);
    h.NumFolders = Get32a(p + 0x24);
    if (h.NumFolders > ((UInt32)1 << 29) || h.NumFiles > ((UInt32)1 << 30))
        return S_FALSE;

    RINOK(InStream_GetSize_SeekToEnd(inStream, ArcFileSize))
    if (progress) {
        const UInt64 numFiles = (UInt64)h.NumFiles + h.NumFolders + 1;
        RINOK(progress->SetTotal(&numFiles, NULL))
    }

    h.NumBlocks = Get32a(p + 0x2C);
    h.NumFreeBlocks = Get32a(p + 0x30);
    const UInt64 volumeSize = (UInt64)h.NumBlocks << h.BlockSizeLog;
    if (h.NumBlocks == 0 || SpecOffset > ArcFileSize || volumeSize > ArcFileSize - SpecOffset ||
        (embeddedSize && volumeSize > embeddedSize))
        return S_FALSE;
    // hfsplus_read_wrapper checks the signature of the secondary header at
    // the end of its selected volume. A bare archive supplies its own extent.
    const UInt64 selectedSize = embeddedSize ? embeddedSize : volumeSize;
    if (selectedSize < kHfsHeaderSize)
        return S_FALSE;
    UInt32 backupHeader[512 / sizeof(UInt32)];
    RINOK(InStream_SeekSet(inStream, SpecOffset + selectedSize - 1024))
    RINOK(ReadStream_FALSE(inStream, backupHeader, sizeof(backupHeader)))
    if (GetUi16((const Byte *)backupHeader) != GetUi16a(p))
        return S_FALSE;
    /*
    h.NextCalatlogNodeID = Get32(p + 0x40);
    h.WriteCount = Get32(p + 0x44);
    for (i = 0; i < 6; i++)
      h.FinderInfo[i] = Get32(p + 0x50 + i * 4);
    h.VolID = Get64(p + 0x68);
    */

    ResFileName = kResFileName;

    CFork extentsFork, catalogFork, attrFork;
    // allocationFork.Parse(p + 0x70 + 0x50 * 0);
    extentsFork.Parse(p + 0x70 + 0x50 * 1);
    catalogFork.Parse(p + 0x70 + 0x50 * 2);
    attrFork.Parse(p + 0x70 + 0x50 * 3);
    // startupFork.Parse(p + 0x70 + 0x50 * 4);

    CObjectVector<CIdExtents> overflowExtents[2];
    if (!extentsFork.IsOk(Header.BlockSizeLog))
        HeadersError = true;
    else {
        const HRESULT res = LoadExtentFile(extentsFork, inStream, overflowExtents);
        if (res == S_FALSE)
            HeadersError = true;
        else if (res != S_OK)
            return res;
    }

    if (!catalogFork.UpgradeAndTest(overflowExtents[0], kHfsID_CatalogFile, Header.BlockSizeLog))
        return S_FALSE;

    if (!attrFork.UpgradeAndTest(overflowExtents[0], kHfsID_AttributesFile, Header.BlockSizeLog))
        HeadersError = true;
    else {
        if (attrFork.Size != 0)
            RINOK(LoadAttrs(attrFork, inStream, progress))
    }

    RINOK(LoadCatalog(catalogFork, overflowExtents, inStream, progress))

    // PhySize = Header.GetPhySize();
    return S_OK;
}

// clang-format off
Z7_class_CHandler_final:
  public IInArchive,
  public IArchiveGetRawProps,
  public IInArchiveGetStream,
  public CMyUnknownImp,
  public CDatabase
{
  Z7_IFACES_IMP_UNK_3(
      IInArchive,
      IArchiveGetRawProps,
      IInArchiveGetStream)

  CMyComPtr<IInStream> _stream;
  HRESULT GetForkStream(const CFork &fork, ISequentialInStream **stream);
};
// clang-format on

static const char *const g_BsdFlags[] = {
    "User NoDump", "User Immutable",   "User Append",  NULL, NULL, NULL, NULL, NULL,
    NULL,          "System Immutable", "System Append"};

static const Byte kProps[] = {
    kpidPath,        kpidIsDir,    kpidSize,        kpidPackSize,    kpidCTime,
    kpidMTime,       kpidATime,    kpidChangeTime,  kpidPosixAttrib, kpidUserId,
    kpidGroupId,     kpidINode,    kpidLinks,       kpidSymLink,
#ifdef HFS_SHOW_ALT_STREAMS
    kpidIsAltStream,
#endif
    kpidMethod,      kpidCharacts, kpidDeviceMajor, kpidDeviceMinor,
};

static const PROPID kpidReadLimitations = kpidUserDefined;
static const CStatProp kArcProps[] = {
    {nullptr, kpidMethod, VT_BSTR},     {nullptr, kpidCharacts, VT_BSTR},
    {nullptr, kpidClusterSize, VT_UI4}, {nullptr, kpidFreeSpace, VT_UI8},
    {nullptr, kpidCTime, VT_FILETIME},  {nullptr, kpidMTime, VT_FILETIME},
    {nullptr, kpidReadOnly, VT_BOOL},   {"Read-only limitations", kpidReadLimitations, VT_BSTR}};
IMP_IInArchive_Props IMP_IInArchive_ArcProps_WITH_NAME

    static void
    HfsTimeToProp(UInt32 hfsTime, NWindows::NCOM::CPropVariant &prop) {
    FILETIME ft;
    HfsTimeToFileTime(hfsTime, ft);
    prop.SetAsTimeFrom_FT_Prec(ft, k_PropVar_TimePrec_Base);
}

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    switch (propID) {
    case kpidReadOnly:
        prop = true;
        break;
    case kpidReadLimitations: {
        AString reasons = ReadLimitations;
        if (Header.NumFreeBlocks > Header.NumBlocks) {
            if (!reasons.IsEmpty())
                reasons += '\n';
            reasons += "The free-space count exceeds the volume size. Free space is unknown.";
        }
        if (!reasons.IsEmpty())
            prop = reasons;
        break;
    }
    case kpidExtension:
        prop = Header.IsHfsX() ? "hfsx" : "hfs";
        break;
    case kpidMethod:
        prop = Header.IsHfsX() ? "HFSX" : "HFS+";
        break;
    case kpidCharacts:
        MethodsMaskToProp(MethodsMask, prop);
        break;
    case kpidPhySize: {
        UInt64 v = SpecOffset + Header.GetPhySize(); // PhySize;
        if (v < PhySize2)
            v = PhySize2;
        prop = v;
        break;
    }
    case kpidClusterSize:
        prop = Header.GetBlockSize();
        break;
    case kpidFreeSpace:
        if (Header.NumFreeBlocks > Header.NumBlocks)
            break;
        prop = (UInt64)Header.GetFreeSize();
        break;
    case kpidMTime:
        HfsTimeToProp(Header.MTime, prop);
        break;
    case kpidCTime: {
        if (Header.CTime != 0) {
            FILETIME localFt, ft;
            HfsTimeToFileTime(Header.CTime, localFt);
            if (LocalFileTimeToFileTime(&localFt, &ft))
                prop.SetAsTimeFrom_FT_Prec(ft, k_PropVar_TimePrec_Base);
        }
        break;
    }
    case kpidIsTree:
        prop = true;
        break;
    case kpidErrorFlags: {
        UInt32 flags = 0;
        if (HeadersError)
            flags |= kpv_ErrorFlags_HeadersError;
        if (UnsupportedFeature)
            flags |= kpv_ErrorFlags_UnsupportedFeature;
        if (flags != 0)
            prop = flags;
        break;
    }
    case kpidIsAltStream:
        prop = ThereAreAltStreams;
        break;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetNumRawProps(UInt32 *numProps)) {
    *numProps = 0;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetRawPropInfo(UInt32 /* index */, BSTR *name, PROPID *propID)) {
    *name = NULL;
    *propID = 0;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetParent(UInt32 index, UInt32 *parent, UInt32 *parentType)) {
    const CRef &ref = Refs[index];
    *parentType = ref.IsAltStream() ? NParentType::kAltStream : NParentType::kDir;
    *parent = (UInt32)(Int32)ref.Parent;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetRawProp(UInt32 index, PROPID propID, const void **data, UInt32 *dataSize,
                                  UInt32 *propType)) {
    *data = NULL;
    *dataSize = 0;
    *propType = 0;
#ifdef MY_CPU_LE
    if (propID == kpidName) {
        const CRef &ref = Refs[index];
        const UString *s;
        if (ref.IsResource())
            s = &ResFileName;
        else if (ref.AttrIndex >= 0)
            s = &Attrs[ref.AttrIndex].Name;
        else
            s = &Items[ref.ItemIndex].Name;
        *data = (const wchar_t *)(*s);
        *dataSize = (s->Len() + 1) * (UInt32)sizeof(wchar_t);
        *propType = PROP_DATA_TYPE_wchar_t_PTR_Z_LE;
        return S_OK;
    }
#else
    UNUSED_VAR(index)
    UNUSED_VAR(propID)
#endif
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    const CRef &ref = Refs[index];
    const CItem &item = GetDataItem(ref);
    switch (propID) {
    case kpidPath:
        GetItemPath(index, prop);
        break;
    case kpidName: {
        const UString *s;
        if (ref.IsResource())
            s = &ResFileName;
        else if (ref.AttrIndex >= 0)
            s = &Attrs[ref.AttrIndex].Name;
        else
            s = &Items[ref.ItemIndex].Name;
        prop = *s;
        break;
    }
    case kpidPackSize: {
        UInt64 size;
        if (ref.AttrIndex >= 0)
            size = Attrs[ref.AttrIndex].GetSize();
        else if (ref.IsResource())
            size = (UInt64)item.ResourceFork.NumBlocks * Header.GetBlockSize();
        else if (item.IsDir())
            break;
        else if (item.CompressHeader.IsCorrect) {
            if (item.CompressHeader.IsMethod_Resource())
                size = (UInt64)item.ResourceFork.NumBlocks * Header.GetBlockSize();
            else if (item.decmpfs_AttrIndex >= 0) {
                // size = item.PackSize;
                const CAttr &attr = Attrs[item.decmpfs_AttrIndex];
                size = attr.Data.Size() - item.CompressHeader.DataPos;
            } else
                size = 0;
        } else
            size = (UInt64)item.DataFork.NumBlocks * Header.GetBlockSize();
        prop = size;
        break;
    }
    case kpidSize: {
        UInt64 size;
        if (ref.AttrIndex >= 0)
            size = Attrs[ref.AttrIndex].GetSize();
        else if (ref.IsResource())
            size = item.ResourceFork.Size;
        else if (item.IsDir())
            break;
        else if (item.CompressHeader.IsCorrect)
            size = item.CompressHeader.UnpackSize;
        else
            size = item.DataFork.Size;
        prop = size;
        break;
    }
    case kpidIsDir:
        prop = (ref.IsItem() && item.IsDir());
        break;
    case kpidIsAltStream:
        prop = ref.IsAltStream();
        break;
    case kpidCTime:
        HfsTimeToProp(item.CTime, prop);
        break;
    case kpidMTime:
        HfsTimeToProp(item.MTime, prop);
        break;
    case kpidATime:
        HfsTimeToProp(item.ATime, prop);
        break;
    case kpidChangeTime:
        HfsTimeToProp(item.AttrMTime, prop);
        break;
    case kpidPosixAttrib:
        if (ref.IsItem())
            prop = (UInt32)item.FileMode;
        break;
    case kpidUserId:
        prop = item.OwnerID;
        break;
    case kpidCharacts:
        if (ref.IsItem())
            FLAGS_TO_PROP(g_BsdFlags, (UInt32)item.OwnerFlags | ((UInt32)item.AdminFlags << 8),
                          prop);
        break;
    case kpidGroupId:
        prop = item.GroupID;
        break;
    case kpidINode:
    case kpidStreamId:
        if (ref.IsItem())
            prop = item.ID;
        break;
    case kpidLinks:
        if (ref.IsItem())
            prop = item.NumLinks;
        break;
    case kpidDeviceMajor:
    case kpidDeviceMinor:
        if (ref.IsItem() &&
            ((item.FileMode & 0170000) == 0020000 || (item.FileMode & 0170000) == 0060000))
            prop = propID == kpidDeviceMajor ? item.DeviceMajor : item.DeviceMinor;
        break;
    case kpidSymLink:
        if (ref.IsItem() && (item.FileMode & 0170000) == 0120000) {
            CByteBuffer data(SEVENZIP_PAGE_LINK_SIZE);
            memset(data, 0, data.Size());
            if (item.DataFork.Size) {
                CMyComPtr<ISequentialInStream> input;
                RINOK(GetForkStream(item.DataFork, &input))
                const size_t size = (size_t)MyMin(item.DataFork.Size, UInt64(data.Size()));
                RINOK(ReadStream_FALSE(input, data, size))
            }
            const size_t targetSize = sevenzip_page_link(data, item.DataFork.Size);
            AString target;
            target.SetFrom((const char *)(const Byte *)data, (unsigned)targetSize);
            UString text;
            if (!ConvertUTF8ToUnicode(target, text))
                MultiByteToUnicodeString2(text, target);
            prop = text;
        }
        break;

    case kpidMethod:
        if (ref.IsItem())
            item.CompressHeader.MethodToProp(prop);
        break;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Open(IInStream *inStream, const UInt64 * /* maxCheckStartPosition */,
                            IArchiveOpenCallback *callback)) {
    COM_TRY_BEGIN
    Close();
    HRESULT result;
    try {
        result = Open2(inStream, callback);
    } catch (...) {
        Close();
        throw;
    }
    if (result != S_OK) {
        Close();
        return result;
    }
    _stream = inStream;
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Close()) {
    _stream.Release();
    Clear();
    return S_OK;
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems, Int32 testMode,
                               IArchiveExtractCallback *extractCallback)) {
    COM_TRY_BEGIN
    const bool allFilesMode = (numItems == (UInt32)(Int32)-1);
    if (allFilesMode)
        numItems = Refs.Size();
    if (numItems == 0)
        return S_OK;
    UInt32 i;
    UInt64 totalSize = 0;
    for (i = 0; i < numItems; i++) {
        const CRef &ref = Refs[allFilesMode ? i : indices[i]];
        totalSize += Get_UnpackSize_of_Ref(ref);
    }
    RINOK(extractCallback->SetTotal(totalSize))

    UInt64 currentTotalSize = 0, currentItemSize = 0;

    const size_t kBufSize = kCompressionBlockSize;
    CByteBuffer buf(kBufSize + 0x10); // we need 1 additional bytes for uncompressed chunk header

    // there are hfs without adler in zlib.
    CDecoder decoder(true); // IsAdlerOptional

    for (i = 0;; i++, currentTotalSize += currentItemSize) {
        RINOK(extractCallback->SetCompleted(&currentTotalSize))
        if (i >= numItems)
            break;
        const UInt32 index = allFilesMode ? i : indices[i];
        const CRef &ref = Refs[index];
        const CItem &item = GetDataItem(ref);
        currentItemSize = Get_UnpackSize_of_Ref(ref);

        int opRes;
        {
            CMyComPtr<ISequentialOutStream> realOutStream;
            const Int32 askMode =
                testMode ? NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
            RINOK(extractCallback->GetStream(index, &realOutStream, askMode))

            if (ref.IsItem() && item.IsDir()) {
                RINOK(extractCallback->PrepareOperation(askMode))
                RINOK(extractCallback->SetOperationResult(NExtract::NOperationResult::kOK))
                continue;
            }
            if (!testMode && !realOutStream)
                continue;

            RINOK(extractCallback->PrepareOperation(askMode))

            UInt64 pos = 0;
            opRes = NExtract::NOperationResult::kDataError;
            const CFork *fork = NULL;

            if (item.LinkError || item.UnsupportedLink) {
                if (item.UnsupportedLink)
                    opRes = NExtract::NOperationResult::kUnsupportedMethod;
            } else if (ref.AttrIndex >= 0) {
                const CAttr &attr = Attrs[ref.AttrIndex];
                if (attr.Fork_defined && attr.Data.Size() == 0)
                    fork = &attr.Fork;
                else {
                    opRes = NExtract::NOperationResult::kOK;
                    if (realOutStream) {
                        RINOK(WriteStream(realOutStream,
                                          // AttrBuf + attr.Pos, attr.Size
                                          attr.Data, attr.Data.Size()))
                    }
                }
            } else if (ref.IsResource())
                fork = &item.ResourceFork;
            else if (item.CompressHeader.IsSupported) {
                CMyComPtr<ISequentialInStream> inStreamFork;
                UInt64 forkSize = 0;
                const CByteBuffer *decmpfs_Data = NULL;

                if (item.CompressHeader.IsMethod_Resource()) {
                    const CFork &resourceFork = item.ResourceFork;
                    forkSize = resourceFork.Size;
                    const HRESULT result = GetForkStream(resourceFork, &inStreamFork);
                    if (result != S_OK && result != S_FALSE)
                        return result;
                } else {
                    const CAttr &attr = Attrs[item.decmpfs_AttrIndex];
                    decmpfs_Data = &attr.Data;
                }

                if (inStreamFork || decmpfs_Data) {
                    const HRESULT hres =
                        decoder.Extract(inStreamFork, realOutStream, forkSize, item.CompressHeader,
                                        decmpfs_Data, currentTotalSize, extractCallback, opRes);
                    if (hres != S_FALSE && hres != S_OK)
                        return hres;
                }
            } else if (item.CompressHeader.IsCorrect)
                opRes = NExtract::NOperationResult::kUnsupportedMethod;
            else
                fork = &item.DataFork;

            if (fork) {
                CMyComPtr<ISequentialInStream> inStreamFork;
                const HRESULT streamResult = GetForkStream(*fork, &inStreamFork);
                if (streamResult != S_OK && streamResult != S_FALSE)
                    return streamResult;
                if (streamResult == S_OK) {
                    while (pos < fork->Size) {
                        size_t cur = kBufSize;
                        if (cur > fork->Size - pos)
                            cur = (size_t)(fork->Size - pos);
                        const HRESULT result = ReadStream(inStreamFork, buf, &cur);
                        if (result != S_OK && result != S_FALSE)
                            return result;
                        if (cur != 0 && realOutStream) {
                            RINOK(WriteStream(realOutStream, buf, cur))
                        }
                        pos += cur;
                        const UInt64 processed = currentTotalSize + pos;
                        RINOK(extractCallback->SetCompleted(&processed))
                        if (result == S_FALSE || cur == 0)
                            break;
                    }
                    if (pos == fork->Size)
                        opRes = NExtract::NOperationResult::kOK;
                }
            }
        }
        RINOK(extractCallback->SetOperationResult(opRes))
    }
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems)) {
    *numItems = Refs.Size();
    return S_OK;
}

HRESULT CHandler::GetForkStream(const CFork &fork, ISequentialInStream **stream) {
    *stream = NULL;

    if (!CheckFork(fork))
        return S_FALSE;

    CMyComPtr2<ISequentialInStream, CExtentsStream> extentStream;
    extentStream.Create_if_Empty();

    UInt64 rem = fork.Size;
    UInt64 virt = 0;

    FOR_VECTOR(i, fork.Extents) {
        if (rem == 0)
            break;
        const CExtent &e = fork.Extents[i];
        if (e.NumBlocks == 0)
            continue;
        UInt64 cur = (UInt64)e.NumBlocks * Header.GetBlockSize();
        if (cur > rem) {
            cur = rem;
        }
        CSeekExtent se;
        se.Phy = SpecOffset + (UInt64)e.Pos * Header.GetBlockSize();
        se.Virt = virt;
        virt += cur;
        rem -= cur;
        extentStream->Extents.Add(se);
    }

    if (rem != 0)
        return S_FALSE;

    CSeekExtent se;
    se.Phy = 0; // = SpecOffset ?
    se.Virt = virt;
    extentStream->Extents.Add(se);
    extentStream->Stream = _stream;
    extentStream->Init();
    *stream = extentStream.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream **stream)) {
    COM_TRY_BEGIN
    *stream = NULL;
    if (index >= Refs.Size())
        return E_INVALIDARG;

    const CRef &ref = Refs[index];
    if (GetDataItem(ref).UnsupportedLink || GetDataItem(ref).LinkError)
        return S_FALSE;
    const CFork *fork = NULL;
    if (ref.AttrIndex >= 0) {
        const CAttr &attr = Attrs[ref.AttrIndex];
        if (!attr.Fork_defined) {
            Create_BufInStream_WithNewBuffer(attr.Data, stream);
            return S_OK;
        }
        fork = &attr.Fork;
    } else {
        const CItem &item = GetDataItem(ref);
        if (ref.IsResource())
            fork = &item.ResourceFork;
        else if (item.IsDir())
            return S_FALSE;
        else if (item.CompressHeader.IsCorrect) {
            CMyComPtr<ISequentialInStream> resource;
            const CByteBuffer *data = nullptr;
            if (item.CompressHeader.IsMethod_Resource()) {
                RINOK(GetForkStream(item.ResourceFork, &resource))
            } else if (item.decmpfs_AttrIndex >= 0)
                data = &Attrs[item.decmpfs_AttrIndex].Data;
            return CreateDecmpfsStream(resource, item.ResourceFork.Size, item.CompressHeader, data,
                                       stream);
        } else
            fork = &item.DataFork;
    }
    return GetForkStream(*fork, stream);
    COM_TRY_END
}

static const Byte k_Signature[] = {2, 'B', 'D', 2, 'H', '+', 2, 'H', 'X'};
REGISTER_ARC_I("HFS", "hfs hfsx", NULL, 0xE3, k_Signature, kHeaderPadSize,
               NArcInfoFlags::kMultiSignature, IsArc_HFS)

} // namespace NHfs
} // namespace NArchive
