// Adapted from 7-Zip CPP/7zip/UI/FileManager/ExtractCallback.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native result/flag report composition with existing Qt diagnostic translation.
#include "archive/diagnostics/archiveerrormessages.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/IntToString.h"
#include "archive/opening/OpenArchive.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/PropertyName.h"
#include "localization/LangUtils.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/ProgressDialog2.h"

void SetExtractErrorMessage(Int32 opRes, Int32 encrypted, const wchar_t *fileName, UString &s) {
    s.Empty();

    if (opRes == NArchive::NExtract::NOperationResult::kOK)
        return;

#ifndef Z7_SFX
    UINT messageID = 0;
#endif
    UINT id = 0;

    switch (opRes) {
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
#ifndef Z7_SFX
        messageID = IDS_EXTRACT_MESSAGE_UNSUPPORTED_METHOD;
#endif
        id = IDS_EXTRACT_MSG_UNSUPPORTED_METHOD;
        break;
    case NArchive::NExtract::NOperationResult::kDataError:
#ifndef Z7_SFX
        messageID =
            encrypted ? IDS_EXTRACT_MESSAGE_DATA_ERROR_ENCRYPTED : IDS_EXTRACT_MESSAGE_DATA_ERROR;
#endif
        id = IDS_EXTRACT_MSG_DATA_ERROR;
        break;
    case NArchive::NExtract::NOperationResult::kCRCError:
#ifndef Z7_SFX
        messageID =
            encrypted ? IDS_EXTRACT_MESSAGE_CRC_ERROR_ENCRYPTED : IDS_EXTRACT_MESSAGE_CRC_ERROR;
#endif
        id = IDS_EXTRACT_MSG_CRC_ERROR;
        break;
    case NArchive::NExtract::NOperationResult::kUnavailable:
        id = IDS_EXTRACT_MSG_UNAVAILABLE_DATA;
        break;
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
        id = IDS_EXTRACT_MSG_UEXPECTED_END;
        break;
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
        id = IDS_EXTRACT_MSG_DATA_AFTER_END;
        break;
    case NArchive::NExtract::NOperationResult::kIsNotArc:
        id = IDS_EXTRACT_MSG_IS_NOT_ARC;
        break;
    case NArchive::NExtract::NOperationResult::kHeadersError:
        id = IDS_EXTRACT_MSG_HEADERS_ERROR;
        break;
    case NArchive::NExtract::NOperationResult::kWrongPassword:
        id = IDS_EXTRACT_MSG_WRONG_PSW_CLAIM;
        break;
        /*
        default:
          messageID = IDS_EXTRACT_MESSAGE_UNKNOWN_ERROR;
          break;
        */
    }

    UString msg;

#ifndef Z7_SFX
    UString msgOld;
#ifdef Z7_LANG
    if (id != 0)
        LangString_OnlyFromLangFile(id, msg);
    if (messageID != 0 && msg.IsEmpty())
        LangString_OnlyFromLangFile(messageID, msgOld);
#endif
    if (msg.IsEmpty() && !msgOld.IsEmpty())
        s = MyFormatNew(msgOld, fileName);
    else
#endif
    {
        if (msg.IsEmpty() && id != 0)
            LangString(id, msg);
        if (!msg.IsEmpty())
            s += msg;
        else {
            s += QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error #%1"),
                                  (UInt32)opRes);
        }

        if (encrypted && opRes != NArchive::NExtract::NOperationResult::kWrongPassword) {
            // s += " : ";
            // AddLangString(s, IDS_EXTRACT_MSG_ENCRYPTED);
            s += " : ";
            AddLangString(s, IDS_EXTRACT_MSG_WRONG_PSW_GUESS);
        }
        s += " : ";
        s += fileName;
    }
}

static const UInt32 k_ErrorFlagsIds[] = {
    IDS_EXTRACT_MSG_IS_NOT_ARC,       IDS_EXTRACT_MSG_HEADERS_ERROR,
    IDS_EXTRACT_MSG_HEADERS_ERROR,    IDS_OPEN_MSG_UNAVAILABLE_START,
    IDS_OPEN_MSG_UNCONFIRMED_START,   IDS_EXTRACT_MSG_UEXPECTED_END,
    IDS_EXTRACT_MSG_DATA_AFTER_END,   IDS_EXTRACT_MSG_UNSUPPORTED_METHOD,
    IDS_OPEN_MSG_UNSUPPORTED_FEATURE, IDS_EXTRACT_MSG_DATA_ERROR,
    IDS_EXTRACT_MSG_CRC_ERROR};

static void AddNewLineString(UString &s, const UString &m) {
    s += m;
    s.Add_LF();
}

UString GetOpenArcErrorMessage(UInt32 errorFlags) {
    UString s;

    for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_ErrorFlagsIds); i++) {
        const UInt32 f = (UInt32)1 << i;
        if ((errorFlags & f) == 0)
            continue;
        const UInt32 id = k_ErrorFlagsIds[i];
        UString m = LangString(id);
        if (m.IsEmpty())
            continue;
        if (f == kpv_ErrorFlags_EncryptedHeadersError) {
            m += " : ";
            AddLangString(m, IDS_EXTRACT_MSG_WRONG_PSW_GUESS);
        }
        if (!s.IsEmpty())
            s.Add_LF();
        s += m;
        errorFlags &= ~f;
    }

    if (errorFlags != 0) {
        char sz[16];
        sz[0] = '0';
        sz[1] = 'x';
        ConvertUInt32ToHex(errorFlags, sz + 2);
        if (!s.IsEmpty())
            s.Add_LF();
        s += sz;
    }

    return s;
}

static void ErrorInfo_Print(UString &s, const CArcErrorInfo &er) {
    const UInt32 errorFlags = er.GetErrorFlags();
    const UInt32 warningFlags = er.GetWarningFlags();

    if (errorFlags != 0)
        AddNewLineString(s, GetOpenArcErrorMessage(errorFlags));

    if (!er.ErrorMessage.IsEmpty())
        AddNewLineString(s, QtArchiveDiagnosticText(er.ErrorMessage));

    if (warningFlags != 0) {
        s += GetNameOfProperty(kpidWarningFlags, L"Warnings");
        s.Add_Colon();
        s.Add_LF();
        AddNewLineString(s, GetOpenArcErrorMessage(warningFlags));
    }

    if (!er.WarningMessage.IsEmpty()) {
        s += GetNameOfProperty(kpidWarning, L"Warning");
        s += ": ";
        s += QtArchiveDiagnosticText(er.WarningMessage);
        s.Add_LF();
    }
}

static UString GetBracedType(const wchar_t *type) {
    UString s('[');
    s += type;
    s.Add_Char(']');
    return s;
}

void OpenResult_GUI(UString &s, const CCodecs *codecs, const CArchiveLink &arcLink,
                    const wchar_t *name, HRESULT result) {
    FOR_VECTOR(level, arcLink.Arcs) {
        const CArc &arc = arcLink.Arcs[level];
        const CArcErrorInfo &er = arc.ErrorInfo;

        if (!er.IsThereErrorOrWarning() && er.ErrorFormatIndex < 0)
            continue;

        if (s.IsEmpty()) {
            s += name;
            s.Add_LF();
        }

        if (level != 0) {
            AddNewLineString(s, arc.Path);
        }

        ErrorInfo_Print(s, er);

        if (er.ErrorFormatIndex >= 0) {
            AddNewLineString(s, GetNameOfProperty(kpidWarning, L"Warning"));
            if (arc.FormatIndex == er.ErrorFormatIndex) {
                AddNewLineString(s, LangString(IDS_IS_OPEN_WITH_OFFSET));
            } else {
                AddNewLineString(
                    s, MyFormatNew(IDS_CANT_OPEN_AS_TYPE,
                                   GetBracedType(codecs->GetFormatNamePtr(er.ErrorFormatIndex))));
                AddNewLineString(
                    s, MyFormatNew(IDS_IS_OPEN_AS_TYPE,
                                   GetBracedType(codecs->GetFormatNamePtr(arc.FormatIndex))));
            }
        }
    }

    if (arcLink.NonOpen_ErrorInfo.ErrorFormatIndex >= 0 || result != S_OK) {
        s += name;
        s.Add_LF();
        if (!arcLink.Arcs.IsEmpty())
            AddNewLineString(s, arcLink.NonOpen_ArcPath);

        if (arcLink.NonOpen_ErrorInfo.ErrorFormatIndex >= 0 || result == S_FALSE) {
            UINT id = IDS_CANT_OPEN_ARCHIVE;
            UString param;
            if (arcLink.PasswordWasAsked)
                id = IDS_CANT_OPEN_ENCRYPTED_ARCHIVE;
            else if (arcLink.NonOpen_ErrorInfo.ErrorFormatIndex >= 0) {
                id = IDS_CANT_OPEN_AS_TYPE;
                param = GetBracedType(
                    codecs->GetFormatNamePtr(arcLink.NonOpen_ErrorInfo.ErrorFormatIndex));
            }
            UString s2 = MyFormatNew(id, param);
            s2.Replace(L" ''", L"");
            s2.Replace(L"''", L"");
            s += s2;
        } else
            s += HResultToMessage(result);

        s.Add_LF();
        ErrorInfo_Print(s, arcLink.NonOpen_ErrorInfo);
    }

    if (!s.IsEmpty() && s.Back() == '\n')
        s.DeleteBack();
}
