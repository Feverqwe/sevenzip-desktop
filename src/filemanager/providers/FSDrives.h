// Adapted from 7-Zip CPP/7zip/UI/FileManager/FSDrives.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "fileops/volumes/qtvolumemounts.h"

struct CDriveInfo {
    FString Name;
    FString FullSystemName;
    UInt64 DriveSize = 0;
    UInt64 FreeSpace = 0;
    UInt64 ClusterSize = 0;
    UString VolumeName;
    UString FileSystemName;
    UINT DriveType = 0;
    bool KnownSize;
    bool KnownSizes;
    bool IsPhysicalDrive;
#ifdef _WIN32
    FString GetDeviceFileIoName() const;
#endif
    CDriveInfo() : KnownSize(false), KnownSizes(false), IsPhysicalDrive(false) {}
};

class CFSDrives final : public IFolderFolder,
#ifdef _WIN32
                        public IFolderOperations,
                        public IFolderGetSystemIconIndex,
#endif
                        public CMyUnknownImp {
#ifdef _WIN32
    Z7_COM_UNKNOWN_IMP_3(IFolderFolder, IFolderOperations, IFolderGetSystemIconIndex)
#else
    Z7_COM_UNKNOWN_IMP_1(IFolderFolder)
#endif
  public:
    Z7_IFACE_COM7_IMP(IFolderFolder)
#ifdef _WIN32
    Z7_IFACE_COM7_IMP(IFolderOperations)
    Z7_IFACE_COM7_IMP(IFolderGetSystemIconIndex)
#endif
    explicit CFSDrives(QtVolumeMountProvider mountProvider = QtSystemVolumeMounts)
        : _mountProvider(std::move(mountProvider)) {}
    void Init(bool volMode = false, bool superMode = false) {
        _volumeMode = volMode;
        _superMode = superMode;
    }

  private:
    CObjectVector<CDriveInfo> _drives;
    bool _volumeMode = false;
    bool _superMode = false;
    QtVolumeMountProvider _mountProvider;
    HRESULT BindToFolderSpec(CFSTR name, IFolderFolder **resultFolder);
    void AddExt(FString &s, unsigned index) const;
#ifdef _WIN32
    HRESULT GetFileSize(unsigned index, UInt64 &fileSize) const;
#endif
};
