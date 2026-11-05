// Adapted from 7-Zip CPP/7zip/UI/FileManager/RootFolder.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Ordinary filesystem binding prerequisite for PanelFolderChange.
// Network/ADS/device-image namespace and icon presentation remain P1-11.2/P0-07/P0-09.
#include "filemanager/providers/RootFolder.h"
#ifdef _WIN32
#include "filemanager/SysIconUtils.h"
#include "filemanager/providers/AltStreamsFolder.h"
#include <ShlObj.h>
#endif
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "filemanager/providers/FSDrives.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/NetFolder.h"
#include "localization/LangUtils.h"
#include "localization/qtlanguageservices.h"
#include <QStandardPaths>

using namespace NWindows;
#if defined(_WIN32) && !defined(UNDER_CE)
#define USE_WIN_PATHS
#endif
static const unsigned kNumRootFolderItems =
#ifdef USE_WIN_PATHS
    4;
#else
    3;
#endif
static const Byte kProps[] = {kpidName};
enum {
    ROOT_INDEX_COMPUTER = 0
#ifdef USE_WIN_PATHS
    ,
    ROOT_INDEX_DOCUMENTS,
    ROOT_INDEX_NETWORK,
    ROOT_INDEX_VOLUMES
#else
    ,
    ROOT_INDEX_MOUNTS,
    ROOT_INDEX_NETWORK
#endif
};
#ifdef USE_WIN_PATHS
static const char *const kVolPrefix = "\\\\.";
#endif

// Windows retains source namespace image-list indices; Qt presents those
// images through the existing QIcon bridge. POSIX uses platform icons.
UString RootFolder_GetName_Computer(int &iconIndex) {
#ifdef USE_WIN_PATHS
    iconIndex = Shell_GetFileInfo_SysIconIndex_for_CSIDL(CSIDL_DRIVES);
#else
    iconIndex = -1;
#endif
    return LangString(IDS_COMPUTER);
}
UString RootFolder_GetName_Network(int &iconIndex) {
#ifdef USE_WIN_PATHS
    iconIndex = Shell_GetFileInfo_SysIconIndex_for_CSIDL(CSIDL_NETWORK);
#else
    iconIndex = -1;
#endif
    return LangString(IDS_NETWORK);
}
UString RootFolder_GetName_Documents(int &iconIndex) {
#ifdef USE_WIN_PATHS
    iconIndex = Shell_GetFileInfo_SysIconIndex_for_CSIDL(CSIDL_PERSONAL);
#else
    iconIndex = -1;
#endif
    return LangString(IDS_DOCUMENTS);
}

void CRootFolder::Init() {
    _names[ROOT_INDEX_COMPUTER] = RootFolder_GetName_Computer(_iconIndices[ROOT_INDEX_COMPUTER]);
#ifdef USE_WIN_PATHS
    _names[ROOT_INDEX_DOCUMENTS] = RootFolder_GetName_Documents(_iconIndices[ROOT_INDEX_DOCUMENTS]);
    _names[ROOT_INDEX_NETWORK] = RootFolder_GetName_Network(_iconIndices[ROOT_INDEX_NETWORK]);
    _names[ROOT_INDEX_VOLUMES] = kVolPrefix;
    _iconIndices[ROOT_INDEX_VOLUMES] = Shell_GetFileInfo_SysIconIndex_for_CSIDL(CSIDL_DRIVES);
#endif
#ifndef USE_WIN_PATHS
    const auto mounts = QCoreApplication::translate("RootFolder", "Volumes").toStdWString();
    _names[ROOT_INDEX_MOUNTS] = mounts.c_str();
    _iconIndices[ROOT_INDEX_MOUNTS] = -1;
    _names[ROOT_INDEX_NETWORK] = RootFolder_GetName_Network(_iconIndices[ROOT_INDEX_NETWORK]);
#endif
}
Z7_COM7F_IMF(CRootFolder::LoadItems()) {
    Init();
    return S_OK;
}
Z7_COM7F_IMF(CRootFolder::GetNumberOfItems(UInt32 *numItems)) {
    *numItems = kNumRootFolderItems;
    return S_OK;
}
Z7_COM7F_IMF(CRootFolder::GetProperty(UInt32 itemIndex, PROPID propID, PROPVARIANT *value)) {
    NCOM::CPropVariant prop;
    switch (propID) {
    case kpidIsDir:
        prop = true;
        break;
    case kpidName:
        prop = _names[itemIndex];
        break;
    }
    prop.Detach(value);
    return S_OK;
}
#ifdef USE_WIN_PATHS
static UString GetMyDocsPath() {
    const std::wstring native =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdWString();
    UString us = native.c_str();
    NFile::NName::NormalizeDirPathPrefix(us);
    return us;
}
#endif
Z7_COM7F_IMF(CRootFolder::BindToFolder(UInt32 index, IFolderFolder **resultFolder)) {
    *resultFolder = NULL;
    CMyComPtr<IFolderFolder> subFolder;
#ifdef USE_WIN_PATHS
    if (index == ROOT_INDEX_COMPUTER || index == ROOT_INDEX_VOLUMES) {
        CFSDrives *fsDrivesSpec = new CFSDrives;
        subFolder = fsDrivesSpec;
        fsDrivesSpec->Init(index == ROOT_INDEX_VOLUMES);
    } else if (index == ROOT_INDEX_NETWORK) {
        CNetFolder *network = new CNetFolder;
        subFolder = network;
        network->Init(NULL, NULL, _names[ROOT_INDEX_NETWORK] + WCHAR_PATH_SEPARATOR);
    } else if (index == ROOT_INDEX_DOCUMENTS) {
        UString s = GetMyDocsPath();
        if (!s.IsEmpty()) {
            NFsFolder::CFSFolder *fsFolderSpec = new NFsFolder::CFSFolder;
            subFolder = fsFolderSpec;
            RINOK(fsFolderSpec->Init(us2fs(s)))
        }
    }
#else
    if (index == ROOT_INDEX_COMPUTER) {
        CFSDrives *drives = new CFSDrives;
        subFolder = drives;
        drives->Init();
    } else if (index == ROOT_INDEX_MOUNTS) {
        CFSDrives *mounts = new CFSDrives;
        subFolder = mounts;
        mounts->Init();
    } else if (index == ROOT_INDEX_NETWORK) {
        CNetFolder *network = new CNetFolder;
        subFolder = network;
        network->Init(_names[ROOT_INDEX_NETWORK] + WCHAR_PATH_SEPARATOR);
    }
#endif
    else
        return E_INVALIDARG;
    *resultFolder = subFolder.Detach();
    return S_OK;
}
static bool AreEqualNames(const UString &path, const wchar_t *name) {
    unsigned len = MyStringLen(name);
    if (len > path.Len() || len + 1 < path.Len())
        return false;
    if (len + 1 == path.Len() && !IS_PATH_SEPAR(path[len]))
        return false;
    return path.IsPrefixedBy(name);
}
Z7_COM7F_IMF(CRootFolder::BindToFolder(const wchar_t *name, IFolderFolder **resultFolder)) {
    *resultFolder = NULL;
    UString name2 = name;
    name2.Trim();
    if (name2.IsEmpty()) {
        CRootFolder *rootFolderSpec = new CRootFolder;
        CMyComPtr<IFolderFolder> rootFolder = rootFolderSpec;
        rootFolderSpec->Init();
        *resultFolder = rootFolder.Detach();
        return S_OK;
    }
    for (unsigned i = 0; i < kNumRootFolderItems; i++)
        if (AreEqualNames(name2, _names[i]))
            return BindToFolder((UInt32)i, resultFolder);
#ifdef USE_WIN_PATHS
    if (AreEqualNames(name2, L"My Documents") || AreEqualNames(name2, L"Documents"))
        return BindToFolder((UInt32)ROOT_INDEX_DOCUMENTS, resultFolder);
#else
    if (name2.IsEqualTo(STRING_PATH_SEPARATOR)) {
        NFsFolder::CFSFolder *fsFolder = new NFsFolder::CFSFolder;
        CMyComPtr<IFolderFolder> root = fsFolder;
        fsFolder->InitToRoot();
        *resultFolder = root.Detach();
        return S_OK;
    }
#endif
    if (AreEqualNames(name2, L"My Computer") || AreEqualNames(name2, L"Computer"))
        return BindToFolder((UInt32)ROOT_INDEX_COMPUTER, resultFolder);
    if (name2.IsEqualTo(STRING_PATH_SEPARATOR)) {
        CMyComPtr<IFolderFolder> subFolder = this;
        *resultFolder = subFolder.Detach();
        return S_OK;
    }
    if (name2.Len() < 2)
        return E_INVALIDARG;
    CMyComPtr<IFolderFolder> subFolder;
#ifdef USE_WIN_PATHS
    if (name2.IsPrefixedBy_Ascii_NoCase(kVolPrefix)) {
        CFSDrives *folderSpec = new CFSDrives;
        subFolder = folderSpec;
        folderSpec->Init(true);
    } else if (name2.IsEqualTo(NFile::NName::kSuperPathPrefix)) {
        CFSDrives *folderSpec = new CFSDrives;
        subFolder = folderSpec;
        folderSpec->Init(false, true);
    } else if (name2.Back() == ':' && (name2.Len() != 2 || !NFile::NName::IsDrivePath2(name2))) {
        NAltStreamsFolder::CAltStreamsFolder *folderSpec = new NAltStreamsFolder::CAltStreamsFolder;
        subFolder = folderSpec;
        if (folderSpec->Init(us2fs(name2)) != S_OK)
            return E_INVALIDARG;
    } else
#endif
    {
        NFile::NName::NormalizeDirPathPrefix(name2);
        NFsFolder::CFSFolder *fsFolderSpec = new NFsFolder::CFSFolder;
        subFolder = fsFolderSpec;
        if (fsFolderSpec->Init(us2fs(name2)) != S_OK) {
#ifdef USE_WIN_PATHS
            if (IS_PATH_SEPAR(name2[0])) {
                CNetFolder *netFolderSpec = new CNetFolder;
                subFolder = netFolderSpec;
                netFolderSpec->Init(name2);
            } else
#endif
                return E_INVALIDARG;
        }
    }
    *resultFolder = subFolder.Detach();
    return S_OK;
}
Z7_COM7F_IMF(CRootFolder::BindToParentFolder(IFolderFolder **resultFolder)) {
    *resultFolder = NULL;
    return S_OK;
}
IMP_IFolderFolder_Props(CRootFolder)
    Z7_COM7F_IMF(CRootFolder::GetFolderProperty(PROPID propID, PROPVARIANT *value)) {
    NCOM::CPropVariant prop;
    switch (propID) {
    case kpidType:
        prop = "RootFolder";
        break;
    case kpidPath:
        prop = "";
        break;
    }
    prop.Detach(value);
    return S_OK;
}
Z7_COM7F_IMF(CRootFolder::GetSystemIconIndex(UInt32 index, Int32 *iconIndex)) {
    *iconIndex = _iconIndices[index];
    return S_OK;
}
