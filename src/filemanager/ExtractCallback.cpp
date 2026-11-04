// ExtractCallback.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "ui/diagnostics/qtdiagnostictext.h"

#include "Windows/ErrorMsg.h"
#include "Windows/FileFind.h"
#include "Windows/PropVariantConv.h"
#include "fileops/native/FileDir.h"

#include "7zip/Common/FilePathAutoRename.h"
#include "archive/resources/virtualextractfilesystem.h"

#ifndef Z7_SFX
#include "archive/operations/ZipRegistry.h"
#endif

#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"

#include "dialogs/mem/MemDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#ifndef Z7_NO_CRYPTO
#include "archive/callbacks/qtcallbackpassword.h"
#endif

using namespace NWindows;
using namespace NFile;
using namespace NFind;

#include "archive/callbacks/qtcallbackdialogs.h"

CExtractCallbackImp::~CExtractCallbackImp() {}

void CExtractCallbackImp::Init() {
    LangString(IDS_PROGRESS_EXTRACTING, _lang_Extracting);
    LangString(IDS_PROGRESS_TESTING, _lang_Testing);
    LangString(IDS_PROGRESS_SKIPPING, _lang_Skipping);
    _lang_Reading = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Reading"));

    NumArchiveErrors = 0;
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

Z7_COM7F_IMF(CExtractCallbackImp::AskOverwrite(const wchar_t *existName, const FILETIME *existTime,
                                               const UInt64 *existSize, const wchar_t *newName,
                                               const FILETIME *newTime, const UInt64 *newSize,
                                               Int32 *answer)) {
    return RunCallbackQuestion(*ProgressDialog, [&]() -> HRESULT {
        COverwriteDialog dialog;

        dialog.OldFileInfo.SetTime2(existTime);
        dialog.OldFileInfo.SetSize2(existSize);
        dialog.OldFileInfo.Path = existName;
        dialog.OldFileInfo.Is_FileSystemFile = true;

        dialog.NewFileInfo.SetTime2(newTime);
        dialog.NewFileInfo.SetSize2(newSize);
        dialog.NewFileInfo.Path = newName;
        dialog.NewFileInfo.Is_FileSystemFile = Src_Is_IO_FS_Folder;

        const INT_PTR writeAnswer = dialog.Create(ProgressDialog);
        const int mapped = NOverwriteDialog::MapButtonResult(writeAnswer);
        if (mapped == NOverwriteAnswer::kCancel && writeAnswer != IDCANCEL)
            return E_FAIL;
        *answer = mapped;
        return writeAnswer == IDCANCEL ? E_ABORT : S_OK;
    });
}

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

Z7_COM7F_IMF(CExtractCallbackImp::AskWrite(const wchar_t *srcPath, Int32 srcIsFolder,
                                           const FILETIME *srcTime, const UInt64 *srcSize,
                                           const wchar_t *destPath, BSTR *destPathResult,
                                           Int32 *writeAnswer)) {
    UString destPathResultTemp = destPath;

    // RINOK(StringToBstr(destPath, destPathResult));

    *destPathResult = NULL;
    *writeAnswer = BoolToInt(false);

    FString destPathSys = us2fs(destPath);
    const bool srcIsFolderSpec = IntToBool(srcIsFolder);
    CFileInfo destFileInfo;

    if (destFileInfo.Find(destPathSys)) {
        if (srcIsFolderSpec) {
            if (!destFileInfo.IsDir()) {
                RINOK(MessageError(
                    QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                      "Cannot replace file with folder with same name"),
                    destPathSys))
                return E_ABORT;
            }
            *writeAnswer = BoolToInt(false);
            return S_OK;
        }

        if (destFileInfo.IsDir()) {
            RINOK(MessageError(QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                                 "Cannot replace folder with file with same name"),
                               destPathSys))
            *writeAnswer = BoolToInt(false);
            return S_OK;
        }

        switch ((int)OverwriteMode) {
        case NExtract::NOverwriteMode::kSkip:
            return S_OK;
        case NExtract::NOverwriteMode::kAsk: {
            Int32 overwriteResult;
            UString destPathSpec = destPath;
            const int slashPos = destPathSpec.ReverseFind_PathSepar();
            destPathSpec.DeleteFrom((unsigned)(slashPos + 1));
            destPathSpec += fs2us(destFileInfo.Name);

            FILETIME destTime;
            FiTime_To_FILETIME(destFileInfo.MTime, destTime);
            RINOK(AskOverwrite(destPathSpec, &destTime, &destFileInfo.Size, srcPath, srcTime,
                               srcSize, &overwriteResult))

            switch (overwriteResult) {
            case NOverwriteAnswer::kCancel:
                return E_ABORT;
            case NOverwriteAnswer::kNo:
                return S_OK;
            case NOverwriteAnswer::kNoToAll:
                OverwriteMode = NExtract::NOverwriteMode::kSkip;
                return S_OK;
            case NOverwriteAnswer::kYes:
                break;
            case NOverwriteAnswer::kYesToAll:
                OverwriteMode = NExtract::NOverwriteMode::kOverwrite;
                break;
            case NOverwriteAnswer::kAutoRename:
                OverwriteMode = NExtract::NOverwriteMode::kRename;
                break;
            default:
                return E_FAIL;
            }
            break;
        }
        default:
            break;
        }

        if (OverwriteMode == NExtract::NOverwriteMode::kRename) {
            if (!AutoRenamePath(destPathSys)) {
                RINOK(MessageError(
                    QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot create name for file"),
                    destPathSys))
                return E_ABORT;
            }
            destPathResultTemp = fs2us(destPathSys);
        } else {
            if (NFind::DoesFileExist_Raw(destPathSys))
                if (!NDir::DeleteFileAlways(destPathSys))
                    if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                        RINOK(MessageError(
                            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot delete output file"),
                            destPathSys))
                        return E_ABORT;
                    }
        }
    }
    *writeAnswer = BoolToInt(true);
    return StringToBstr(destPathResultTemp, destPathResult);
}

Z7_COM7F_IMF(CExtractCallbackImp::UseExtractToStream(Int32 *res)) {
    *res = BoolToInt(StreamMode);
    return S_OK;
}

static HRESULT GetTime(IGetProp *getProp, PROPID propID, FILETIME &ft, bool &ftDefined) {
    ftDefined = false;
    NCOM::CPropVariant prop;
    RINOK(getProp->GetProp(propID, &prop))
    if (prop.vt == VT_FILETIME) {
        ft = prop.filetime;
        ftDefined = (ft.dwHighDateTime != 0 || ft.dwLowDateTime != 0);
    } else if (prop.vt != VT_EMPTY)
        return E_FAIL;
    return S_OK;
}

static HRESULT GetItemBoolProp(IGetProp *getProp, PROPID propID, bool &result) {
    NCOM::CPropVariant prop;
    result = false;
    RINOK(getProp->GetProp(propID, &prop))
    if (prop.vt == VT_BOOL)
        result = VARIANT_BOOLToBool(prop.boolVal);
    else if (prop.vt != VT_EMPTY)
        return E_FAIL;
    return S_OK;
}

Z7_COM7F_IMF(CExtractCallbackImp::GetStream7(const wchar_t *const name, const Int32 isDir,
                                             ISequentialOutStream **outStream,
                                             const Int32 askExtractMode, IGetProp *getProp)) {
    COM_TRY_BEGIN
    *outStream = NULL;
    _newVirtFileWasAdded = false;
    _hashStream_WasUsed = false;
    _needUpdateStat = false;
    _isFolder = IntToBool(isDir);
    _curSize_Defined = false;
    _curSize = 0;

    if (_hashStream)
        _hashStream->ReleaseStream();

    _filePath = name;

    UInt64 size = 0;
    bool size_Defined;
    {
        NCOM::CPropVariant prop;
        RINOK(getProp->GetProp(kpidSize, &prop))
        size_Defined = ConvertPropVariantToUInt64(prop, size);
    }
    if (size_Defined) {
        _curSize = size;
        _curSize_Defined = true;
    }

    GetItemBoolProp(getProp, kpidIsAltStream, _isAltStream);
    if (!ProcessAltStreams && _isAltStream)
        return S_OK;

    /* if (isDir), we don't exit from function here because we still need to
         initialize and use (_hashStream) to calculate Hash of dir name.
       v.26.04 : we have commented incorrect code below:
    // if (isDir) return S_OK; // v.24.09-v.26.03 used this incorrect function exit.
    */

    if (askExtractMode != NArchive::NExtract::NAskMode::kExtract &&
        askExtractMode != NArchive::NExtract::NAskMode::kTest)
        return S_OK;

    _needUpdateStat = true;

    CMyComPtr<ISequentialOutStream> outStreamLoc;

    if (!_isFolder) // v.26.04 : we don't need dir item in VirtFileSystem
        if (VirtFileSystem && askExtractMode == NArchive::NExtract::NAskMode::kExtract) {
            if (!VirtFileSystemSpec->Files.IsEmpty())
                VirtFileSystemSpec->MaxTotalAllocSize -=
                    VirtFileSystemSpec->Files.Back().Data.Size();
            CVirtFile &file = VirtFileSystemSpec->Files.AddNew();
            _newVirtFileWasAdded = true;
            // file.IsDir = _isFolder;
            file.IsAltStream = _isAltStream;
            file.WrittenSize = 0;
            file.ExpectedSize = 0;
            if (size_Defined)
                file.ExpectedSize = size;

            if (VirtFileSystemSpec->Index_of_MainExtractedFile_in_Files < 0)
                if (!file.IsAltStream || VirtFileSystemSpec->IsAltStreamFile)
                    VirtFileSystemSpec->Index_of_MainExtractedFile_in_Files =
                        (int)(VirtFileSystemSpec->Files.Size() - 1);

            /* if we open only AltStream, then (name) contains only name without "fileName:" prefix
             */
            file.BaseName = name;

            if (file.IsAltStream && !VirtFileSystemSpec->IsAltStreamFile &&
                file.BaseName.IsPrefixedBy_NoCase(VirtFileSystemSpec->FileName)) {
                const unsigned colonPos = VirtFileSystemSpec->FileName.Len();
                if (file.BaseName[colonPos] == ':') {
                    file.ColonWasUsed = true;
                    file.AltStreamName = name + (size_t)colonPos + 1;
                    file.BaseName.DeleteFrom(colonPos);
                    if (Is_ZoneId_StreamName(file.AltStreamName)) {
                        if (VirtFileSystemSpec->Index_of_ZoneBuf_AltStream_in_Files < 0)
                            VirtFileSystemSpec->Index_of_ZoneBuf_AltStream_in_Files =
                                (int)(VirtFileSystemSpec->Files.Size() - 1);
                    }
                }
            }
            RINOK(GetTime(getProp, kpidCTime, file.CTime, file.CTime_Defined))
            RINOK(GetTime(getProp, kpidATime, file.ATime, file.ATime_Defined))
            RINOK(GetTime(getProp, kpidMTime, file.MTime, file.MTime_Defined)) {
                NCOM::CPropVariant prop;
                RINOK(getProp->GetProp(kpidAttrib, &prop))
                if (prop.vt == VT_UI4) {
                    file.Attrib = prop.ulVal;
                    file.Attrib_Defined = true;
                }
            }
            outStreamLoc = VirtFileSystem;
        }

    if (_hashStream) {
        _hashStream->SetStream(outStreamLoc);
        outStreamLoc = _hashStream;
        _hashStream->Init(true);
        _hashStream_WasUsed = true;
    }

    /* if (_isFolder && _hashStream_WasUsed) the caller must call
       PrepareOperation7() and SetOperationResult8() because
       we need dir item processing for hash calculation for dir name.
       We set hash stream to (*outStream) for dir item. This is an additional
       marker far caller indicating the need for further processing with
       PrepareOperation7() and SetOperationResult8().
    */
    // if (!_isFolder) // for debug : we can set NULL to *outStream for any dir.
    if (outStreamLoc)
        *outStream = outStreamLoc.Detach();
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CExtractCallbackImp::PrepareOperation7(Int32 askExtractMode)) {
    COM_TRY_BEGIN
    _needUpdateStat = (askExtractMode == NArchive::NExtract::NAskMode::kExtract ||
                       askExtractMode == NArchive::NExtract::NAskMode::kTest ||
                       askExtractMode == NArchive::NExtract::NAskMode::kReadExternal);

    /*
    _extractMode = false;
    switch (askExtractMode)
    {
      case NArchive::NExtract::NAskMode::kExtract:
        if (_testMode)
          askExtractMode = NArchive::NExtract::NAskMode::kTest;
        else
          _extractMode = true;
        break;
    };
    */
    return SetCurrentFilePath2(_filePath);
    COM_TRY_END
}

Z7_COM7F_IMF(CExtractCallbackImp::SetOperationResult8(Int32 opRes, Int32 encrypted, UInt64 size)) {
    COM_TRY_BEGIN
    if (VirtFileSystem && _newVirtFileWasAdded) {
        // FIXME: probably we must request file size from VirtFileSystem
        // _curSize = VirtFileSystem->GetLastFileSize()
        // _curSize_Defined = true;
        RINOK(VirtFileSystemSpec->CloseMemFile())
    }
    if (_hashStream && _hashStream_WasUsed) {
        _hashStream->_hash->Final(_isFolder, _isAltStream, _filePath);
        _curSize = _hashStream->GetSize();
        _curSize_Defined = true;
        _hashStream->ReleaseStream();
        _hashStream_WasUsed = false;
    } else if (_hashCalc && _needUpdateStat) {
        _hashCalc->SetSize(size); // (_curSize) before 21.04
        _hashCalc->Final(_isFolder, _isAltStream, _filePath);
    }
    return SetOperationResult(opRes, encrypted);
    COM_TRY_END
}

Z7_COM7F_IMF(CExtractCallbackImp::RequestMemoryUse(UInt32 flags, UInt32 indexType,
                                                   UInt32 /* index */, const wchar_t *path,
                                                   UInt64 requiredSize, UInt64 *allowedSize,
                                                   UInt32 *answerFlags)) {
    return RunCallbackDialog(*ProgressDialog, [&]() -> HRESULT {
        UInt32 limit_GB = (UInt32)((*allowedSize + ((1u << 30) - 1)) >> 30);

        if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
            UInt64 limit_bytes = *allowedSize;
            const UInt32 limit_GB_Registry = NExtract::Read_LimitGB(SettingsPath);
            if (limit_GB_Registry != 0 && limit_GB_Registry != (UInt32)(Int32)-1) {
                const UInt64 limit_bytes_Registry = (UInt64)limit_GB_Registry << 30;
                // registry_WasForced = true;
                if ((flags & NRequestMemoryUseFlags::k_AllowedSize_WasForced) == 0 ||
                    limit_bytes < limit_bytes_Registry) {
                    limit_bytes = limit_bytes_Registry;
                    limit_GB = limit_GB_Registry;
                }
            }
            *allowedSize = limit_bytes;
            if (requiredSize <= limit_bytes) {
                *answerFlags = NRequestMemoryAnswerFlags::k_Allow;
                return S_OK;
            }
            // default answer can be k_Allow, if limit was not forced,
            // so we change answer to non-allowed here,
            // because user has chance to change limit in GUI.
            *answerFlags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
            if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected)
                *answerFlags |= NRequestMemoryAnswerFlags::k_SkipArc;
        }

        const UInt32 required_GB = (UInt32)((requiredSize + ((1u << 30) - 1)) >> 30);

        CMemDialog dialog;
        dialog.Limit_GB = limit_GB;
        dialog.Required_GB = required_GB;
        dialog.TestMode = TestMode;
        if (MultiArcMode)
            dialog.ArcPath = _currentArchivePath;
        if (path)
            dialog.FilePath = path;

        if (!DisableUserQuestions && (flags & NRequestMemoryUseFlags::k_IsReport) == 0) {
            if (_remember)
                dialog.SkipArc = _skipArc;
            else {
                if (ProgressDialog->Sync.Get_Stopped()) {
                    *answerFlags = NRequestMemoryAnswerFlags::k_Stop;
                    return E_ABORT;
                }
                dialog.ShowRemember =
                    (MultiArcMode || indexType != NArchive::NEventIndexType::kNoIndex || path);
                if (dialog.Create(ProgressDialog) != IDCONTINUE) {
                    *answerFlags = NRequestMemoryAnswerFlags::k_Stop;
                    return E_ABORT;
                }
                if (dialog.NeedSave)
                    NExtract::Save_LimitGB(SettingsPath, dialog.Limit_GB);
                if (dialog.Remember) {
                    _remember = true;
                    _skipArc = dialog.SkipArc;
                }
            }

            *allowedSize = (UInt64)dialog.Limit_GB << 30;
            if (!dialog.SkipArc) {
                *answerFlags = NRequestMemoryAnswerFlags::k_Allow;
                return S_OK;
            }
            *answerFlags =
                NRequestMemoryAnswerFlags::k_SkipArc | NRequestMemoryAnswerFlags::k_Limit_Exceeded;
            flags |= NRequestMemoryUseFlags::k_Report_SkipArc;
        }

        if ((flags & NRequestMemoryUseFlags::k_NoErrorMessage) == 0) {
            UString s = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "ERROR: "));
            dialog.AddInfoMessage_To_String(s);
            s.Add_LF();
            // if (indexType == NArchive::NEventIndexType::kNoIndex)
            if ((flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) ||
                (flags & NRequestMemoryUseFlags::k_Report_SkipArc))
                AddLangString(s, IDS_MSG_ARC_UNPACKING_WAS_SKIPPED);
            /*
                else
                  AddLangString(, IDS_MSG_ARC_FILES_UNPACKING_WAS_SKIPPED);
            */
            AddError_Message_ShowArcPath(s);
        }

        /*
          if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0)
            *answerFlags |= NRequestMemoryAnswerFlags::k_Limit_Exceeded;
        */
        return S_OK;
    });
}

#endif
