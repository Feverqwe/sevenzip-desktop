// Adapted from 7-Zip CPP/7zip/UI/FileManager/OpenCallback.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/StdAfx.h"

#include "Common/ComTry.h"
#include "Common/StringConvert.h"

#include "Windows/FileName.h"
#include "Windows/PropVariant.h"

#include "7zip/Common/FileStreams.h"

#include "archive/operations/ZipRegistry.h"

#include "archive/callbacks/qtcallbackpassword.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/OpenCallback.h"

using namespace NWindows;

HRESULT COpenArchiveCallback::Open_SetTotal(const UInt64 *numFiles, const UInt64 *numBytes)
// Z7_COM7F_IMF(COpenArchiveCallback::SetTotal(const UInt64 *numFiles, const UInt64 *numBytes))
{
    // COM_TRY_BEGIN
    RINOK(ProgressDialog.Sync.CheckStop()) {
        // NSynchronization::CCriticalSectionLock lock(_criticalSection);
        ProgressDialog.Sync.Set_NumFilesTotal(numFiles ? *numFiles : (UInt64)(Int64)-1);
        // if (numFiles)
        {
            ProgressDialog.Sync.Set_FilesProgressMode(numFiles != NULL);
        }
        if (numBytes)
            ProgressDialog.Sync.Set_NumBytesTotal(*numBytes);
    }
    return S_OK;
    // COM_TRY_END
}

HRESULT COpenArchiveCallback::Open_SetCompleted(const UInt64 *numFiles, const UInt64 *numBytes)
// Z7_COM7F_IMF(COpenArchiveCallback::SetCompleted(const UInt64 *numFiles, const UInt64 *numBytes))
{
    // COM_TRY_BEGIN
    // NSynchronization::CCriticalSectionLock lock(_criticalSection);
    if (numFiles)
        ProgressDialog.Sync.Set_NumFilesCur(*numFiles);
    if (numBytes)
        ProgressDialog.Sync.Set_NumBytesCur(*numBytes);
    return ProgressDialog.Sync.CheckStop();
    // COM_TRY_END
}

HRESULT COpenArchiveCallback::Open_CheckBreak() { return ProgressDialog.Sync.CheckStop(); }

HRESULT COpenArchiveCallback::Open_Finished() { return ProgressDialog.Sync.CheckStop(); }

#ifndef Z7_NO_CRYPTO
HRESULT COpenArchiveCallback::Open_FscryptKeys(BSTR *keys) {
    PasswordWasAsked = true;
    if (!FilesystemKeysDefined && PasswordIsDefined && Password.IsPrefixedBy(L"fscrypt:")) {
        FilesystemKeys.SetFrom(Password.Ptr(), Password.Len());
        FilesystemKeysDefined = true;
    }
    if (!FilesystemKeysDefined) {
        if (!AskCallbackFilesystemKeys(ProgressDialog, FilesystemKeys))
            return E_ABORT;
        FilesystemKeysDefined = true;
    }
    Password = FilesystemKeys;
    PasswordIsDefined = true;
    return StringToBstr(FilesystemKeys, keys);
}

HRESULT COpenArchiveCallback::Open_CryptoGetTextPassword(BSTR *password)
// Z7_COM7F_IMF(COpenArchiveCallback::CryptoGetTextPassword(BSTR *password))
{
    // COM_TRY_BEGIN
    PasswordWasAsked = true;
    if (!PasswordIsDefined) {
        bool showPassword = NExtract::Read_ShowPassword(SettingsPath);
        bool selectedShowPassword = showPassword;

        if (!AskCallbackPassword(ProgressDialog, Password, selectedShowPassword,
                                 PasswordArchivePath, true))
            return E_ABORT;

        PasswordIsDefined = true;
        if (selectedShowPassword != showPassword)
            NExtract::Save_ShowPassword(SettingsPath, selectedShowPassword);
    }
    return StringToBstr(Password, password);
    // COM_TRY_END
}
#endif
