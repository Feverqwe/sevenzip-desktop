// Declarations extracted from upstream PanelSplitFile.cpp (0766b733).
#pragma once

#include "Common/MyString.h"
#include "Windows/FileIO.h"
#include "common/collections/MyVector.h"
#include "fileops/volumes/qtvolumeprogress.h"

struct CVolSeqName {
    UString UnchangedPart;
    UString ChangedPart;
    CVolSeqName() : ChangedPart("000") {}
    void SetNumDigits(UInt64 numVolumes);
    bool ParseName(const UString &name);
    UString GetNextName();
    UString GetCombinedName() const;
};

// Qt replaces the QWidget-bearing CProgressThreadVirt base. Output creation
// crosses the transaction boundary; all reads/writes remain native NIO.
class QtVolumeWorker {
  public:
    QtVolumeProgress Sync;
    UStringVector ErrorPaths;
    std::function<bool(NWindows::NFile::NIO::COutFile &, const FString &)> createOutput;
    void AddErrorPath(const FString &path) { ErrorPaths.Add(fs2us(path)); }
    bool CreateOutput(NWindows::NFile::NIO::COutFile &file, const FString &path);
};

struct CCombineVolumePlan {
    FString InputDirPrefix;
    FStringVector Names;
    FString OutputPath;
    UInt64 TotalSize = 0;
    UString GetInfo() const;
    void LoadParts(const UString &srcPath, const UString &itemName, CVolSeqName &volSeqName);
};

struct CSplitVolumePlan {
    FString FilePath;
    FString VolBasePath;
    UInt64 NumVolumes = 0;
    CRecordVector<UInt64> VolumeSizes;
};

class CThreadCombine : public QtVolumeWorker, public CCombineVolumePlan {
  public:
    explicit CThreadCombine(const CCombineVolumePlan &plan) : CCombineVolumePlan(plan) {}
    HRESULT ProcessVirt();
};

class CThreadSplit : public QtVolumeWorker, public CSplitVolumePlan {
  public:
    explicit CThreadSplit(const CSplitVolumePlan &plan) : CSplitVolumePlan(plan) {}
    HRESULT ProcessVirt();
};
