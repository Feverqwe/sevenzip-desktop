// WorkDir.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Windows/StdAfx.h"

#include "Windows/FileName.h"

#include "platform/temp/WorkDir.h"
#include "settings/RegistryUtils.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

FString GetWorkDir(const NWorkDir::CInfo &workDirInfo, const FString &path, FString &fileName) {
    const int pos = path.ReverseFind_PathSepar() + 1;
    fileName = path.Ptr((unsigned)pos);

    FString tempDir;
    if (workDirInfo.Mode == NWorkDir::NMode::kSpecified)
        tempDir = workDirInfo.Path;
    else if (!MyGetTempPath(tempDir))
        throw 141717;
    NName::NormalizeDirPathPrefix(tempDir);
    return tempDir;
}

HRESULT CWorkDirTempFile::CreateTempFile(const FString &originalPath, const QString &settingsPath) {
    NWorkDir::CInfo workDirInfo;
    workDirInfo.Load(settingsPath.isEmpty() ? FmSettingsPath() : settingsPath);
    FString namePart;
    FString path = GetWorkDir(workDirInfo, originalPath, namePart);
    CreateComplexDir(path);
    path += namePart;
    _outStreamSpec = new COutFileStream;
    OutStream = _outStreamSpec;
    if (!_tempFile.Create(path, &_outStreamSpec->File))
        return GetLastError_noZero_HRESULT();
    _originalPath = originalPath;
    return S_OK;
}

HRESULT CWorkDirTempFile::SetMTime(const CFiTime &time) {
    return _outStreamSpec->SetMTime(&time) ? S_OK : GetLastError_noZero_HRESULT();
}

HRESULT CWorkDirTempFile::MoveToOriginal(bool deleteOriginal,
                                         NWindows::NFile::NDir::ICopyFileProgress *progress,
                                         bool *published) {
    OutStream.Release();
    if (!_tempFile.MoveTo(_originalPath, deleteOriginal, progress, published))
        return GetLastError_noZero_HRESULT();
    return S_OK;
}
