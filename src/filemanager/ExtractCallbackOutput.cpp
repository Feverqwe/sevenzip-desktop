// Entry output callbacks adapted from 7-Zip UI/FileManager/ExtractCallback.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/PropVariantConv.h"
#include "archive/callbacks/qtcallbackdialogs.h"
#include "archive/resources/virtualextractfilesystem.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/ExtractCallback.h"
#include "fileops/native/FileDir.h"
#include "ui/diagnostics/qtdiagnostictext.h"

using namespace NWindows;
using namespace NFile;
using namespace NFind;

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
    bool renameOnce = false;

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
            case NOverwriteAnswer::kAutoRenameOnce:
                renameOnce = true;
                break;
            default:
                return E_FAIL;
            }
            break;
        }
        default:
            break;
        }

        if (renameOnce || OverwriteMode == NExtract::NOverwriteMode::kRename) {
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
            if (!VirtFileSystem->Files.IsEmpty())
                VirtFileSystem->MaxTotalAllocSize -= VirtFileSystem->Files.Back().Data.Size();
            CVirtFile &file = VirtFileSystem->Files.AddNew();
            _newVirtFileWasAdded = true;
            // file.IsDir = _isFolder;
            file.IsAltStream = _isAltStream;
            file.WrittenSize = 0;
            file.ExpectedSize = 0;
            if (size_Defined)
                file.ExpectedSize = size;

            if (VirtFileSystem->Index_of_MainExtractedFile_in_Files < 0)
                if (!file.IsAltStream || VirtFileSystem->IsAltStreamFile)
                    VirtFileSystem->Index_of_MainExtractedFile_in_Files =
                        (int)(VirtFileSystem->Files.Size() - 1);

            /* if we open only AltStream, then (name) contains only name without "fileName:" prefix
             */
            file.BaseName = name;

            if (file.IsAltStream && !VirtFileSystem->IsAltStreamFile &&
                file.BaseName.IsPrefixedBy_NoCase(VirtFileSystem->FileName)) {
                const unsigned colonPos = VirtFileSystem->FileName.Len();
                if (file.BaseName[colonPos] == ':') {
                    file.ColonWasUsed = true;
                    file.AltStreamName = name + (size_t)colonPos + 1;
                    file.BaseName.DeleteFrom(colonPos);
                    if (Is_ZoneId_StreamName(file.AltStreamName)) {
                        if (VirtFileSystem->Index_of_ZoneBuf_AltStream_in_Files < 0)
                            VirtFileSystem->Index_of_ZoneBuf_AltStream_in_Files =
                                (int)(VirtFileSystem->Files.Size() - 1);
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
        RINOK(VirtFileSystem->CloseMemFile())
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

#endif
