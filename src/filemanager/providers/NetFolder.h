// Adapted from 7-Zip CPP/7zip/UI/FileManager/NetFolder.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_NET_FOLDER_H
#define ZIP7_INC_NET_FOLDER_H

#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "common/collections/MyVector.h"
#include <utility>
#ifdef _WIN32
#include "Windows/Net.h"
#else
#include "fileops/volumes/qtvolumemounts.h"
#endif

#ifdef _WIN32
struct CResourceEx : public NWindows::NNet::CResourceW {
    UString Name;
};
#else
struct CResourceEx {
    UString Name;
    UString RemoteName;
    UString Provider;
};
#endif

Z7_CLASS_IMP_NOQIB_2(CNetFolder, IFolderFolder, IFolderGetSystemIconIndex)
#ifdef _WIN32
NWindows::NNet::CResourceW _netResource;
NWindows::NNet::CResourceW *_netResourcePointer;
#else
QtVolumeMountProvider _mountProvider;
#endif

CObjectVector<CResourceEx> _items;

CMyComPtr<IFolderFolder> _parentFolder;
UString _path;

public:
#ifdef _WIN32
CNetFolder() : _netResourcePointer(NULL) {}
#else
explicit CNetFolder(QtVolumeMountProvider provider = QtSystemVolumeMounts)
    : _mountProvider(std::move(provider)) {}
#endif
void Init(const UString &path);
bool IsRootLocation() const;
#ifdef _WIN32
void Init(const NWindows::NNet::CResourceW *netResource, IFolderFolder *parentFolder,
          const UString &path);
#endif
}
;

#endif
