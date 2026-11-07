// HashGUI.cpp

// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/IntToString.h"
#include "Common/StringConvert.h"

#include "Windows/ErrorMsg.h"

#include "7zip/UI/FileManager/OverwriteDialogRes.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include <QStringList>

#include "gui/HashGUI.h"

using namespace NWindows;

class CHashCallbackGUI Z7_final : public CProgressThreadVirt, public IHashCallbackUI {
    UInt64 NumFiles;
    bool _curIsFolder;
    UString FirstFileName;
    UString CurrentFileName;
    // UString MainPath;

    CPropNameValPairs PropNameValPairs;
    CPropNameValPairs CompletedFiles;

    HRESULT ProcessVirt() Z7_override;
    virtual void ProcessWasFinished_GuiVirt() Z7_override;

  public:
    bool HasErrors = false;
    const NWildcard::CCensor *censor;
    const CHashOptions *options;

    DECL_EXTERNAL_CODECS_LOC_VARS_DECL

    Z7_IFACE_IMP(IDirItemsCallback)
    Z7_IFACE_IMP(IHashCallbackUI)

    /*
    void AddErrorMessage(DWORD systemError, const wchar_t *name)
    {
      Sync.AddError_Code_Name(systemError, name);
    }
    */
    void AddErrorMessage(HRESULT systemError, const wchar_t *name) {
        Sync.AddError_Code_Name(systemError, name);
    }
};

void AddValuePair(CPropNameValPairs &pairs, UINT resourceID, UInt64 value) {
    CProperty &pair = pairs.AddNew();
    AddLangString(pair.Name, resourceID);
    char sz[32];
    ConvertUInt64ToString(value, sz);
    pair.Value = sz;
}

void AddSizeValuePair(CPropNameValPairs &pairs, UINT resourceID, UInt64 value) {
    CProperty &pair = pairs.AddNew();
    LangString(resourceID, pair.Name);
    AddSizeValue(pair.Value, value);
}

HRESULT CHashCallbackGUI::StartScanning() {
    CProgressSync &sync = Sync;
    sync.Set_Status(LangString(IDS_SCANNING));
    return CheckBreak();
}

HRESULT CHashCallbackGUI::ScanProgress(const CDirItemsStat &st, const FString &path, bool isDir) {
    return Sync.ScanProgress(st.NumFiles, st.GetTotalBytes(), path, isDir);
}

HRESULT CHashCallbackGUI::ScanError(const FString &path, DWORD systemError) {
    AddErrorMessage(HRESULT_FROM_WIN32(systemError), fs2us(path));
    return CheckBreak();
}

HRESULT CHashCallbackGUI::FinishScanning(const CDirItemsStat &st) {
    return ScanProgress(st, FString(), false); // isDir
}

HRESULT CHashCallbackGUI::CheckBreak() { return Sync.CheckStop(); }

HRESULT CHashCallbackGUI::SetNumFiles(UInt64 numFiles) {
    CProgressSync &sync = Sync;
    sync.Set_NumFilesTotal(numFiles);
    return CheckBreak();
}

HRESULT CHashCallbackGUI::SetTotal(UInt64 size) {
    CProgressSync &sync = Sync;
    sync.Set_NumBytesTotal(size);
    return CheckBreak();
}

HRESULT CHashCallbackGUI::SetCompleted(const UInt64 *completed) {
    return Sync.Set_NumBytesCur(completed);
}

HRESULT CHashCallbackGUI::BeforeFirstFile(const CHashBundle & /* hb */) { return S_OK; }

HRESULT CHashCallbackGUI::GetStream(const wchar_t *name, bool isFolder) {
    if (NumFiles == 0)
        FirstFileName = name;
    _curIsFolder = isFolder;
    CurrentFileName = name;
    CProgressSync &sync = Sync;
    sync.Set_FilePath(name, isFolder);
    return CheckBreak();
}

HRESULT CHashCallbackGUI::OpenFileError(const FString &path, DWORD systemError) {
    // if (systemError == ERROR_SHARING_VIOLATION)
    {
        AddErrorMessage(HRESULT_FROM_WIN32(systemError), fs2us(path));
        return S_FALSE;
    }
    // return systemError;
}

HRESULT CHashCallbackGUI::SetOperationResult(UInt64 /* fileSize */, const CHashBundle &hb,
                                             bool showHash) {
    CProgressSync &sync = Sync;
    if (!_curIsFolder) {
        NumFiles++;
        if (showHash)
            AddCompletedFileHashes(CompletedFiles, CurrentFileName, hb);
    }
    sync.Set_NumFilesCur(NumFiles);
    return CheckBreak();
}

static const unsigned k_DigestStringSize =
    k_HashCalc_DigestSize_Max * 2 + k_HashCalc_ExtraSize * 2 + 16;

static void AddHashString(CProperty &s, const CHasherState &h, unsigned digestIndex) {
    char temp[k_DigestStringSize];
    h.WriteToString(digestIndex, temp);
    s.Value = temp;
}

void AddCompletedFileHashes(CPropNameValPairs &rows, const UString &path, const CHashBundle &hash) {
    FOR_VECTOR(i, hash.Hashers) {
        const auto &hasher = hash.Hashers[i];
        auto &row = rows.AddNew();
        row.Name = path;
        row.Name += L" · ";
        row.Name += hasher.Name;
        AddHashString(row, hasher, k_HashCalc_Index_Current);
    }
}

static void AddHashResString(CPropNameValPairs &s, const CHasherState &h, unsigned digestIndex,
                             UInt32 resID) {
    CProperty &pair = s.AddNew();
    UString &s2 = pair.Name;
    LangString(resID, s2);
    UString name(h.Name);
    s2.Replace(L"CRC", name);
    s2.Replace(L":", L"");
    AddHashString(pair, h, digestIndex);
}

void AddHashBundleRes(CPropNameValPairs &s, const CHashBundle &hb) {
    if (hb.NumErrors != 0)
        AddValuePair(s, IDS_PROP_NUM_ERRORS, hb.NumErrors);

    if (hb.NumFiles == 1 && hb.NumDirs == 0 && !hb.FirstFileName.IsEmpty()) {
        CProperty &pair = s.AddNew();
        LangString(IDS_PROP_NAME, pair.Name);
        pair.Value = hb.FirstFileName;
    } else {
        if (!hb.MainName.IsEmpty()) {
            CProperty &pair = s.AddNew();
            LangString(IDS_PROP_NAME, pair.Name);
            pair.Value = hb.MainName;
        }
        if (hb.NumDirs != 0)
            AddValuePair(s, IDS_PROP_FOLDERS, hb.NumDirs);
        AddValuePair(s, IDS_PROP_FILES, hb.NumFiles);
    }

    AddSizeValuePair(s, IDS_PROP_SIZE, hb.FilesSize);

    if (hb.NumAltStreams != 0) {
        AddValuePair(s, IDS_PROP_NUM_ALT_STREAMS, hb.NumAltStreams);
        AddSizeValuePair(s, IDS_PROP_ALT_STREAMS_SIZE, hb.AltStreamsSize);
    }

    FOR_VECTOR(i, hb.Hashers) {
        const CHasherState &h = hb.Hashers[i];
        if (hb.NumFiles == 1 && hb.NumDirs == 0) {
            CProperty &pair = s.AddNew();
            pair.Name += h.Name;
            AddHashString(pair, h, k_HashCalc_Index_DataSum);
        } else {
            AddHashResString(s, h, k_HashCalc_Index_DataSum, IDS_CHECKSUM_CRC_DATA);
            AddHashResString(s, h, k_HashCalc_Index_NamesSum, IDS_CHECKSUM_CRC_DATA_NAMES);
        }
        if (hb.NumAltStreams != 0) {
            AddHashResString(s, h, k_HashCalc_Index_StreamsSum, IDS_CHECKSUM_CRC_STREAMS_NAMES);
        }
    }
}

void AddHashBundleRes(UString &s, const CHashBundle &hb) {
    CPropNameValPairs pairs;
    AddHashBundleRes(pairs, hb);

    FOR_VECTOR(i, pairs) {
        const CProperty &pair = pairs[i];
        s += pair.Name;
        s += ": ";
        s += pair.Value;
        s.Add_LF();
    }

    if (hb.NumErrors == 0 && hb.Hashers.IsEmpty()) {
        s.Add_LF();
        AddLangString(s, IDS_MESSAGE_NO_ERRORS);
        s.Add_LF();
    }
}

HRESULT CHashCallbackGUI::AfterLastFile(CHashBundle &hb) {
    HasErrors = hb.NumErrors != 0;
    hb.FirstFileName = FirstFileName;
    // MainPath
    AddHashBundleRes(PropNameValPairs, hb);
    if (hb.NumFiles > 1 || hb.NumDirs != 0 || HasErrors)
        PropNameValPairs += CompletedFiles;

    CProgressSync &sync = Sync;
    sync.Set_NumFilesCur(hb.NumFiles);

    // CProgressMessageBoxPair &pair = GetMessagePair(hb.NumErrors != 0);
    // pair.Message = s;
    // LangString(IDS_CHECKSUM_INFORMATION, pair.Title);

    return S_OK;
}

HRESULT CHashCallbackGUI::ProcessVirt() {
    NumFiles = 0;
    AString errorInfo;
    HRESULT res = HashCalc(EXTERNAL_CODECS_LOC_VARS * censor, *options, errorInfo, this);
    return res;
}

HRESULT HashCalcGUI(DECL_EXTERNAL_CODECS_LOC_VARS const NWildcard::CCensor &censor,
                    const CHashOptions &options, bool &messageWasDisplayed) {
    CHashCallbackGUI t;
#ifdef Z7_EXTERNAL_CODECS
    t._externalCodecs = _externalCodecs;
#endif
    t.censor = &censor;
    t.options = &options;

    t.ShowCompressionInfo = false;

    const UString title = LangString(IDS_CHECKSUM_CALCULATING);

    t.MainTitle = "7-Zip"; // LangString(IDS_APP_TITLE);
    t.MainAddTitle = title;
    t.MainAddTitle.Add_Space();

    RINOK(t.Create(title))
    messageWasDisplayed = t.ThreadFinishedOK && t.MessagesDisplayed;
    return t.Result == S_OK && t.HasErrors ? S_FALSE : t.Result;
}

void ShowHashResults(const CPropNameValPairs &propPairs, QWidget *hwnd, HRESULT status,
                     const UStringVector *messages) {
    QStringList lines;
    FOR_VECTOR(i, propPairs) {
        const CProperty &pair = propPairs[i];
        lines.append(QString::fromWCharArray(pair.Name) + QStringLiteral(": ") +
                     QString::fromWCharArray(pair.Value));
    }
    if (messages && !messages->IsEmpty()) {
        lines.append(QString());
        FOR_VECTOR(i, *messages)
        lines.append(QString::fromWCharArray((*messages)[i]));
    }
    const char *summary;
    if (status == S_OK)
        summary = QT_TRANSLATE_NOOP("QtTaskResultDialog", "Checksum calculation completed.");
    else if (status == E_ABORT)
        summary = QT_TRANSLATE_NOOP("QtTaskResultDialog", "Checksum calculation was cancelled.");
    else
        summary =
            QT_TRANSLATE_NOOP("QtTaskResultDialog", "Some checksums could not be calculated.");
    QtTaskResultDialog dialog(summary, lines.join('\n'), hwnd);
    dialog.setObjectName("checksumOperationResult");
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.exec();
}

void CHashCallbackGUI::ProcessWasFinished_GuiVirt() {
    if (Result == S_OK && !PropNameValPairs.IsEmpty())
        ShowHashResults(PropNameValPairs, this, HasErrors ? S_FALSE : S_OK, &Sync.Messages);
    else if (!CompletedFiles.IsEmpty())
        ShowHashResults(CompletedFiles, this, Result, &Sync.Messages);
}
