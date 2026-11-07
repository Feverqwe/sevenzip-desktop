// ExtractCallback.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "ui/diagnostics/qtdiagnostictext.h"

#include "Windows/ErrorMsg.h"

#ifndef Z7_SFX
#include "archive/operations/ZipRegistry.h"
#include "archive/resources/virtualextractfilesystem.h"
#endif

#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"

#include "7zip/UI/FileManager/MemDialogRes.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#include "ui/formatting/qtregionalformat.h"
#ifndef Z7_NO_CRYPTO
#include "archive/callbacks/qtcallbackpassword.h"
#endif

using namespace NWindows;

#include "archive/callbacks/qtcallbackdialogs.h"

CExtractCallbackImp::CExtractCallbackImp()
    : _totalFiles_Defined(false), _totalBytes_Defined(false), MultiArcMode(false),
      ProcessAltStreams(true), StreamMode(false), ThereAreMessageErrors(false),
      Src_Is_IO_FS_Folder(false)
#ifndef Z7_NO_CRYPTO
      ,
      PasswordIsDefined(false), PasswordWasAsked(false)
#endif
      ,
      YesToAll(false), OverwriteMode(NExtract::NOverwriteMode::kAsk)
#ifndef Z7_SFX
      ,
      _hashCalc(NULL)
#endif
{
}

CExtractCallbackImp::~CExtractCallbackImp() {}

void CExtractCallbackImp::Init() {
    LangString(IDS_PROGRESS_EXTRACTING, _lang_Extracting);
    LangString(IDS_PROGRESS_TESTING, _lang_Testing);
    LangString(IDS_PROGRESS_SKIPPING, _lang_Skipping);
    _lang_Reading = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Reading"));

    NumArchiveErrors = 0;
    SomeItemsSkipped = false;
    ExtractReport.Diagnostics.clear();
    ExtractReport.HadErrors = false;
    ThereAreMessageErrors = false;
#ifndef Z7_SFX
    NumFolders = NumFiles = 0;
    NeedAddFile = false;
#endif
}

void CExtractCallbackImp::AddError_Message(LPCWSTR s) {
    ThereAreMessageErrors = true;
    ProgressDialog->Sync.AddError_Message(s);
}

void CExtractCallbackImp::AddError_Message_ShowArcPath(LPCWSTR s) {
    Add_ArchiveName_Error();
    AddError_Message(s);
}

#ifndef Z7_SFX

Z7_COM7F_IMF(CExtractCallbackImp::SetNumFiles(UInt64 numFiles)) {
#ifdef Z7_SFX
    UNUSED_VAR(numFiles)
#else
    ProgressDialog->Sync.Set_NumFilesTotal(numFiles);
#endif
    return S_OK;
}

#endif

Z7_COM7F_IMF(CExtractCallbackImp::SetTotal(UInt64 total)) {
    ProgressDialog->Sync.Set_NumBytesTotal(total);
    return S_OK;
}

Z7_COM7F_IMF(CExtractCallbackImp::SetCompleted(const UInt64 *value)) {
    return ProgressDialog->Sync.Set_NumBytesCur(value);
}

HRESULT CExtractCallbackImp::Open_CheckBreak() { return ProgressDialog->Sync.CheckStop(); }

HRESULT CExtractCallbackImp::Open_SetTotal(const UInt64 *files, const UInt64 *bytes) {
    HRESULT res = S_OK;
    if (!MultiArcMode) {
        if (files) {
            _totalFiles_Defined = true;
            // res = ProgressDialog->Sync.Set_NumFilesTotal(*files);
        } else
            _totalFiles_Defined = false;

        if (bytes) {
            _totalBytes_Defined = true;
            ProgressDialog->Sync.Set_NumBytesTotal(*bytes);
        } else
            _totalBytes_Defined = false;
    }

    return res;
}

HRESULT CExtractCallbackImp::Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) {
    if (!MultiArcMode) {
        if (files) {
            ProgressDialog->Sync.Set_NumFilesCur(*files);
        }

        if (bytes) {
        }
    }

    return ProgressDialog->Sync.CheckStop();
}

HRESULT CExtractCallbackImp::Open_Finished() { return ProgressDialog->Sync.CheckStop(); }

#ifndef Z7_NO_CRYPTO

HRESULT CExtractCallbackImp::Open_CryptoGetTextPassword(BSTR *password) {
    return CryptoGetTextPassword(password);
}

/*
HRESULT CExtractCallbackImp::Open_GetPasswordIfAny(bool &passwordIsDefined, UString &password)
{
  passwordIsDefined = PasswordIsDefined;
  password = Password;
  return S_OK;
}

bool CExtractCallbackImp::Open_WasPasswordAsked()
{
  return PasswordWasAsked;
}

void CExtractCallbackImp::Open_Clear_PasswordWasAsked_Flag()
{
  PasswordWasAsked = false;
}
*/

#endif

#ifndef Z7_SFX
Z7_COM7F_IMF(CExtractCallbackImp::SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize)) {
    ProgressDialog->Sync.Set_Ratio(inSize, outSize);
    return S_OK;
}
#endif

/*
Z7_COM7F_IMF(CExtractCallbackImp::SetTotalFiles(UInt64 total)
{
  ProgressDialog->Sync.SetNumFilesTotal(total);
  return S_OK;
}

Z7_COM7F_IMF(CExtractCallbackImp::SetCompletedFiles(const UInt64 *value)
{
  if (value != NULL)
    ProgressDialog->Sync.SetNumFilesCur(*value);
  return S_OK;
}
*/

Z7_COM7F_IMF(CExtractCallbackImp::PrepareOperation(const wchar_t *name, Int32 isFolder,
                                                   Int32 askExtractMode,
                                                   const UInt64 * /* position */)) {
    _isFolder = IntToBool(isFolder);
    _currentFilePath = name;

    const UString *msg = &_lang_Empty;
    switch (askExtractMode) {
    case NArchive::NExtract::NAskMode::kExtract:
        msg = &_lang_Extracting;
        break;
    case NArchive::NExtract::NAskMode::kTest:
        msg = &_lang_Testing;
        break;
    case NArchive::NExtract::NAskMode::kSkip:
        msg = &_lang_Skipping;
        break;
    case NArchive::NExtract::NAskMode::kReadExternal:
        msg = &_lang_Reading;
        break;
        // default: s = "Unknown operation";
    }

    return ProgressDialog->Sync.Set_Status2(*msg, name, IntToBool(isFolder));
}

Z7_COM7F_IMF(CExtractCallbackImp::MessageError(const wchar_t *s)) {
    AddError_Message(s);
    return S_OK;
}

HRESULT CExtractCallbackImp::MessageError(const char *message, const FString &path) {
    ThereAreMessageErrors = true;
    ProgressDialog->Sync.AddError_Message_Name(QtDiagnosticText(message), fs2us(path));
    return S_OK;
}

#ifndef Z7_SFX

Z7_COM7F_IMF(CExtractCallbackImp::ShowMessage(const wchar_t *s)) {
    AddError_Message(s);
    return S_OK;
}

#endif

Z7_COM7F_IMF(CExtractCallbackImp::SetOperationResult(Int32 opRes, Int32 encrypted)) {
    switch (opRes) {
    case NArchive::NExtract::NOperationResult::kOK:
        break;
    default: {
        UString s;
        SetExtractErrorMessage(opRes, encrypted, _currentFilePath, s);
        AddError_Message_ShowArcPath(s);
    }
    }

    _currentFilePath.Empty();
#ifndef Z7_SFX
    if (_isFolder)
        NumFolders++;
    else
        NumFiles++;
    ProgressDialog->Sync.Set_NumFilesCur(NumFiles);
#endif

    return S_OK;
}

Z7_COM7F_IMF(CExtractCallbackImp::ReportExtractResult(Int32 opRes, Int32 encrypted,
                                                      const wchar_t *name)) {
    if (opRes != NArchive::NExtract::NOperationResult::kOK) {
        UString s;
        SetExtractErrorMessage(opRes, encrypted, name, s);
        AddError_Message_ShowArcPath(s);
    }
    return S_OK;
}

////////////////////////////////////////
// IExtractCallbackUI

HRESULT CExtractCallbackImp::BeforeOpen(const wchar_t *name, bool /* testMode */) {
    _currentArchivePath = name;
    _needWriteArchivePath = true;
#ifndef Z7_SFX
    RINOK(ProgressDialog->Sync.CheckStop())
    ProgressDialog->Sync.Set_TitleFileName(name);
#endif
    return S_OK;
}

HRESULT CExtractCallbackImp::SetCurrentFilePath2(const wchar_t *path) {
    _currentFilePath = path;
#ifndef Z7_SFX
    ProgressDialog->Sync.Set_FilePath(path);
#endif
    return S_OK;
}

#ifndef Z7_SFX

Z7_COM7F_IMF(CExtractCallbackImp::SetCurrentFilePath(const wchar_t *path)) {
#ifndef Z7_SFX
    if (NeedAddFile)
        NumFiles++;
    NeedAddFile = true;
    ProgressDialog->Sync.Set_NumFilesCur(NumFiles);
#endif
    return SetCurrentFilePath2(path);
}

#endif

HRESULT CExtractCallbackImp::OpenResult(const CCodecs *codecs, const CArchiveLink &arcLink,
                                        const wchar_t *name, HRESULT result) {
    _currentArchivePath = name;
    _needWriteArchivePath = true;

    UString s;
    OpenResult_GUI(s, codecs, arcLink, name, result);
    if (!s.IsEmpty()) {
        NumArchiveErrors++;
        AddError_Message(s);
        _needWriteArchivePath = false;
    }

    return S_OK;
}

HRESULT CExtractCallbackImp::ThereAreNoFiles() { return S_OK; }

void CExtractCallbackImp::Add_ArchiveName_Error() {
    if (_needWriteArchivePath) {
        if (!_currentArchivePath.IsEmpty())
            AddError_Message(_currentArchivePath);
        _needWriteArchivePath = false;
    }
}

HRESULT CExtractCallbackImp::ExtractResult(HRESULT result) {
#ifndef Z7_SFX
    ProgressDialog->Sync.Set_FilePath(L"");
#endif

    if (result == S_OK)
        return result;
    NumArchiveErrors++;
    if (result == E_ABORT || result == HRESULT_FROM_WIN32(ERROR_DISK_FULL))
        return result;

    Add_ArchiveName_Error();
    if (!_currentFilePath.IsEmpty())
        MessageError(_currentFilePath);
    MessageError(NError::MyFormatMessage(result));
    return S_OK;
}

#ifndef Z7_NO_CRYPTO

HRESULT CExtractCallbackImp::SetPassword(const UString &password) {
    PasswordIsDefined = true;
    Password = password;
    return S_OK;
}

Z7_COM7F_IMF(CExtractCallbackImp::CryptoGetTextPassword(BSTR *password)) {
    PasswordWasAsked = true;
    if (!PasswordIsDefined) {
        const bool showPassword = NExtract::Read_ShowPassword(SettingsPath);
        bool selectedShowPassword = showPassword;
        if (!AskCallbackPassword(*ProgressDialog, Password, selectedShowPassword))
            return E_ABORT;
        PasswordIsDefined = true;
        if (selectedShowPassword != showPassword)
            NExtract::Save_ShowPassword(SettingsPath, selectedShowPassword);
    }
    return StringToBstr(Password, password);
}

#endif

#ifndef Z7_SFX

Z7_COM7F_IMF(CExtractCallbackImp::RequestMemoryUse(UInt32 flags, UInt32, UInt32,
                                                   const wchar_t *path, UInt64 requiredSize,
                                                   UInt64 *allowedSize, UInt32 *answerFlags)) {
    const bool report = (flags & NRequestMemoryUseFlags::k_IsReport) != 0;
    if (!report) {
        const UInt32 savedLimit = NExtract::Read_LimitGB(SettingsPath);
        if (savedLimit != 0 && savedLimit != UInt32(-1)) {
            const UInt64 savedBytes = UInt64(savedLimit) << 30;
            if (!(flags & NRequestMemoryUseFlags::k_AllowedSize_WasForced) ||
                *allowedSize < savedBytes)
                *allowedSize = savedBytes;
        }
        if (requiredSize <= *allowedSize) {
            *answerFlags = NRequestMemoryAnswerFlags::k_Allow;
            return S_OK;
        }
        if (ProgressDialog->Sync.Get_Stopped()) {
            *answerFlags = NRequestMemoryAnswerFlags::k_Stop;
            return E_ABORT;
        }
        *answerFlags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
        if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected)
            *answerFlags |= NRequestMemoryAnswerFlags::k_SkipArc;
    }

    const bool skipped = (*answerFlags & NRequestMemoryAnswerFlags::k_SkipArc) != 0;
    SomeItemsSkipped |= skipped;
    if (!(flags & NRequestMemoryUseFlags::k_NoErrorMessage)) {
        UString message = LangString(IDS_MEM_REQUIRES_BIG_MEM);
        const auto addSize = [&message](UInt32 label, UInt64 bytes) {
            message.Add_LF();
            AddLangString(message, label);
            message += L": ";
            message += UString(QtRegionalFormat::exactBytes(bytes).toStdWString().c_str());
        };
        addSize(IDS_MEM_REQUIRED_MEM_SIZE, requiredSize);
        addSize(IDS_MEM_CURRENT_MEM_LIMIT, *allowedSize);
        if (path && *path) {
            message.Add_LF();
            message += path;
        }
        if (skipped || (flags & NRequestMemoryUseFlags::k_Report_SkipArc)) {
            message.Add_LF();
            AddLangString(message, IDS_MSG_ARC_UNPACKING_WAS_SKIPPED);
        }
        AddError_Message_ShowArcPath(message);
    }
    return S_OK;
}

#endif
