// Adapted from 7-Zip UI/FileManager/FSFolder.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Windows/FileFind.h"
#include "common/collections/MyVector.h"
#include "localization/TextPairs.h"
#include <QFileDevice>
#include <QFileInfo>
#include <QString>
#include <functional>
#include <optional>
#include <string>

namespace NFsFolder {
#define FS_SHOW_LINKS_INFO
struct CDirItem : public NWindows::NFile::NFind::CFileInfo {
    // Derived only from this loaded record's Name; Clear/LoadItems replaces it.
    std::optional<std::u16string> NameSortKey;
    CByteBuffer Reparse;
    UInt64 PackSize = 0;
    bool PackSize_Defined = false;
    CFiTime ChangeTime{};
    UInt64 FileIndex = 0;
    UInt32 NumLinks = 0;
    bool FileInfo_Defined = false;
    bool FileInfo_WasRequested = false;
    bool ChangeTime_Defined = false;
    bool ChangeTime_WasRequested = false;
    bool FolderStat_Defined = false;
    int Parent = -1;
    UInt64 NumFolders = 0;
    UInt64 NumFiles = 0;
};

struct CFsFolderSizeResult {
    HRESULT Status;
    UInt64 Size;
    UInt64 NumFolders;
    UInt64 NumFiles;
};

CFsFolderSizeResult MeasureFolderSize(const FString &path, IProgress *progress);

struct CFsFolderStat {
    UInt64 NumFolders = 0;
    UInt64 NumFiles = 0;
    UInt64 Size = 0;
    IProgress *Progress = nullptr;
    FString Path;

    CFsFolderStat(const FString &path, IProgress *progress Z7_lifetimebound = nullptr)
        : Progress(progress), Path(path) {}
    HRESULT Enumerate();
};

// Source folder owner. Runtime panel loading and destructive mutations use
// narrow Qt/platform services; CopyTo retains the upstream callback contract.
class CFSFolder final : public IFolderFolder,
                        public IFolderCompare,
                        public IArchiveGetRawProps,
                        public IFolderCalcItemFullSize,
                        public IFolderClone,
                        public IFolderSetFlatMode,
                        public IFolderWasChanged,
                        public IFolderOperations,
                        public IFolderGetSystemIconIndex,
#ifdef USE_UNICODE_FSTRING
                        public IFolderGetItemName,
#endif
                        public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderFolder)
    Z7_COM_QI_ENTRY(IArchiveGetRawProps)
    Z7_COM_QI_ENTRY(IFolderCompare)
    Z7_COM_QI_ENTRY(IFolderCalcItemFullSize)
    Z7_COM_QI_ENTRY(IFolderClone)
    Z7_COM_QI_ENTRY(IFolderSetFlatMode)
    Z7_COM_QI_ENTRY(IFolderWasChanged)
    Z7_COM_QI_ENTRY(IFolderOperations)
    Z7_COM_QI_ENTRY(IFolderGetSystemIconIndex)
#ifdef USE_UNICODE_FSTRING
    Z7_COM_QI_ENTRY(IFolderGetItemName)
#endif
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    CFSFolder() = default;
    explicit CFSFolder(QString path);
    Z7_IFACE_COM7_IMP(IFolderFolder)
    Z7_IFACE_COM7_IMP(IArchiveGetRawProps)
    Z7_IFACE_COM7_IMP(IFolderCompare)
    Z7_IFACE_COM7_IMP(IFolderCalcItemFullSize)
    Z7_IFACE_COM7_IMP(IFolderClone)
    Z7_IFACE_COM7_IMP(IFolderSetFlatMode)
    Z7_IFACE_COM7_IMP(IFolderWasChanged)
    Z7_IFACE_COM7_IMP(IFolderOperations)
    Z7_IFACE_COM7_IMP(IFolderGetSystemIconIndex)
#ifdef USE_UNICODE_FSTRING
    Z7_IFACE_COM7_IMP(IFolderGetItemName)
#endif
    HRESULT Init(const FString &path);
    HRESULT InitToRoot() { return Init((FString)FSTRING_PATH_SEPARATOR); }
    CObjectVector<CDirItem> Files;
    FStringVector Folders;
    void Clear() {
        Files.Clear();
        Folders.Clear();
        // Streams.Clear();
    }
    QString ErrorMessage;
    std::function<bool(quint64)> SetCompleted;
    // LoadItems uses these existing integration callbacks for count progress
    // and cooperative cancellation; no Qt widgets run in the source body.
    std::function<bool(const QString &)> CheckBreak;
    void AddPath(const QString &path);
    FString GetRelPath(const CDirItem &item) const;
    bool CanCalculateItemFullSize(UInt32 index) const;
    FString ItemFullSizePath(UInt32 index) const;
    void ApplyItemFullSize(UInt32 index, const CFsFolderSizeResult &result);

  private:
    HRESULT LoadSubItems(int dirItem, const FString &relPrefix);
    HRESULT BindToFolderSpec(CFSTR name, IFolderFolder **resultFolder);
    void GetAbsPath(const wchar_t *name, FString &absPath) const;
    FString _path;
    bool _flatMode = false;
    bool _commentsAreLoaded = false;
    CPairsStorage _comments;
    bool LoadComments();
    bool ReadFileInfo(CDirItem &item);
    void ReadChangeTime(CDirItem &item);
#ifdef _WIN32
    NWindows::NFile::NFind::CFindChangeNotification _findChangeNotification;
#endif
};

struct CCopyStateIO {
    int ErrorFileIndex = -1;
    QString ErrorMessage;
    quint64 CurrentSize = 0;
    quint64 StartPos = 0;
    quint64 TotalSize = 0;
    bool DeleteSrcFile = false;
    IProgress *Progress = nullptr;
    std::function<bool(quint64)> QtProgress;
    HRESULT MyCopyFile(CFSTR inPath, CFSTR outPath, DWORD attrib = DWORD(-1));
    bool MyCopyFile(const QString &inPath, const QString &outPath, QFileDevice::Permissions attrib);
};
bool IsDestChild(const QString &src, const QString &dest);

struct CCopyState {
    quint64 StartPos = 0;
    quint64 TotalSize = 0;
    quint64 FileSize = 0;
    HRESULT ProgressResult = S_OK;
    QString ErrorMessage;
    std::function<bool(quint64)> Progress;
    std::function<void(const QString &)> CurrentFile;
    IFolderOperationsExtractCallback *Callback = nullptr;
    bool MoveMode = false;
    bool UseReadWriteMode = false;
    bool IsAltStreamsDest = false;
    bool CreatedDirectory = false;
#ifdef _WIN32
    bool CopyFile_NT(const wchar_t *oldFile, const wchar_t *newFile);
    bool CopyFile_Sys(CFSTR oldFile, CFSTR newFile);
#endif
    bool CopyFile_Sys(const QString &oldFile, const QString &newFile,
                      QFileDevice::Permissions attrib);
    bool MoveFile_Sys(const QString &oldFile, const QString &newFile,
                      QFileDevice::Permissions attrib);
    DWORD ReportSystemProgress(UInt64 total, UInt64 current);
    bool CallProgress();
};
bool CopyFile_Ask(CCopyState &state, const QString &srcPath, const QFileInfo &srcFileInfo,
                  const QString &destPath);
bool CopyFolder(CCopyState &state, const QString &srcPath, const QString &destPath);
HRESULT CopyFileSystemItems(const UStringVector &itemsPaths, const FString &destDirPrefix,
                            bool moveMode, IFolderOperationsExtractCallback *callback);
} // namespace NFsFolder
