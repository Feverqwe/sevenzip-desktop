// WorkDir.h
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_WORK_DIR_H
#define ZIP7_INC_WORK_DIR_H

#include "fileops/native/FileDir.h"

#include "7zip/Common/FileStreams.h"

#include "archive/operations/ZipRegistry.h"

FString GetWorkDir(const NWorkDir::CInfo &workDirInfo, const FString &path, FString &fileName);

class CWorkDirTempFile MY_UNCOPYABLE {
    FString _originalPath;
    NWindows::NFile::NDir::CTempFile _tempFile;
    COutFileStream *_outStreamSpec;

  public:
    CMyComPtr<IOutStream> OutStream;

    const FString &Get_OriginalFilePath() const Z7_lifetimebound { return _originalPath; }
    const FString &Get_TempFilePath() const Z7_lifetimebound { return _tempFile.GetPath(); }
    const std::shared_ptr<QtOwnedTempLease> &OwnedLease() const { return _tempFile.OwnedLease(); }

    HRESULT CreateTempFile(const FString &originalPath, const QString &settingsPath = {});
    HRESULT SetMTime(const CFiTime &time);
    HRESULT MoveToOriginal(bool deleteOriginal,
                           NWindows::NFile::NDir::ICopyFileProgress *progress = NULL,
                           bool *published = nullptr);
};

#endif
