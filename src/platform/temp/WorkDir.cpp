// WorkDir.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Windows/StdAfx.h"

#include "Windows/FileName.h"
#include "Windows/FileSystem.h"

#include "platform/temp/WorkDir.h"
#include "settings/RegistryUtils.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

FString GetWorkDir(const NWorkDir::CInfo &workDirInfo, const FString &path, FString &fileName) {
    NWorkDir::NMode::EEnum mode = workDirInfo.Mode;

#if defined(_WIN32) && !defined(UNDER_CE)
    if (workDirInfo.ForRemovableOnly) {
        mode = NWorkDir::NMode::kCurrent;
        const FString prefix = path.Left(3);
        if (NName::IsDrivePath(prefix)) {
            const UINT driveType = NSystem::MyGetDriveType(prefix);
            if (driveType == DRIVE_CDROM || driveType == DRIVE_REMOVABLE)
                mode = workDirInfo.Mode;
        }
        /*
        CParsedPath parsedPath;
        parsedPath.ParsePath(archiveName);
        UINT driveType = GetDriveType(parsedPath.Prefix);
        if ((driveType != DRIVE_CDROM) && (driveType != DRIVE_REMOVABLE))
          mode = NZipSettings::NWorkDir::NMode::kCurrent;
        */
    }
#endif

    const int pos = path.ReverseFind_PathSepar() + 1;
    fileName = path.Ptr((unsigned)pos);

    FString tempDir;
    switch ((int)mode) {
    case NWorkDir::NMode::kCurrent:
        tempDir = path.Left((unsigned)pos);
        break;
    case NWorkDir::NMode::kSpecified:
        tempDir = workDirInfo.Path;
        break;
    // case NWorkDir::NMode::kSystem:
    default:
        if (!MyGetTempPath(tempDir))
            throw 141717;
        break;
    }
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
