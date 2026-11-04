// Adapted from 7-Zip CPP/7zip/UI/Agent/Agent.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source CProxyArc/CProxyArc2 walks, binding and properties are retained.
// Qt metadata and archive services supply the portable engine boundary.
#include "archive/agent/Agent.h"
#include "7zip/PropID.h"
#include "C/Sort.h"
#include "Common/ComTry.h"
#include "Common/Defs.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/System.h"
#include "archive/callbacks/ArchiveExtractCallback.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/resources/qtarchiveagentopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "devices/deviceaccess.h"
#include "filemanager/panels/PanelSort.h"
#include "fileops/native/FileDir.h"
#include "settings/ViewSettings.h"
#include <QScopeGuard>

static const bool k_keepEmptyDirPrefixes = false; // upstream 22.00 and later

Z7_COM7F_IMF(CAgentFolder::GetAgentFolder(CAgentFolder **agentFolder)) {
    *agentFolder = this;
    return S_OK;
}

void CAgentFolder::LoadFolder(unsigned dirIndex) {
    CProxyItem item;
    if (_proxy2) {
        CUIntVector vec;
        unsigned i = 0;
        for (;;) {
            const CProxyDir2 &dir = _proxy2->Dirs[dirIndex];
            if (i == dir.Items.Size()) {
                const unsigned num = vec.Size();
                if (num < 2)
                    return;
                dirIndex = vec[num - 2];
                i = vec[num - 1];
                vec.DeleteFrom(num - 2);
                continue;
            }
            item.DirIndex = dirIndex;
            item.Index = i;
            _items.Add(item);
            const unsigned index = dir.Items[i];
            i++;
            const CProxyFile2 &file = _proxy2->Files[index];
            if (_loadAltStreams && file.AltDirIndex != -1)
                LoadFolder((unsigned)file.AltDirIndex);
            if (file.DirIndex != -1) {
                vec.Add(dirIndex);
                vec.Add(i);
                dirIndex = (unsigned)file.DirIndex;
                i = 0;
            }
        }
    }
    item.DirIndex = dirIndex;
    const CProxyDir &dir = _proxy->Dirs[dirIndex];
    unsigned i;
    for (i = 0; i < dir.SubDirs.Size(); i++) {
        item.Index = i;
        _items.Add(item);
        LoadFolder(dir.SubDirs[i]);
    }
    const unsigned start = dir.SubDirs.Size();
    for (i = 0; i < dir.SubFiles.Size(); i++) {
        item.Index = start + i;
        _items.Add(item);
    }
}

Z7_COM7F_IMF(CAgentFolder::LoadItems()) {
    if (!_agentSpec || !_agentSpec->_archiveLink.IsOpen || !IsBound())
        return E_FAIL;
    RINOK(LoadDirectoryItems(_flatMode))
    _items.Clear();
    if (_flatMode) {
        LoadFolder(_proxyDirIndex);
        if (_proxy2 && _loadAltStreams) {
            if (_proxyDirIndex == k_Proxy2_RootDirIndex)
                LoadFolder(k_Proxy2_AltRootDirIndex);
        }
    }
    return S_OK;
}

unsigned CAgentFolder::GetNumberOfItems() const {
    UInt32 numItems = 0;
    // Legacy Qt enumeration convenience; one source interface body owns count.
    const_cast<CAgentFolder *>(this)->GetNumberOfItems(&numItems);
    return numItems;
}

CProxyItem CAgentFolder::GetItem(unsigned index) const {
    if (_flatMode)
        return _items[index];
    return {_proxyDirIndex, index};
}

bool CAgentFolder::IsBound() const {
    if (_closedArchivePath || !_agentSpec || !_agentSpec->_archiveLink.IsOpen ||
        _proxyGeneration != _agentSpec->_proxyGeneration.load())
        return false;
    return _proxy2 ? _proxyDirIndex < _proxy2->Dirs.Size()
                   : _proxy && _proxyDirIndex < _proxy->Dirs.Size();
}

void CAgentFolder::Init(const CProxyArc *proxy, const CProxyArc2 *proxy2, unsigned proxyDirIndex,
                        CAgent *agent) {
    _proxy = proxy;
    _proxy2 = proxy2;
    _proxyGeneration = agent->_proxyGeneration.load();
    _proxyDirIndex = proxyDirIndex;
    _isAltStreamFolder = false;
    if (_proxy2)
        _isAltStreamFolder = _proxy2->IsAltDir(proxyDirIndex);
    _agent = agent;
    _agentSpec = agent;
    _archiveType = agent->ArchiveType;
}

IInArchive *CAgentFolder::GetArchiveProperties() const {
    return _agentSpec ? _agentSpec->GetArchive() : nullptr;
}

HRESULT CAgentFolder::GetArchiveItemPath(UInt32 index, UString &path) const {
    return _agentSpec ? _agentSpec->GetArc().GetItem_Path(index, path) : E_FAIL;
}

Z7_COM7F_IMF(CAgentFolder::GetNumberOfItems(UInt32 *numItems)) {
    if (!IsBound()) {
        *numItems = 0;
        return E_FAIL;
    }
    if (_flatMode)
        *numItems = _items.Size();
    else if (_proxy2)
        *numItems = _proxy2->Dirs[_proxyDirIndex].Items.Size();
    else {
        const CProxyDir *dir = &_proxy->Dirs[_proxyDirIndex];
        *numItems = (UInt32)(dir->SubDirs.Size() + dir->SubFiles.Size());
    }
    return S_OK;
}

#define SET_realIndex_AND_dir                                                                      \
    unsigned realIndex;                                                                            \
    const CProxyDir *dir;                                                                          \
    if (_flatMode) {                                                                               \
        const CProxyItem &item = _items[index];                                                    \
        dir = &_proxy->Dirs[item.DirIndex];                                                        \
        realIndex = item.Index;                                                                    \
    } else {                                                                                       \
        dir = &_proxy->Dirs[_proxyDirIndex];                                                       \
        realIndex = index;                                                                         \
    }

#define SET_realIndex_AND_dir_2                                                                    \
    unsigned realIndex;                                                                            \
    const CProxyDir2 *dir;                                                                         \
    if (_flatMode) {                                                                               \
        const CProxyItem &item = _items[index];                                                    \
        dir = &_proxy2->Dirs[item.DirIndex];                                                       \
        realIndex = item.Index;                                                                    \
    } else {                                                                                       \
        dir = &_proxy2->Dirs[_proxyDirIndex];                                                      \
        realIndex = index;                                                                         \
    }

Z7_COM7F_IMF2(UInt64, CAgentFolder::GetItemSize(const UInt32 index)) {
    if (!IsBound() || index >= GetNumberOfItems())
        return 0;
    unsigned arcIndex;
    if (_proxy2) {
        SET_realIndex_AND_dir_2 arcIndex = dir->Items[realIndex];
        const CProxyFile2 &item = _proxy2->Files[arcIndex];
        if (item.IsDir()) {
            const CProxyDir2 &itemFolder = _proxy2->Dirs[item.DirIndex];
            if (!_flatMode)
                return itemFolder.Size;
        }
    } else {
        SET_realIndex_AND_dir if (realIndex < dir->SubDirs.Size()) {
            const CProxyDir &item = _proxy->Dirs[dir->SubDirs[realIndex]];
            if (!_flatMode)
                return item.Size;
            if (!item.IsLeaf())
                return 0;
            arcIndex = (unsigned)item.ArcIndex;
        }
        else {
            arcIndex = dir->SubFiles[realIndex - dir->SubDirs.Size()];
        }
    }
    NWindows::NCOM::CPropVariant prop;
    _agentSpec->GetArchive()->GetProperty(arcIndex, kpidSize, &prop);
    if (prop.vt == VT_UI8)
        return prop.uhVal.QuadPart;
    else
        return 0;
}

Z7_COM7F_IMF(CAgentFolder::GetItemName(UInt32 index, const wchar_t **name, unsigned *len)) {
    *name = nullptr;
    *len = 0;
    if (!IsBound() || index >= GetNumberOfItems())
        return E_INVALIDARG;
    if (_proxy2) {
        SET_realIndex_AND_dir_2 unsigned arcIndex = dir->Items[realIndex];
        const CProxyFile2 &item = _proxy2->Files[arcIndex];
        *name = item.Name;
        *len = item.NameLen;
        return S_OK;
    } else {
        SET_realIndex_AND_dir if (realIndex < dir->SubDirs.Size()) {
            const CProxyDir &item = _proxy->Dirs[dir->SubDirs[realIndex]];
            *name = item.Name;
            *len = item.NameLen;
            return S_OK;
        }
        else {
            const CProxyFile &item = _proxy->Files[dir->SubFiles[realIndex - dir->SubDirs.Size()]];
            *name = item.Name;
            *len = item.NameLen;
            return S_OK;
        }
    }
}

/* called for (_flatMode == true) from:
    CPanel::RefreshListCtrl() : to check selected items and focused item.
    CPanel::SetItemText() : to show prefix column
*/
Z7_COM7F_IMF(CAgentFolder::GetItemPrefix(UInt32 index, const wchar_t **name, unsigned *len)) {
    *name = NULL;
    *len = 0;
    if (!IsBound() || index >= GetNumberOfItems())
        return E_INVALIDARG;
#ifdef Z7_AGENT_PROXY2_USE_DIR_PATH_PREFIX
    if (!_flatMode)
        return S_OK;

    if (_proxy2) {
        const CProxyItem &item = _items[index];
        const CProxyDir2 &dir = _proxy2->Dirs[item.DirIndex];
        const CProxyDir2 &baseDir = _proxy2->Dirs[_proxyDirIndex];
        const UString &s = dir.PathPrefix;
        if (dir.IsLongPath) {
            // in case of IsLongPath we can return reduced prefix or return nothing.
            // so caller will call it with full prefix.
#if 1 // 1 - for fast processing of long strings, 0 - for full processing
            *name = (const wchar_t *)s;
            *len = s.Len();
#endif
        } else {
            if (baseDir.IsLongPath)
                return E_FAIL; // throw 1;
            const unsigned baseLen = baseDir.PathPrefix.Len();
            if (s.Len() < baseLen)
                return E_FAIL; // throw 1;
            *name = (const wchar_t *)s + baseLen;
            *len = s.Len() - baseLen;
        }
    }
#else
    UNUSED_VAR(index)
#endif
    return S_OK;
}

UString CAgentFolder::GetName(UInt32 index) const {
    if (!IsBound() || index >= GetNumberOfItems())
        return UString();
    if (_proxy2) {
        const CProxyItem item = GetItem(index);
        const CProxyDir2 *dir = &_proxy2->Dirs[item.DirIndex];
        const unsigned realIndex = item.Index;
        return _proxy2->Files[dir->Items[realIndex]].Name;
    }
    SET_realIndex_AND_dir;
    if (realIndex < dir->SubDirs.Size())
        return _proxy->Dirs[dir->SubDirs[realIndex]].Name;
    return _proxy->Files[dir->SubFiles[realIndex - dir->SubDirs.Size()]].Name;
}

void CAgentFolder::GetPrefix(UInt32 index, UString &prefix) const {
    prefix.Empty();
    if (!_flatMode)
        return;
    const unsigned itemDirIndex = _items[index].DirIndex;
    if (itemDirIndex == _proxyDirIndex)
        return;
    if (_proxy2) {
        const unsigned kLenLimit = 1u << 12;
        _proxy2->GetDirPath_as_Prefix_from_Base(itemDirIndex, prefix, _proxyDirIndex, kLenLimit);
    } else
        _proxy->GetDirPath_as_Prefix_from_Base(itemDirIndex, prefix, _proxyDirIndex);
}

UString CAgentFolder::GetFullPrefix(const UInt32 index) const {
    if (!IsBound() || index >= GetNumberOfItems())
        return UString();
    unsigned foldIndex = _proxyDirIndex;
    if (_flatMode)
        foldIndex = _items[index].DirIndex;
    if (_proxy2)
        return _proxy2->GetDirPath_as_Prefix(foldIndex);
    return _proxy->GetDirPath_as_Prefix(foldIndex);
}

Z7_COM7F_IMF(CAgentFolder::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    if (!IsBound() || index >= GetNumberOfItems())
        return E_INVALIDARG;
    NWindows::NCOM::CPropVariant prop;
    if (propID == kpidPrefix) {
        if (_flatMode) {
            UString prefix;
            GetPrefix(index, prefix);
            prop = prefix;
        }
    } else if (_proxy2) {
        const CProxyItem proxyItem = GetItem(index);
        const CProxyDir2 *dir = &_proxy2->Dirs[proxyItem.DirIndex];
        const unsigned realIndex = proxyItem.Index;
        const unsigned arcIndex = dir->Items[realIndex];
        const CProxyFile2 &item = _proxy2->Files[arcIndex];
        if (item.IsDir() &&
            (propID == kpidSize || propID == kpidPackSize || propID == kpidNumSubDirs ||
             propID == kpidNumSubFiles) &&
            !DirectoryItemSubtreeComplete(arcIndex)) {
            prop.Detach(value);
            return S_OK;
        }
        if (!item.IsDir()) {
            switch (propID) {
            case kpidIsDir:
                prop = false;
                break;
            case kpidName:
                prop = item.Name;
                break;
            default:
                return _agentSpec->GetArchive()->GetProperty(arcIndex, propID, value);
            }
        } else {
            const CProxyDir2 &itemFolder = _proxy2->Dirs[item.DirIndex];
            if (!_flatMode && propID == kpidSize)
                prop = itemFolder.Size;
            else if (!_flatMode && propID == kpidPackSize)
                prop = itemFolder.PackSize;
            else
                switch (propID) {
                case kpidIsDir:
                    prop = true;
                    break;
                case kpidNumSubDirs:
                    prop = itemFolder.NumSubDirs;
                    break;
                case kpidNumSubFiles:
                    prop = itemFolder.NumSubFiles;
                    break;
                case kpidName:
                    prop = item.Name;
                    break;
                case kpidCRC:
                    if (!item.Ignore) {
                        RINOK(_agentSpec->GetArchive()->GetProperty(arcIndex, propID, value))
                    }
                    if (itemFolder.CrcIsDefined && value->vt == VT_EMPTY)
                        prop = itemFolder.Crc;
                    break;
                default:
                    if (!item.Ignore)
                        return _agentSpec->GetArchive()->GetProperty(arcIndex, propID, value);
                }
        }
    } else {
        const CProxyItem proxyItem = GetItem(index); // SET_realIndex_AND_dir control adapter.
        const unsigned realIndex = proxyItem.Index;
        const CProxyDir *dir = &_proxy->Dirs[proxyItem.DirIndex];
        if (realIndex < dir->SubDirs.Size()) {
            const CProxyDir &item = _proxy->Dirs[dir->SubDirs[realIndex]];
            if (!_flatMode && propID == kpidSize)
                prop = UInt64(item.Size);
            else if (!_flatMode && propID == kpidPackSize)
                prop = UInt64(item.PackSize);
            else
                switch (propID) {
                case kpidIsDir:
                    prop = true;
                    break;
                case kpidNumSubDirs:
                    prop = item.NumSubDirs;
                    break;
                case kpidNumSubFiles:
                    prop = item.NumSubFiles;
                    break;
                case kpidName:
                    prop = item.Name;
                    break;
                case kpidCRC:
                    if (item.IsLeaf()) {
                        RINOK(_agentSpec->GetArchive()->GetProperty((unsigned)item.ArcIndex, propID,
                                                                    value))
                    }
                    if (item.CrcIsDefined && value->vt == VT_EMPTY)
                        prop = item.Crc;
                    break;
                default:
                    if (item.IsLeaf()) {
                        if (!_agentSpec->GetArchive())
                            return E_NOTIMPL;
                        return _agentSpec->GetArchive()->GetProperty((unsigned)item.ArcIndex,
                                                                     propID, value);
                    }
                }
        } else {
            const unsigned arcIndex = dir->SubFiles[realIndex - dir->SubDirs.Size()];
            switch (propID) {
            case kpidIsDir:
                prop = false;
                break;
            case kpidName:
                prop = _proxy->Files[arcIndex].Name;
                break;
            default:
                if (!_agentSpec->GetArchive())
                    return E_NOTIMPL;
                return _agentSpec->GetArchive()->GetProperty(arcIndex, propID, value);
            }
        }
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

static UInt64 GetUInt64Prop(IInArchive *archive, UInt32 index, PROPID propID) {
    NWindows::NCOM::CPropVariant prop;
    if (archive->GetProperty(index, propID, &prop) != S_OK)
        throw 111233443;
    UInt64 v = 0;
    if (ConvertPropVariantToUInt64(prop, v))
        return v;
    return 0;
}

static int CompareRawProps(IArchiveGetRawProps *rawProps, unsigned arcIndex1, unsigned arcIndex2,
                           PROPID propID) {
    // if (propID == kpidSha1)
    if (rawProps) {
        const void *p1, *p2;
        UInt32 size1, size2;
        UInt32 propType1, propType2;
        const HRESULT res1 = rawProps->GetRawProp(arcIndex1, propID, &p1, &size1, &propType1);
        const HRESULT res2 = rawProps->GetRawProp(arcIndex2, propID, &p2, &size2, &propType2);
        if (res1 == S_OK && res2 == S_OK) {
            for (UInt32 i = 0; i < size1 && i < size2; i++) {
                const Byte b1 = ((const Byte *)p1)[i];
                const Byte b2 = ((const Byte *)p2)[i];
                if (b1 < b2)
                    return -1;
                if (b1 > b2)
                    return 1;
            }
            if (size1 < size2)
                return -1;
            if (size1 > size2)
                return 1;
            return 0;
        }
    }
    return 0;
}

// returns pointer to extension including '.'

static const wchar_t *GetExtension(const wchar_t *name Z7_lifetimebound) {
    for (const wchar_t *dotPtr = NULL;; name++) {
        wchar_t c = *name;
        if (c == 0)
            return dotPtr ? dotPtr : name;
        if (c == '.')
            dotPtr = name;
    }
}

int CAgentFolder::CompareItems3(UInt32 index1, UInt32 index2, PROPID propID) {
    NWindows::NCOM::CPropVariant prop1, prop2;
    // Name must be first property
    GetProperty(index1, propID, &prop1);
    GetProperty(index2, propID, &prop2);
    if (prop1.vt != prop2.vt)
        return MyCompare(prop1.vt, prop2.vt);
    if (prop1.vt == VT_BSTR)
        return MyStringCompareNoCase(prop1.bstrVal, prop2.bstrVal);
    return prop1.Compare(prop2);
}

int CAgentFolder::ComparePrefixes(const UInt32 index1, const UInt32 index2) {
    // we use CAgentFolder::_temp1 instead of local vaiables for faster execution.
    // it's not allowed to call ComparePrefixes() from different threads simultaneously.
    // UString _temp1, _temp2;
    GetPrefix(index1, _temp1);
    GetPrefix(index2, _temp2);
    return CompareFileNames_ForFolderList(_temp1, _temp2);
}

int CAgentFolder::CompareItems2(UInt32 index1, UInt32 index2, PROPID propID, Int32 propIsRaw) {
    unsigned realIndex1, realIndex2;
    const CProxyDir2 *dir1, *dir2;

    if (_flatMode) {
        const CProxyItem &item1 = _items[index1];
        const CProxyItem &item2 = _items[index2];
        dir1 = &_proxy2->Dirs[item1.DirIndex];
        dir2 = &_proxy2->Dirs[item2.DirIndex];
        realIndex1 = item1.Index;
        realIndex2 = item2.Index;
    } else {
        dir2 = dir1 = &_proxy2->Dirs[_proxyDirIndex];
        realIndex1 = index1;
        realIndex2 = index2;
    }

    UInt32 arcIndex1;
    UInt32 arcIndex2;
    bool isDir1, isDir2;
    arcIndex1 = dir1->Items[realIndex1];
    arcIndex2 = dir2->Items[realIndex2];
    const CProxyFile2 &prox1 = _proxy2->Files[arcIndex1];
    const CProxyFile2 &prox2 = _proxy2->Files[arcIndex2];

    if (propID == kpidName) {
        return CompareFileNames_ForFolderList(prox1.Name, prox2.Name);
    }

    if (propID == kpidPrefix) {
        if (!_flatMode)
            return 0;
#ifdef Z7_AGENT_PROXY2_USE_DIR_PATH_PREFIX
        return CompareFileNames_ForFolderList(_proxy2->Dirs[_items[index1].DirIndex].PathPrefix,
                                              _proxy2->Dirs[_items[index2].DirIndex].PathPrefix);
#else
        return ComparePrefixes(index1, index2);
#endif
    }

    if (propID == kpidExtension) {
        return CompareFileNames_ForFolderList(GetExtension(prox1.Name), GetExtension(prox2.Name));
    }

    isDir1 = prox1.IsDir();
    isDir2 = prox2.IsDir();

    if (propID == kpidIsDir) {
        if (isDir1 == isDir2)
            return 0;
        return isDir1 ? -1 : 1;
    }

    const CProxyDir2 *proxFolder1 = NULL;
    const CProxyDir2 *proxFolder2 = NULL;
    if (isDir1)
        proxFolder1 = &_proxy2->Dirs[prox1.DirIndex];
    if (isDir2)
        proxFolder2 = &_proxy2->Dirs[prox2.DirIndex];

    if (propID == kpidNumSubDirs) {
        UInt32 n1 = 0;
        UInt32 n2 = 0;
        if (isDir1)
            n1 = proxFolder1->NumSubDirs;
        if (isDir2)
            n2 = proxFolder2->NumSubDirs;
        return MyCompare(n1, n2);
    }

    if (propID == kpidNumSubFiles) {
        UInt32 n1 = 0;
        UInt32 n2 = 0;
        if (isDir1)
            n1 = proxFolder1->NumSubFiles;
        if (isDir2)
            n2 = proxFolder2->NumSubFiles;
        return MyCompare(n1, n2);
    }

    if (propID == kpidSize) {
        UInt64 n1, n2;
        if (isDir1)
            n1 = _flatMode ? 0 : proxFolder1->Size;
        else
            n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidSize);
        if (isDir2)
            n2 = _flatMode ? 0 : proxFolder2->Size;
        else
            n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidSize);
        return MyCompare(n1, n2);
    }

    if (propID == kpidPackSize) {
        UInt64 n1, n2;
        if (isDir1)
            n1 = _flatMode ? 0 : proxFolder1->PackSize;
        else
            n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidPackSize);
        if (isDir2)
            n2 = _flatMode ? 0 : proxFolder2->PackSize;
        else
            n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidPackSize);
        return MyCompare(n1, n2);
    }

    if (propID == kpidCRC) {
        UInt64 n1, n2;
        if (!isDir1 || !prox1.Ignore)
            n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidCRC);
        else
            n1 = proxFolder1->Crc;
        if (!isDir2 || !prox2.Ignore)
            n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidCRC);
        else
            n2 = proxFolder2->Crc;
        return MyCompare(n1, n2);
    }

    if (propIsRaw)
        return CompareRawProps(_agentSpec->_archiveLink.GetArchiveGetRawProps(), arcIndex1,
                               arcIndex2, propID);

    return CompareItems3(index1, index2, propID);
}

Z7_COM7F_IMF2(Int32, CAgentFolder::CompareItems(UInt32 index1, UInt32 index2, PROPID propID,
                                                Int32 propIsRaw)) {
    try {
        if (!IsBound() || index1 >= GetNumberOfItems() || index2 >= GetNumberOfItems())
            return 0;
        if (_proxy2)
            return CompareItems2(index1, index2, propID, propIsRaw);

        unsigned realIndex1, realIndex2;
        const CProxyDir *dir1, *dir2;

        if (_flatMode) {
            const CProxyItem &item1 = _items[index1];
            const CProxyItem &item2 = _items[index2];
            dir1 = &_proxy->Dirs[item1.DirIndex];
            dir2 = &_proxy->Dirs[item2.DirIndex];
            realIndex1 = item1.Index;
            realIndex2 = item2.Index;
        } else {
            dir2 = dir1 = &_proxy->Dirs[_proxyDirIndex];
            realIndex1 = index1;
            realIndex2 = index2;
        }

        if (propID == kpidPrefix) {
            if (!_flatMode)
                return 0;
            return ComparePrefixes(index1, index2);
        }

        UInt32 arcIndex1;
        UInt32 arcIndex2;

        const CProxyDir *proxFolder1 = NULL;
        const CProxyDir *proxFolder2 = NULL;

        if (realIndex1 < dir1->SubDirs.Size()) {
            proxFolder1 = &_proxy->Dirs[dir1->SubDirs[realIndex1]];
            arcIndex1 = (unsigned)proxFolder1->ArcIndex;
        } else
            arcIndex1 = dir1->SubFiles[realIndex1 - dir1->SubDirs.Size()];

        if (realIndex2 < dir2->SubDirs.Size()) {
            proxFolder2 = &_proxy->Dirs[dir2->SubDirs[realIndex2]];
            arcIndex2 = (unsigned)proxFolder2->ArcIndex;
        } else
            arcIndex2 = dir2->SubFiles[realIndex2 - dir2->SubDirs.Size()];

        if (propID == kpidName)
            return CompareFileNames_ForFolderList(
                proxFolder1 ? proxFolder1->Name : _proxy->Files[arcIndex1].Name,
                proxFolder2 ? proxFolder2->Name : _proxy->Files[arcIndex2].Name);

        if (propID == kpidExtension)
            return CompareFileNames_ForFolderList(
                GetExtension(proxFolder1 ? proxFolder1->Name : _proxy->Files[arcIndex1].Name),
                GetExtension(proxFolder2 ? proxFolder2->Name : _proxy->Files[arcIndex2].Name));

        if (propID == kpidIsDir) {
            if (proxFolder1)
                return proxFolder2 ? 0 : -1;
            return proxFolder2 ? 1 : 0;
        }

        if (propID == kpidNumSubDirs) {
            UInt32 n1 = 0;
            UInt32 n2 = 0;
            if (proxFolder1)
                n1 = proxFolder1->NumSubDirs;
            if (proxFolder2)
                n2 = proxFolder2->NumSubDirs;
            return MyCompare(n1, n2);
        }

        if (propID == kpidNumSubFiles) {
            UInt32 n1 = 0;
            UInt32 n2 = 0;
            if (proxFolder1)
                n1 = proxFolder1->NumSubFiles;
            if (proxFolder2)
                n2 = proxFolder2->NumSubFiles;
            return MyCompare(n1, n2);
        }

        if (propID == kpidSize) {
            UInt64 n1, n2;
            if (proxFolder1)
                n1 = _flatMode ? 0 : proxFolder1->Size;
            else
                n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidSize);
            if (proxFolder2)
                n2 = _flatMode ? 0 : proxFolder2->Size;
            else
                n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidSize);
            return MyCompare(n1, n2);
        }

        if (propID == kpidPackSize) {
            UInt64 n1, n2;
            if (proxFolder1)
                n1 = _flatMode ? 0 : proxFolder1->PackSize;
            else
                n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidPackSize);
            if (proxFolder2)
                n2 = _flatMode ? 0 : proxFolder2->PackSize;
            else
                n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidPackSize);
            return MyCompare(n1, n2);
        }

        if (propID == kpidCRC) {
            UInt64 n1, n2;
            if (proxFolder1 && !proxFolder1->IsLeaf())
                n1 = proxFolder1->Crc;
            else
                n1 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex1, kpidCRC);
            if (proxFolder2 && !proxFolder2->IsLeaf())
                n2 = proxFolder2->Crc;
            else
                n2 = GetUInt64Prop(_agentSpec->GetArchive(), arcIndex2, kpidCRC);
            return MyCompare(n1, n2);
        }

        if (propIsRaw) {
            bool isVirt1 = (proxFolder1 && !proxFolder1->IsLeaf());
            bool isVirt2 = (proxFolder2 && !proxFolder2->IsLeaf());
            if (isVirt1)
                return isVirt2 ? 0 : -1;
            if (isVirt2)
                return 1;
            return CompareRawProps(_agentSpec->_archiveLink.GetArchiveGetRawProps(), arcIndex1,
                                   arcIndex2, propID);
        }

        return CompareItems3(index1, index2, propID);

    } catch (...) {
        return 0;
    }
}

HRESULT CAgentFolder::BindToFolder_Internal(unsigned proxyDirIndex, IFolderFolder **resultFolder) {
    CAgentFolder *folderSpec = new CAgentFolder;
    CMyComPtr<IFolderFolder> agentFolder = folderSpec;
    folderSpec->Init(_proxy, _proxy2, proxyDirIndex, _agentSpec);
    *resultFolder = agentFolder.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::BindToFolder(UInt32 index, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = nullptr;
    if (!IsBound())
        return E_FAIL;
    if (_flatMode && index >= _items.Size())
        return E_INVALIDARG;
    const CProxyItem item = GetItem(index);
    const unsigned realIndex = item.Index;
    if (_proxy2) {
        const CProxyDir2 *dir = &_proxy2->Dirs[item.DirIndex];
        const unsigned arcIndex = dir->Items[realIndex];
        const CProxyFile2 &file = _proxy2->Files[arcIndex];
        if (!file.IsDir())
            return E_INVALIDARG;
        return BindToFolder_Internal((unsigned)file.DirIndex, resultFolder);
    }
    const CProxyDir *dir = &_proxy->Dirs[item.DirIndex];
    if (realIndex >= (UInt32)dir->SubDirs.Size())
        return E_INVALIDARG;
    return BindToFolder_Internal(dir->SubDirs[realIndex], resultFolder);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::BindToFolder(const wchar_t *name, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    RINOK(LoadDirectoryItems(false))
    *resultFolder = nullptr;
    if (!IsBound())
        return E_FAIL;
    if (_proxy2) {
        const int index = _proxy2->FindItem(_proxyDirIndex, name, true);
        if (index == -1)
            return E_INVALIDARG;
        return BindToFolder_Internal(
            (unsigned)_proxy2->Files[_proxy2->Dirs[_proxyDirIndex].Items[index]].DirIndex,
            resultFolder);
    }
    const int index = _proxy->FindSubDir(_proxyDirIndex, name);
    if (index == -1)
        return E_INVALIDARG;
    return BindToFolder_Internal((unsigned)index, resultFolder);
    COM_TRY_END
}

HRESULT CAgentFolder::BindToAltStreams_Internal(unsigned proxyDirIndex,
                                                IFolderFolder **resultFolder) {
    *resultFolder = nullptr;
    if (!_proxy2)
        return S_OK;
    CAgentFolder *folderSpec = new CAgentFolder;
    CMyComPtr<IFolderFolder> agentFolder = folderSpec;
    folderSpec->Init(_proxy, _proxy2, proxyDirIndex, _agentSpec);
    *resultFolder = agentFolder.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::BindToAltStreams(UInt32 index, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = nullptr;
    if (!IsBound())
        return E_FAIL;
    if (!_proxy2)
        return S_OK;
    if (_proxy2->IsAltDir(_proxyDirIndex))
        return S_OK;
    {
        if (index == (UInt32)(Int32)-1) {
            unsigned altDirIndex;
            if (_proxyDirIndex == k_Proxy2_RootDirIndex)
                altDirIndex = k_Proxy2_AltRootDirIndex;
            else {
                const unsigned arcIndex = (unsigned)_proxy2->Dirs[_proxyDirIndex].ArcIndex;
                const CProxyFile2 &item = _proxy2->Files[arcIndex];
                if (item.AltDirIndex == -1)
                    return S_OK;
                altDirIndex = (unsigned)item.AltDirIndex;
            }
            CAgentFolder *folderSpec = new CAgentFolder;
            CMyComPtr<IFolderFolder> agentFolder = folderSpec;
            folderSpec->Init(_proxy, _proxy2, altDirIndex, _agentSpec);
            *resultFolder = agentFolder.Detach();
            return S_OK;
        }
        if (index >= GetNumberOfItems())
            return E_INVALIDARG;
        const CProxyItem proxyItem = GetItem(index);
        const CProxyDir2 *dir = &_proxy2->Dirs[proxyItem.DirIndex];
        const unsigned realIndex = proxyItem.Index;
        const unsigned arcIndex = dir->Items[realIndex];
        const CProxyFile2 &item = _proxy2->Files[arcIndex];
        if (item.AltDirIndex == -1)
            return S_OK;
        return BindToAltStreams_Internal((unsigned)item.AltDirIndex, resultFolder);
    }
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::BindToAltStreams(const wchar_t *name, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = nullptr;
    if (!IsBound())
        return E_FAIL;
    if (!_proxy2)
        return S_OK;
    if (_proxy2->IsAltDir(_proxyDirIndex))
        return S_OK;
    if (name[0] == 0)
        return BindToAltStreams((UInt32)(Int32)-1, resultFolder);
    {
        const CUIntVector &subFiles = _proxy2->Dirs[_proxyDirIndex].Items;
        FOR_VECTOR(i, subFiles) {
            const CProxyFile2 &file = _proxy2->Files[subFiles[i]];
            if (file.AltDirIndex != -1)
                if (CompareFileNames(file.Name, name) == 0)
                    return BindToAltStreams_Internal((unsigned)file.AltDirIndex, resultFolder);
        }
        return E_INVALIDARG;
    }
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::AreAltStreamsSupported(UInt32 index, Int32 *isSupported)) {
    *isSupported = BoolToInt(false);
    if (!IsBound())
        return E_FAIL;
    if (!_proxy2)
        return S_OK;
    if (_proxy2->IsAltDir(_proxyDirIndex))
        return S_OK;
    unsigned arcIndex;
    if (index == (UInt32)(Int32)-1) {
        if (_proxyDirIndex == k_Proxy2_RootDirIndex) {
            *isSupported = BoolToInt(true);
            return S_OK;
        }
        arcIndex = (unsigned)_proxy2->Dirs[_proxyDirIndex].ArcIndex;
    } else {
        if (index >= GetNumberOfItems())
            return E_INVALIDARG;
        const CProxyItem proxyItem = GetItem(index);
        const CProxyDir2 *dir = &_proxy2->Dirs[proxyItem.DirIndex];
        const unsigned realIndex = proxyItem.Index;
        arcIndex = dir->Items[realIndex];
    }
    if (_proxy2->Files[arcIndex].AltDirIndex != -1)
        *isSupported = BoolToInt(true);
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::BindToParentFolder(IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = nullptr;
    if (!IsBound())
        return E_FAIL;
    unsigned proxyDirIndex;
    if (_proxy2) {
        if (_proxyDirIndex == k_Proxy2_RootDirIndex)
            return S_OK;
        if (_proxyDirIndex == k_Proxy2_AltRootDirIndex)
            proxyDirIndex = k_Proxy2_RootDirIndex;
        else {
            const CProxyDir2 &fold = _proxy2->Dirs[_proxyDirIndex];
            const CProxyFile2 &file = _proxy2->Files[(unsigned)fold.ArcIndex];
            const int parentIndex = file.Parent;
            if (parentIndex == -1)
                proxyDirIndex = k_Proxy2_RootDirIndex;
            else
                proxyDirIndex = (unsigned)_proxy2->Files[(unsigned)parentIndex].DirIndex;
        }
    } else {
        const int parent = _proxy->Dirs[_proxyDirIndex].ParentDir;
        if (parent == -1)
            return S_OK;
        proxyDirIndex = (unsigned)parent;
    }
    CAgentFolder *folderSpec = new CAgentFolder;
    CMyComPtr<IFolderFolder> agentFolder = folderSpec;
    folderSpec->Init(_proxy, _proxy2, proxyDirIndex, _agentSpec);
    *resultFolder = agentFolder.Detach();
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::GetStream(UInt32 index, ISequentialInStream **stream)) {
    return GetStreamWithRequest(index, stream, nullptr);
}

Z7_COM7F_IMF(CAgentFolder::GetStreamWithRequest(UInt32 index, ISequentialInStream **stream,
                                                IProgress *request)) {
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    if (!IsBound() || index >= GetNumberOfItems())
        return E_INVALIDARG;
    Z7_DECL_CMyComPtr_QI_FROM(IInArchiveGetStream, getStream, _agentSpec->GetArchive());
    if (!getStream)
        return S_OK;

    UInt32 arcIndex;
    if (_proxy2) {
        SET_realIndex_AND_dir_2;
        arcIndex = dir->Items[realIndex];
    } else {
        SET_realIndex_AND_dir;
        if (realIndex < dir->SubDirs.Size()) {
            const CProxyDir &item = _proxy->Dirs[dir->SubDirs[realIndex]];
            if (!item.IsLeaf())
                return S_OK;
            arcIndex = (unsigned)item.ArcIndex;
        } else
            arcIndex = dir->SubFiles[realIndex - dir->SubDirs.Size()];
    }
    CMyComPtr<IArchiveGetStreamWithRequest> requested;
    getStream.QueryInterface(IID_IArchiveGetStreamWithRequest, &requested);
    return requested ? requested->GetStreamWithRequest(arcIndex, stream, request)
                     : getStream->GetStream(arcIndex, stream);
}

Z7_COM7F_IMF(CAgentFolder::GetFolderProperty(PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    if (!IsBound()) {
        if (propID == kpidPath && _closedArchivePath) {
            NWindows::NCOM::CPropVariant path(_closedArchivePath->Prefix);
            path.Detach(value);
            return S_OK;
        }
        // The source type is independent of the directory proxy. Qt still
        // needs it to render an archive whose native reopen has failed.
        if (propID == kpidType && _agentSpec) {
            NWindows::NCOM::CPropVariant type(UString("7-Zip.") + _archiveType);
            type.Detach(value);
            return S_OK;
        }
        return E_FAIL;
    }
    NWindows::NCOM::CPropVariant prop;
    if ((propID == kpidSize || propID == kpidPackSize || propID == kpidNumSubDirs ||
         propID == kpidNumSubFiles) &&
        DirectoryItemsNeedLoad(true)) {
        prop.Detach(value);
        return S_OK;
    }
    if (propID == kpidReadOnly) {
        if ((_agentSpec->_proxy && _agentSpec->_proxy->Are_Changed_LongPaths) ||
            _agentSpec->Is_Attrib_ReadOnly())
            prop = true;
        else
            prop = _agentSpec->IsThere_ReadOnlyArc();
    } else if (propID == kpidIsHash) {
        prop = _agentSpec->_isHashHandler;
    } else if (_proxy2) {
        const CProxyDir2 &dir = _proxy2->Dirs[_proxyDirIndex];
        if (propID == kpidName) {
            if (dir.ArcIndex != -1)
                prop = _proxy2->Files[(unsigned)dir.ArcIndex].Name;
        } else if (propID == kpidPath)
            prop = _proxy2->GetDirPath_as_Prefix(_proxyDirIndex, true);
        else
            switch (propID) {
            case kpidSize:
                prop = dir.Size;
                break;
            case kpidPackSize:
                prop = dir.PackSize;
                break;
            case kpidNumSubDirs:
                prop = dir.NumSubDirs;
                break;
            case kpidNumSubFiles:
                prop = dir.NumSubFiles;
                break;
            case kpidType:
                prop = UString("7-Zip.") + _archiveType;
                break;
            case kpidCRC:
                if (dir.CrcIsDefined)
                    prop = dir.Crc;
                break;
            }
    } else {
        const CProxyDir &dir = _proxy->Dirs[_proxyDirIndex];
        switch (propID) {
        case kpidSize:
            prop = UInt64(dir.Size);
            break;
        case kpidPackSize:
            prop = UInt64(dir.PackSize);
            break;
        case kpidNumSubDirs:
            prop = dir.NumSubDirs;
            break;
        case kpidNumSubFiles:
            prop = dir.NumSubFiles;
            break;
        case kpidName:
            prop = dir.Name;
            break;
        case kpidPath:
            prop = _proxy->GetDirPath_as_Prefix(_proxyDirIndex);
            break;
        case kpidType:
            prop = UString("7-Zip.") + _archiveType;
            break;
        case kpidCRC:
            if (dir.CrcIsDefined)
                prop = dir.Crc;
            break;
        }
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

static const PROPID kProps[] = {kpidNumSubDirs, kpidNumSubFiles, kpidPrefix};

static const PROPID kFolderProps[] = {kpidSize, kpidPackSize, kpidNumSubDirs, kpidNumSubFiles,
                                      kpidCRC};

Z7_COM7F_IMF(CAgentFolder::GetNumberOfFolderProperties(UInt32 *numProps)) {
    *numProps = Z7_ARRAY_SIZE(kFolderProps);
    return S_OK;
}

IMP_IFolderFolder_GetProp(CAgentFolder::GetFolderPropertyInfo, kFolderProps)

    Z7_COM7F_IMF(CAgentFolder::GetFolderArcProps(IFolderArcProps **object)) {
    CMyComPtr<IFolderArcProps> temp = _agentSpec;
    *object = temp.Detach();
    return S_OK;
}

UString CAgent::GetTypeOfArc(const CArc &arc) const {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    if (context.Result != S_OK)
        return UString();
    const CCodecs *codecs = context.Codecs;
    if (!codecs)
        return context.Format.isEmpty() ? ArchiveType
                                        : UString(context.Format.toStdWString().c_str());
    // Operation metadata is a file-only facade of the innermost native arc.
    // Its codec index is not a native slot; resolve that facade by identity.
    const CArc &native = &arc == &_archiveLink.Arcs.Back() ? *context.Link->GetArc() : arc;
    if (native.FormatIndex < 0)
        return UString("Parser");
    return codecs->GetFormatNamePtr(native.FormatIndex);
}

Z7_COM7F_IMF(CAgent::GetArcNumLevels(UInt32 *numLevels)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    *numLevels = context.Link->Arcs.Size();
    return S_OK;
}

Z7_COM7F_IMF(CAgent::GetArcProp(UInt32 level, PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    const CArchiveLink &link = *context.Link;
    const CCodecs *codecs = context.Codecs;
    if (level > (UInt32)link.Arcs.Size())
        return E_INVALIDARG;
    if (level == (UInt32)link.Arcs.Size()) {
        switch (propID) {
        case kpidError:
            if (!link.NonOpen_ErrorInfo.ErrorMessage.IsEmpty())
                prop = link.NonOpen_ErrorInfo.ErrorMessage;
            break;
        case kpidPath:
            if (!link.NonOpen_ArcPath.IsEmpty())
                prop = link.NonOpen_ArcPath;
            break;
        case kpidErrorType:
            if (codecs && link.NonOpen_ErrorInfo.ErrorFormatIndex >= 0)
                prop = codecs->Formats[link.NonOpen_ErrorInfo.ErrorFormatIndex].Name;
            break;
        case kpidErrorFlags: {
            UInt32 flags = link.NonOpen_ErrorInfo.GetErrorFlags();
            if (flags != 0)
                prop = flags;
            break;
        }
        case kpidWarningFlags: {
            UInt32 flags = link.NonOpen_ErrorInfo.GetWarningFlags();
            if (flags != 0)
                prop = flags;
            break;
        }
        }
    } else {
        const CArc &arc = link.Arcs[level];
        switch (propID) {
        case kpidType:
            prop = GetTypeOfArc(arc);
            break;
        case kpidPath:
            prop = arc.Path;
            break;
        case kpidErrorType:
            if (codecs && arc.ErrorInfo.ErrorFormatIndex >= 0)
                prop = codecs->Formats[arc.ErrorInfo.ErrorFormatIndex].Name;
            break;
        case kpidErrorFlags: {
            const UInt32 flags = arc.ErrorInfo.GetErrorFlags();
            if (flags != 0)
                prop = flags;
            break;
        }
        case kpidWarningFlags: {
            const UInt32 flags = arc.ErrorInfo.GetWarningFlags();
            if (flags != 0)
                prop = flags;
            break;
        }
        case kpidOffset: {
            const Int64 v = arc.GetGlobalOffset();
            if (v != 0)
                prop.Set_Int64(v);
            break;
        }
        case kpidTailSize: {
            if (arc.ErrorInfo.TailSize != 0)
                prop = arc.ErrorInfo.TailSize;
            break;
        }
        default:
            return arc.Archive->GetArchiveProperty(propID, value);
        }
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::GetArcNumProps(UInt32 level, UInt32 *numProps)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    return (*context.Link).Arcs[level].Archive->GetNumberOfArchiveProperties(numProps);
}

Z7_COM7F_IMF(CAgent::GetArcPropInfo(UInt32 level, UInt32 index, BSTR *name, PROPID *propID,
                                    VARTYPE *varType)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    return (*context.Link)
        .Arcs[level]
        .Archive->GetArchivePropertyInfo(index, name, propID, varType);
}

Z7_COM7F_IMF(CAgent::GetArcProp2(UInt32 level, PROPID propID, PROPVARIANT *value)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    const CArchiveLink &link = *context.Link;
    return link.Arcs[level - 1].Archive->GetProperty(link.Arcs[level].SubfileIndex, propID, value);
}

Z7_COM7F_IMF(CAgent::GetArcNumProps2(UInt32 level, UInt32 *numProps)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    return (*context.Link).Arcs[level - 1].Archive->GetNumberOfProperties(numProps);
}

Z7_COM7F_IMF(CAgent::GetArcPropInfo2(UInt32 level, UInt32 index, BSTR *name, PROPID *propID,
                                     VARTYPE *varType)) {
    ArchivePropertyContext context(GetArchive(), _archiveLink, _openCodecs);
    RINOK(context.Result)
    return (*context.Link).Arcs[level - 1].Archive->GetPropertyInfo(index, name, propID, varType);
}

Z7_COM7F_IMF(CAgentFolder::GetNumberOfProperties(UInt32 *numProps)) {
    COM_TRY_BEGIN
    if (!IsBound()) {
        *numProps = 0;
        return E_FAIL;
    }
    RINOK(_agentSpec->GetArchive()->GetNumberOfProperties(numProps))
    *numProps += Z7_ARRAY_SIZE(kProps);
    if (!_flatMode)
        (*numProps)--;
    if (!_agentSpec->ThereIsPathProp)
        (*numProps)++;
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *propID,
                                           VARTYPE *varType)) {
    COM_TRY_BEGIN
    if (!IsBound())
        return E_FAIL;
    UInt32 numProps = 0;
    _agentSpec->GetArchive()->GetNumberOfProperties(&numProps);
    // Source in-process callers supply valid indices. The typed metadata adapter
    // validates indices and initializes an unwritten count, retaining the
    // source policy of consuming a written count regardless of HRESULT.
    if (!_agentSpec->ThereIsPathProp) {
        if (index == 0) {
            *propID = kpidName;
            *varType = VT_BSTR;
            *name = NULL;
            return S_OK;
        }
        index--;
    }
    if (index < numProps) {
        RINOK(_agentSpec->GetArchive()->GetPropertyInfo(index, name, propID, varType))
        if (*propID == kpidPath)
            *propID = kpidName;
    } else {
        index -= numProps;
        if (index >= Z7_ARRAY_SIZE(kProps) - (!_flatMode ? 1 : 0))
            return E_INVALIDARG;
        *propID = kProps[index];
        *varType = k7z_PROPID_To_VARTYPE[(unsigned)*propID];
        *name = NULL;
    }
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::SetFlatMode(Int32 flatMode)) {
    _flatMode = IntToBool(flatMode);
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::GetNumRawProps(UInt32 *numProps)) {
    CMyComPtr<IInArchive> archive = _agentSpec ? _agentSpec->GetArchive() : nullptr;
    CMyComPtr<IArchiveGetRawProps> rawProps;
    if (archive)
        archive.QueryInterface(IID_IArchiveGetRawProps, &rawProps);
    if (rawProps)
        return rawProps->GetNumRawProps(numProps);
    *numProps = 0;
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::GetParent(UInt32 /* index */, UInt32 * /* parent */,
                                     UInt32 * /* parentType */)) {
    return E_FAIL;
}

Z7_COM7F_IMF(CAgentFolder::GetRawPropInfo(UInt32 index, BSTR *name, PROPID *propID)) {
    CMyComPtr<IInArchive> archive = _agentSpec ? _agentSpec->GetArchive() : nullptr;
    CMyComPtr<IArchiveGetRawProps> rawProps;
    if (archive)
        archive.QueryInterface(IID_IArchiveGetRawProps, &rawProps);
    return rawProps ? rawProps->GetRawPropInfo(index, name, propID) : E_FAIL;
}

Z7_COM7F_IMF(CAgentFolder::GetRawProp(UInt32 index, PROPID propID, const void **data,
                                      UInt32 *dataSize, UInt32 *propType)) {
    *data = nullptr;
    *dataSize = 0;
    *propType = 0;
    if (!IsBound() || index >= GetNumberOfItems())
        return E_INVALIDARG;
    CMyComPtr<IInArchive> archive = _agentSpec ? _agentSpec->GetArchive() : nullptr;
    CMyComPtr<IArchiveGetRawProps> rawProps;
    if (archive)
        archive.QueryInterface(IID_IArchiveGetRawProps, &rawProps);
    if (rawProps) {
        const int arcIndex = GetRealIndex(index);
        if (arcIndex >= 0)
            return rawProps->GetRawProp(unsigned(arcIndex), propID, data, dataSize, propType);
    }
    return S_OK;
}

int CAgentFolder::GetRealIndex(unsigned index) const {
    if (!IsBound() || index >= GetNumberOfItems())
        return -1;
    if (!_flatMode) {
        if (_proxy2)
            return (int)_proxy2->GetRealIndex(_proxyDirIndex, index);
        else
            return _proxy->GetRealIndex(_proxyDirIndex, index);
    }
    {
        const CProxyItem &item = _items[index];
        if (_proxy2) {
            const CProxyDir2 *dir = &_proxy2->Dirs[item.DirIndex];
            return (int)dir->Items[item.Index];
        }
        {
            const CProxyDir *dir = &_proxy->Dirs[item.DirIndex];
            const unsigned realIndex = item.Index;
            if (realIndex < dir->SubDirs.Size()) {
                const CProxyDir &f = _proxy->Dirs[dir->SubDirs[realIndex]];
                if (!f.IsLeaf())
                    return -1;
                return f.ArcIndex;
            }
            return (int)dir->SubFiles[realIndex - dir->SubDirs.Size()];
        }
    }
}

void CAgentFolder::GetRealIndices(const UInt32 *indices, const UInt32 numItems,
                                  const bool includeAltStreams,
                                  const bool includeFolderSubItemsInFlatMode,
                                  CUIntVector &realIndices) const {
    if (!_flatMode) {
        if (_proxy2)
            _proxy2->GetRealIndices_Unsorted(_proxyDirIndex, indices, numItems, includeAltStreams,
                                             realIndices);
        else
            _proxy->GetRealIndices_Unsorted(_proxyDirIndex, indices, numItems, realIndices);
    } else {
        realIndices.Clear();
        for (UInt32 i = 0; i < numItems; i++) {
            const CProxyItem &item = _items[indices[i]];
            if (_proxy2) {
                const CProxyDir2 *dir = &_proxy2->Dirs[item.DirIndex];
                _proxy2->AddRealIndices_of_ArcItem(dir->Items[item.Index], includeAltStreams,
                                                   includeFolderSubItemsInFlatMode, realIndices);
                continue;
            }
            UInt32 arcIndex;
            {
                const CProxyDir *dir = &_proxy->Dirs[item.DirIndex];
                const unsigned realIndex = item.Index;
                if (realIndex < dir->SubDirs.Size()) {
                    if (includeFolderSubItemsInFlatMode) {
                        _proxy->AddRealIndices(dir->SubDirs[realIndex], realIndices);
                        continue;
                    }
                    const CProxyDir &f = _proxy->Dirs[dir->SubDirs[realIndex]];
                    if (!f.IsLeaf())
                        continue;
                    arcIndex = (unsigned)f.ArcIndex;
                } else
                    arcIndex = dir->SubFiles[realIndex - dir->SubDirs.Size()];
            }
            realIndices.Add(arcIndex);
        }
    }
    HeapSort(realIndices.NonConstData(), realIndices.Size());
}

CAgent::CAgent(IQtAgentOpenServices *openServices)
    : _proxy(NULL), _proxy2(NULL), _updatePathPrefix_is_AltFolder(false), _isDeviceFile(false),
      _agentFolder(NULL), _openServices(openServices) {
    if (_openServices)
        _engineResource = _openServices->Resource();
}

CAgent::~CAgent() {
    if (_proxy)
        delete _proxy;
    if (_proxy2)
        delete _proxy2;
}

bool CAgent::CanUpdate() const {
    if (_proxy && _proxy->Are_Changed_LongPaths)
        return false;
    // FAR plugin uses empty agent to create new archive !!!
    if (_archiveLink.Arcs.Size() == 0)
        return true;
    if (auto *resource = dynamic_cast<QtArchiveFileResource *>(_engineResource.Interface()))
        if (!resource->HasUpdateService() || !ArchiveUpdateEnabled(GetArchive()))
            return false;
    if (_isDeviceFile)
        return false;
    if (_archiveLink.Arcs.Size() != 1)
        return false;
    if (_archiveLink.Arcs[0].ErrorInfo.ThereIsTail)
        return false;
    return true;
}

bool CAgent::IsThere_ReadOnlyArc() const {
    FOR_VECTOR(i, _archiveLink.Arcs) {
        const CArc &arc = _archiveLink.Arcs[i];
        // Original FormatIndex/UpdateEnabled lookup is supplied by the typed
        // file-only engine capability; the codec catalog is mandatory P2-01.
        if (arc.IsReadOnly || !ArchiveUpdateEnabled(arc.Archive))
            return true;
    }
    return false;
}

Z7_COM7F_IMF(CAgent::Open(IInStream *inStream, const wchar_t *filePath, const wchar_t *arcFormat,
                          BSTR *archiveType, IArchiveOpenCallback *openArchiveCallback)) {
    COM_TRY_BEGIN
    const auto detachOpenServices = qScopeGuard([&] { _openServices = nullptr; });
    _archiveFilePath = filePath;
    _hashBaseFolderPrefix.Empty();
    _attrib = 0;
    _isDeviceFile = dynamic_cast<DirectDeviceInput *>(inStream) != nullptr;
    _isHashHandler = false;
    NWindows::NFile::NFind::CFileInfo fi;
    if (!inStream) {
        if (!fi.Find(us2fs(_archiveFilePath)))
            return GetLastError_noZero_HRESULT();
        if (fi.IsDir() && !IsSparseBundleName(_archiveFilePath))
            return E_FAIL;
        _attrib = fi.GetWinAttrib();
#ifdef _WIN32
        _isDeviceFile = fi.IsDevice;
#endif
        FString dirPrefix, fileName;
        if (NWindows::NFile::NDir::GetFullPathAndSplit(us2fs(_archiveFilePath), dirPrefix,
                                                       fileName)) {
            NWindows::NFile::NName::NormalizeDirPathPrefix(dirPrefix);
            _hashBaseFolderPrefix = dirPrefix;
        }
    }
    CArcInfoEx archiverInfo0, archiverInfo1;

    const CCodecs *codecs = NULL;
    if (!_openServices)
        return E_FAIL;
    RINOK(_openServices->LoadCodecs(codecs))
    _openCodecs = codecs;

    CObjectVector<COpenType> types;
    if (codecs && !ParseOpenTypes(*const_cast<CCodecs *>(codecs), arcFormat, types))
        return S_FALSE;

    /*
    CObjectVector<COptionalOpenProperties> optProps;
    if (Read_ShowDeleted())
    {
      COptionalOpenProperties &optPair = optProps.AddNew();
      optPair.FormatName = "ntfs";
      // optPair.Props.AddNew().Name = "LS";
      optPair.Props.AddNew().Name = "LD";
    }
    */

    COpenOptions options;
    options.props = NULL;
    options.PreferDirectoryRead = true;
    options.codecs = const_cast<CCodecs *>(codecs);
    options.QtOpenService = _openServices;
    options.types = &types;
    CIntVector exl;
    options.excludedFormats = &exl;
    options.stdInMode = false;
    options.stream = inStream;
    options.filePath = _archiveFilePath;
    options.callback = openArchiveCallback;

    HRESULT res = _archiveLink.Open(options);

    if (!_archiveLink.Arcs.IsEmpty()) {
        CArc &arc = _archiveLink.Arcs.Back();
        if (!inStream) {
            arc.MTime.Set_From_FiTime(fi.MTime);
            arc.MTime.Def = !_isDeviceFile;
        }

        ArchiveType = GetTypeOfArc(arc);
        if (archiveType) {
            RINOK(StringToBstr(ArchiveType, archiveType))
        }

        if (arc.IsHashHandler(options))
            _isHashHandler = true;
    }

    return res;

    COM_TRY_END
}

HRESULT CAgent::Init(IFolderFolder *archiveProps, const UString &archiveType,
                     IUnknown *engineResource) {
    COM_TRY_BEGIN
    if (!archiveProps)
        return E_INVALIDARG;
    // Initial binding creates one source owner. Native ReOpen refreshes that
    // owner's metadata; CommonUpdateOperation restores its bound folder paths.
    if (!_archiveLink.Arcs.IsEmpty())
        return E_FAIL;
    RINOK(CreateArchiveLinkTransport(_archiveLink, archiveProps, engineResource))
    CArc &arc = _archiveLink.Arcs.Back();
    if (auto *resource = dynamic_cast<QtArchiveFileResource *>(engineResource)) {
        _isHashHandler = resource->HasNativeHashReader();
    }
    ArchiveType = archiveType;
    _archiveFilePath = arc.Path;
    _hashBaseFolderPrefix.Empty();
    FString dirPrefix, fileName;
    if (NWindows::NFile::NDir::GetFullPathAndSplit(us2fs(_archiveFilePath), dirPrefix, fileName)) {
        NWindows::NFile::NName::NormalizeDirPathPrefix(dirPrefix);
        _hashBaseFolderPrefix = dirPrefix;
    }
    NWindows::NFile::NFind::CFileInfo fi;
    _attrib = fi.Find(us2fs(_archiveFilePath)) ? fi.GetWinAttrib() : (DWORD)-1;
    _engineResource = engineResource;
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::ReOpen(IArchiveOpenCallback *openArchiveCallback)) {
    COM_TRY_BEGIN
    // Published folders borrow these proxies. Retire their binding before
    // deletion, including when reopening fails before a replacement exists.
    ++_proxyGeneration;
    if (_proxy2) {
        delete _proxy2;
        _proxy2 = NULL;
    }
    if (_proxy) {
        delete _proxy;
        _proxy = NULL;
    }

    CObjectVector<COpenType> incl;
    CIntVector exl;

    COpenOptions options;
    options.props = NULL;
    options.codecs = NULL; // Codec engine is supplied by the file-only transport.
    options.types = &incl;
    options.excludedFormats = &exl;
    options.stdInMode = false;
    options.filePath = _archiveFilePath;
    options.callback = openArchiveCallback;

    auto *resource = dynamic_cast<QtArchiveFileResource *>(GetEngineResource());
    if (resource && resource->HasStreamInput()) {
        // A stream refresh reuses its retained input; a virtual display
        // path must never be reopened through the host filesystem.
        if (_archiveLink.Arcs.Size() != 1)
            return E_FAIL;
        _archiveLink.IsOpen = false;
        RINOK(_archiveLink.Arcs[0].Archive->Open(nullptr, nullptr, openArchiveCallback))
        _archiveLink.IsOpen = true;
    } else {
        RINOK(_archiveLink.ReOpen(options))
    }
    // The Qt transport opens a fresh native handler after publication. Its
    // tree/stream capabilities can change when the first ADS is added.
    if (resource) {
        RINOK(resource->ConfigureArc(_archiveLink.Arcs.Back()))
        _isHashHandler = resource->HasNativeHashReader();
    }
    return ReadItems();
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::Close()) {
    COM_TRY_BEGIN
    // Close can release handler-owned names before ReOpen replaces the proxies.
    // Retire every published folder, including if closing a later volume fails.
    ++_proxyGeneration;
    return _archiveLink.Close();
    COM_TRY_END
}

IUnknown *CAgentFolder::GetEngineResource() const {
    return _agentSpec ? _agentSpec->GetEngineResource() : nullptr;
}

Z7_COM7F_IMF(CAgentFolder::GetContainerDepth(UInt64 *depth)) {
    if (!depth)
        return E_INVALIDARG;
    *depth = 0;
    auto *resource = dynamic_cast<QtArchiveFileResource *>(GetEngineResource());
    if (!resource)
        return S_FALSE;
    *depth = resource->ContainerDepth();
    return S_OK;
}

Z7_COM7F_IMF(CAgentFolder::SetParentContainerDepth(UInt64 depth)) {
    auto *resource = dynamic_cast<QtArchiveFileResource *>(GetEngineResource());
    if (!resource)
        return S_FALSE;
    resource->SetParentContainerDepth(depth);
    return S_OK;
}

HRESULT CAgentFolder::CloseArchive() {
    COM_TRY_BEGIN
    if (!_closedArchivePath) {
        if (!IsBound())
            return E_FAIL;
        CClosedArchivePath path;
        // As in CommonUpdateOperation, save FolderItem before closing the
        // handler: Windows proxy names can borrow its raw-property memory.
        GetPathParts(path.Parts, path.IsAltStreamFolder);
        NWindows::NCOM::CPropVariant prefix;
        RINOK(GetFolderProperty(kpidPath, &prefix))
        if (prefix.vt == VT_BSTR)
            path.Prefix = prefix.bstrVal;
        _closedArchivePath = std::move(path);
    }
    return _agentSpec->Close();
    COM_TRY_END
}

HRESULT CAgentFolder::ReOpen(IArchiveOpenCallback *callback, IFolderFolder **folder) {
    COM_TRY_BEGIN
    if (!folder)
        return E_INVALIDARG;
    *folder = nullptr;
    if (!_closedArchivePath && !IsBound())
        return E_FAIL;
    UStringVector pathParts;
    bool isAltStreamFolder = false;
    GetPathParts(pathParts, isAltStreamFolder);
    RINOK(_agentSpec->ReOpen(callback))
    CMyComPtr<IFolderFolder> archiveFolder;
    RINOK(_agentSpec->BindToRootFolder(&archiveFolder))
    // Restore FolderItem from ArchiveFolderOut::CommonUpdateOperation.
    // The reopened owner is returned separately so callers discard stale proxies.
    auto *restored = static_cast<CAgentFolder *>(archiveFolder.Interface());
    restored->RestorePath(pathParts, isAltStreamFolder);
    *folder = archiveFolder.Detach();
    return S_OK;
    COM_TRY_END
}

HRESULT CAgent::ReadItems() {
    if (_proxy || _proxy2)
        return S_OK;
    const CArc &arc = GetArc();
    const bool useProxy2 = (arc.GetRawProps && arc.IsTree);
    if (useProxy2)
        _proxy2 = new CProxyArc2();
    else
        _proxy = new CProxyArc();
    bool loaded = false;
    // Joined workers may retry binding after a descriptor/load error or Cancel.
    // Do not let a partially loaded proxy satisfy the source early return.
    const auto discardPartialProxy = qScopeGuard([&] {
        if (!loaded) {
            delete _proxy;
            _proxy = NULL;
            delete _proxy2;
            _proxy2 = NULL;
        }
    });
    size_t ramSize;
    if (NWindows::NSystem::GetRamSize(ramSize)) {
        ramSize = ramSize / 4 * 3;
        if (_proxy2)
            _proxy2->MemUsage_Limit = ramSize;
        else
            _proxy->MemUsage_Limit = ramSize;
    }
    {
        ThereIsPathProp = false;
        UInt32 numProps = 0;
        arc.Archive->GetNumberOfProperties(&numProps);
        for (UInt32 i = 0; i < numProps; i++) {
            CMyComBSTR name;
            PROPID propID;
            VARTYPE varType;
            RINOK(arc.Archive->GetPropertyInfo(i, &name, &propID, &varType))
            if (propID == kpidPath)
                ThereIsPathProp = true;
        }
    }
    const HRESULT result = _proxy2 ? _proxy2->Load(GetArc(), _progress_for_Open)
                                   : _proxy->Load(GetArc(), _progress_ArchiveOpenCallback_for_Open);
    loaded = (result == S_OK);
    return result;
}

Z7_COM7F_IMF(CAgent::BindToRootFolder(IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = NULL;
    // Source initial-open failures can publish an empty diagnostic folder.
    // A previously closed/reopened owner must not revive retired proxies.
    if (!_archiveLink.IsOpen && _proxyGeneration.load() != 0)
        return E_FAIL;
    if (!_archiveLink.Arcs.IsEmpty()) {
        RINOK(ReadItems())
    }
    CAgentFolder *folderSpec = new CAgentFolder;
    CMyComPtr<IFolderFolder> rootFolder = folderSpec;
    folderSpec->Init(_proxy, _proxy2, k_Proxy_RootDirIndex, this);
    *resultFolder = rootFolder.Detach();
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::Extract(NExtract::NPathMode::EEnum pathMode,
                             NExtract::NOverwriteMode::EEnum overwriteMode, const wchar_t *path,
                             Int32 testMode, IFolderArchiveExtractCallback *extractCallback2)) {
    COM_TRY_BEGIN

    if (!testMode && _isHashHandler)
        return E_NOTIMPL;

    CArchiveExtractCallback *extractCallbackSpec = new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> extractCallback = extractCallbackSpec;
    extractCallbackSpec->InitForMulti(false, // multiArchives
                                      pathMode, overwriteMode, NExtract::NZoneIdMode::kNone,
                                      k_keepEmptyDirPrefixes);

    CExtractNtOptions extractNtOptions;
    extractNtOptions.AltStreams.Val = true;            // change it!!!
    extractNtOptions.AltStreams.Def = true;            // change it!!!
    extractNtOptions.ReplaceColonForAltStream = false; // change it!!!

    extractCallbackSpec->Init(extractNtOptions, NULL, &GetArc(), extractCallback2,
                              false, // stdOutMode
                              IntToBool(testMode), us2fs(path), UStringVector(), false,
                              (UInt64)(Int64)-1);

    extractCallbackSpec->DirPathPrefix_for_HashFiles = _hashBaseFolderPrefix;

#ifdef SUPPORT_LINKS

    if (!testMode) {
        RINOK(extractCallbackSpec->PrepareHardLinks(NULL)) // NULL means all items
    }

#endif

    return GetArchive()->Extract(NULL, (UInt32)(Int32)-1, testMode, extractCallback);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::GetNumberOfProperties(UInt32 *numProps)) {
    COM_TRY_BEGIN
    return GetArchive()->GetNumberOfProperties(numProps);
    COM_TRY_END
}

Z7_COM7F_IMF(CAgent::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType)) {
    COM_TRY_BEGIN
    RINOK(GetArchive()->GetPropertyInfo(index, name, propID, varType))
    if (*propID == kpidPath)
        *propID = kpidName;
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CAgentFolder::Extract(const UInt32 *indices, UInt32 numItems, Int32 includeAltStreams,
                                   Int32 replaceAltStreamColon, NExtract::NPathMode::EEnum pathMode,
                                   NExtract::NOverwriteMode::EEnum overwriteMode,
                                   const wchar_t *path, Int32 testMode,
                                   IFolderArchiveExtractCallback *extractCallback2)) {
    COM_TRY_BEGIN

    auto *status = dynamic_cast<QtArchiveExtractStatusReceiver *>(extractCallback2);
    if (status)
        status->ExtractReport.Status.reset();

    if (!IsBound())
        return E_FAIL;
    RINOK(LoadDirectorySelection(indices, numItems, extractCallback2))

    if (!testMode && _agentSpec->_isHashHandler)
        return E_NOTIMPL;

    CArchiveExtractCallback *extractCallbackSpec = new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> extractCallback = extractCallbackSpec;
    UStringVector pathParts;
    bool isAltStreamFolder = false;
    bool isChangedPath = false;
    if (_proxy2)
        _proxy2->GetDirPathParts(_proxyDirIndex, pathParts, isAltStreamFolder);
    else
        _proxy->GetDirPathParts_isChanged(_proxyDirIndex, pathParts, isChangedPath);
    if (isChangedPath)
        return E_NOTIMPL;
    /*
    if (_flatMode)
      pathMode = NExtract::NPathMode::kNoPathnames;
    */

    extractCallbackSpec->InitForMulti(false, // multiArchives
                                      pathMode, overwriteMode, _zoneMode, k_keepEmptyDirPrefixes);

    if (extractCallback2)
        extractCallback2->SetTotal(_agentSpec->GetArc().GetEstmatedPhySize());

    FString pathU;
    if (path) {
        pathU = us2fs(path);
        if (!pathU.IsEmpty()
#ifdef _WIN32
            && !NWindows::NFile::NName::IsAltStreamPrefixWithColon(path)
#endif
        ) {
            NWindows::NFile::NName::NormalizeDirPathPrefix(pathU);
            NWindows::NFile::NDir::CreateComplexDir(pathU);
        }
    }

    CExtractNtOptions extractNtOptions;
    if (QtExtractNtOptions)
        extractNtOptions = *QtExtractNtOptions;
    extractNtOptions.AltStreams.Val = IntToBool(includeAltStreams); // change it!!!
    extractNtOptions.AltStreams.Def = true;

    extractNtOptions.ReplaceColonForAltStream = IntToBool(replaceAltStreamColon);

    extractCallbackSpec->InitBeforeNewArchive();

#if defined(_WIN32) && !defined(UNDER_CE)
    if (_zoneMode != NExtract::NZoneIdMode::kNone) {
        ReadZoneFile_Of_BaseFile(us2fs(_agentSpec->_archiveFilePath), extractCallbackSpec->ZoneBuf);
        if (_zoneBuf.Size() != 0)
            extractCallbackSpec->ZoneBuf = _zoneBuf;
    }
#endif

    extractCallbackSpec->Init(extractNtOptions, NULL, &_agentSpec->GetArc(), extractCallback2,
                              false, // stdOutMode
                              IntToBool(testMode), pathU, pathParts, isAltStreamFolder,
                              (UInt64)(Int64)-1);
    if (status)
        extractCallbackSpec->QtExtractPurpose = status->ExtractReport.Purpose;

    if (_proxy2)
        extractCallbackSpec->SetBaseParentFolderIndex(
            (unsigned)_proxy2->Dirs[_proxyDirIndex].ArcIndex);

    // do we need another base folder for subfolders ?
    extractCallbackSpec->DirPathPrefix_for_HashFiles = _agentSpec->_hashBaseFolderPrefix;

    CUIntVector realIndices;
    // Match the typed directory extractor's literal Copy selection. Streams
    // explicitly selected in their own folder remain selectable; hidden streams
    // of a selected host must not become extra ordinary destination files.
    const bool includeImplicitStreams =
        IntToBool(includeAltStreams) &&
        !(!testMode && status && status->ExtractReport.Purpose == QtArchiveExtractPurpose::Copy);
    GetRealIndices(indices, numItems,
                   (_loadAltStreams && _flatMode) ? false : includeImplicitStreams,
                   false,        // includeFolderSubItemsInFlatMode
                   realIndices); //

#ifdef SUPPORT_LINKS

    if (!testMode) {
        RINOK(extractCallbackSpec->PrepareHardLinks(&realIndices))
    }

#endif

    {
        CArchiveExtractCallback_Closer ecsCloser(extractCallbackSpec);

        HRESULT res = _agentSpec->GetArchive()->Extract(realIndices.ConstData(), realIndices.Size(),
                                                        testMode, extractCallback);

        const HRESULT res2 = ecsCloser.Close();
        if (res == S_OK)
            res = res2;
        if (res == S_OK && status && !testMode)
            status->ExtractReport.Status = extractCallbackSpec->QtExtractStatus;
        return res;
    }

    COM_TRY_END
}
