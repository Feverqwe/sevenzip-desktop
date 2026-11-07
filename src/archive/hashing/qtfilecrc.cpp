// Native filesystem checksum worker, extracted from PanelCrc.cpp.

// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/MyException.h"

#include "Windows/FileFind.h"
#include "Windows/FileIO.h"
#include "Windows/FileName.h"

#include "7zip/Common/FileStreams.h"
#include "archive/codecs/LoadCodecs.h"

#include "gui/HashGUI.h"

#include "archive/hashing/qtfilecrc.h"
#include "common/memory/midallocbuffer.h"
#include "localization/LangUtils.h"
#include "ui/progress/ProgressDialog2.h"

#include "7zip/UI/FileManager/resource.h"

using namespace NWindows;
using namespace NFile;

static const UInt32 kBufSize = (1 << 15);

#ifdef _WIN32
using QtCrcFile = CInFileStream;
#else
using QtCrcFile = NIO::CInFile;
#endif

static HRESULT QtCrcRead(QtCrcFile &file, void *data, UInt32 size, UInt32 &processed) {
#ifdef _WIN32
    // The source stream supplies bounded, aligned device reads and ordinary file I/O.
    return static_cast<ISequentialInStream &>(file).Read(data, size, &processed);
#else
    size_t read = 0;
    const bool result = file.ReadFull(data, size, read);
    processed = (UInt32)read;
    return result ? S_OK : GetLastError_noZero_HRESULT();
#endif
}

struct CDirEnumerator {
    bool EnterToDirs;
    FString BasePrefix;
    FString BasePrefix_for_Open;
    FStringVector FilePaths;

    struct CDirectoryFrame {
        NFind::CEnumerator Enumerator;
        FString Prefix;
    };
    CObjectVector<CDirectoryFrame> Frames;
    unsigned Index;

    CDirEnumerator() : EnterToDirs(false), Index(0) {}

    void Init();
    DWORD GetNextFile(NFind::CFileInfo &fi, bool &filled, FString &resPath);
};

void CDirEnumerator::Init() {
    Frames.Clear();
    Index = 0;
}

static DWORD GetNormalizedError() {
    const DWORD error = GetLastError();
    return (error == 0) ? (DWORD)E_FAIL : error;
}

DWORD CDirEnumerator::GetNextFile(NFind::CFileInfo &fi, bool &filled, FString &resPath) {
    filled = false;
    resPath.Empty();

    for (;;) {
#if defined(_WIN32) && !defined(UNDER_CE)
        bool isRootPrefix =
            (BasePrefix.IsEmpty() ||
             (NName::IsSuperPath(BasePrefix) && BasePrefix[NName::kSuperPathPrefixSize] == 0));
#endif

        if (Frames.IsEmpty()) {
            if (Index >= FilePaths.Size())
                return S_OK;
            const FString &path = FilePaths[Index++];
            const int pos = path.ReverseFind_PathSepar();
            if (pos >= 0)
                resPath.SetFrom(path, (unsigned)pos + 1);

#if defined(_WIN32) && !defined(UNDER_CE)
            if (isRootPrefix && path.Len() == 2 && NName::IsDrivePath2(path)) {
                // we use "c:" item as directory item
                fi.ClearBase();
                fi.Name = path;
                fi.SetAsDir();
                fi.Size = 0;
            } else
#endif
                if (!fi.Find(BasePrefix + path)) {
                const DWORD error = GetNormalizedError();
                resPath = path;
                return error;
            }

            break;
        }

        bool found;
        auto &frame = Frames.Back();

#ifdef _WIN32
        const bool nextOK = frame.Enumerator.Next(fi, found);
#else
        NFind::CDirEntry entry;
        bool nextOK = frame.Enumerator.Next(entry, found);
        if (nextOK && found)
            nextOK = frame.Enumerator.Fill_FileInfo(entry, fi, false);
#endif
        if (nextOK) {
            if (found) {
                resPath = frame.Prefix;
                break;
            }
        } else {
            const DWORD error = GetNormalizedError();
            resPath = frame.Prefix;
            Frames.DeleteBack();
            return error;
        }

        Frames.DeleteBack();
    }

    resPath += fi.Name;

    // Keep the existing port's filesystem link boundary, including directory
    // reparse cycles. The original enumerator remains authoritative otherwise.
    if (fi.IsOsSymLink())
        return (DWORD)E_NOTIMPL;

    if (EnterToDirs && fi.IsDir()) {
        FString s = resPath;
        s.Add_PathSepar();
        auto &frame = Frames.AddNew();
        frame.Prefix = s;
        frame.Enumerator.SetDirPrefix(BasePrefix + s);
    }

    filled = true;
    return S_OK;
}

class CThreadCrc : public CProgressThreadVirt {
    bool ResultsShown = false;

    HRESULT ProcessVirt() Z7_override;
    virtual void ProcessWasFinished_GuiVirt() Z7_override;

  public:
    CDirEnumerator Enumerator;
    CHashBundle Hash;
    CPropNameValPairs CompletedFiles;
    std::function<bool(const CHashBundle &)> PresentResult;
    // FString FirstFilePath;

    void SetStatus(const UString &s);
    void AddErrorMessage(DWORD systemError, const FChar *name);
    void ShowFinalResults(QWidget *hwnd);
};

void CThreadCrc::ShowFinalResults(QWidget *hwnd) {
    if (ResultsShown || (Result != S_OK && CompletedFiles.IsEmpty()))
        return;
    ResultsShown = true;
    if (Result == S_OK && PresentResult && PresentResult(Hash))
        return;
    CPropNameValPairs report;
    if (Result == S_OK)
        AddHashBundleRes(report, Hash);
    if (Result != S_OK || Hash.NumFiles > 1 || Hash.NumDirs != 0 || Hash.NumErrors != 0)
        report += CompletedFiles;
    ShowHashResults(report, hwnd, Result == S_OK && Hash.NumErrors ? S_FALSE : Result,
                    &Sync.Messages);
}

void CThreadCrc::ProcessWasFinished_GuiVirt() { ShowFinalResults(this); }

void CThreadCrc::AddErrorMessage(DWORD systemError, const FChar *name) {
    Sync.AddError_Code_Name(HRESULT_FROM_WIN32(systemError), fs2us(Enumerator.BasePrefix + name));
    Hash.NumErrors++;
}

void CThreadCrc::SetStatus(const UString &s2) {
    UString s = s2;
    if (!Enumerator.BasePrefix.IsEmpty()) {
        s.Add_Space_if_NotEmpty();
        s += fs2us(Enumerator.BasePrefix);
    }
    Sync.Set_Status(s);
}

HRESULT CThreadCrc::ProcessVirt() {
    // Hash.Init();

    CMyBuffer buf;
    if (!buf.Allocate(kBufSize))
        return E_OUTOFMEMORY;

    CProgressSync &sync = Sync;

    SetStatus(LangString(IDS_SCANNING));

    Enumerator.Init();

    FString path;
    NFind::CFileInfo fi;
    UInt64 numFiles = 0;
    UInt64 numItems = 0, numItems_Prev = 0;
    UInt64 totalSize = 0;

    for (;;) {
        bool filled;
        const DWORD error = Enumerator.GetNextFile(fi, filled, path);
        if (error != 0) {
            AddErrorMessage(error, path);
            continue;
        }
        if (!filled)
            break;
        if (!fi.IsDir()) {
            totalSize += fi.Size;
            numFiles++;
        }
        numItems++;
        bool needPrint = false;
        // if (fi.IsDir())
        {
            if (numItems - numItems_Prev >= 100) {
                needPrint = true;
                numItems_Prev = numItems;
            }
        }
        /*
        else if (numFiles - numFiles_Prev >= 200)
        {
          needPrint = true;
          numFiles_Prev = numFiles;
        }
        */
        if (needPrint) {
            RINOK(sync.ScanProgress(numFiles, totalSize, path, fi.IsDir()))
        }
    }
    RINOK(sync.ScanProgress(numFiles, totalSize, FString(), false))
    // sync.SetNumFilesTotal(numFiles);
    // sync.SetProgress(totalSize, 0);
    // SetStatus(LangString(IDS_CHECKSUM_CALCULATING));
    // sync.SetCurFilePath(L"");
    SetStatus(L"");

    Enumerator.Init();

    FString tempPath;
    bool isFirstFile = true;
    UInt64 errorsFilesSize = 0;

    for (;;) {
        bool filled;
        DWORD error = Enumerator.GetNextFile(fi, filled, path);
        if (error != 0) {
            AddErrorMessage(error, path);
            continue;
        }
        if (!filled)
            break;

        error = 0;
        Hash.InitForNewFile();
        if (!fi.IsDir()) {
            QtCrcFile inFile;
            tempPath = Enumerator.BasePrefix_for_Open;
            tempPath += path;
            if (!inFile.Open(tempPath)) {
                error = GetNormalizedError();
                AddErrorMessage(error, path);
                continue;
            }
            if (isFirstFile) {
                Hash.FirstFileName = fs2us(path);
                isFirstFile = false;
            }
            sync.Set_FilePath(fs2us(path));
            sync.Set_NumFilesCur(Hash.NumFiles);
            UInt64 progress_Prev = 0;
            for (;;) {
                UInt32 size;
                const HRESULT readResult = QtCrcRead(inFile, buf, kBufSize, size);
                if (readResult != S_OK) {
                    error = static_cast<DWORD>(readResult);
                    AddErrorMessage(error, path);
                    UInt64 errorSize = 0;
                    if (inFile.GetLength(errorSize))
                        errorsFilesSize += errorSize;
                    break;
                }
                if (size == 0)
                    break;
                Hash.Update(buf, size);
                if (Hash.CurSize - progress_Prev >= ((UInt32)1 << 21)) {
                    RINOK(sync.Set_NumBytesCur(errorsFilesSize + Hash.FilesSize + Hash.CurSize))
                    progress_Prev = Hash.CurSize;
                }
            }
        }
        if (error == 0) {
            Hash.Final(fi.IsDir(), false, fs2us(path));
            if (!fi.IsDir())
                AddCompletedFileHashes(CompletedFiles, fs2us(path), Hash);
        }
        RINOK(sync.Set_NumBytesCur(errorsFilesSize + Hash.FilesSize))
    }
    RINOK(sync.Set_NumBytesCur(errorsFilesSize + Hash.FilesSize))
    sync.Set_NumFilesCur(Hash.NumFiles);
    if (Hash.NumFiles != 1)
        sync.Set_FilePath(L"");
    SetStatus(L"");

    CProgressMessageBoxPair &pair = GetMessagePair(Hash.NumErrors != 0);
    LangString(IDS_CHECKSUM_INFORMATION, pair.Title);
    return S_OK;
}

HRESULT CalculateFileCrc(const UString &methodName, const CExternalCodecs *externalCodecs,
                         const UString &basePrefix, const UStringVector &paths, bool enterToDirs,
                         QWidget *parent, bool *messageWasDisplayed,
                         const std::function<bool(const CHashBundle &)> &presentResult) {
    if (messageWasDisplayed)
        *messageWasDisplayed = false;
    {
        CThreadCrc t;
        t.PresentResult = presentResult;

        {
            UStringVector methods;
            methods.Add(methodName);
            RINOK(t.Hash.SetMethods(externalCodecs, methods))
        }

        FOR_VECTOR(i, paths)
        t.Enumerator.FilePaths.Add(us2fs(paths[i]));

        if (t.Enumerator.FilePaths.Size() == 1)
            t.Hash.MainName = fs2us(t.Enumerator.FilePaths[0]);

        UString basePrefix2 = basePrefix;
        if (!basePrefix2.IsEmpty() && basePrefix2.Back() == ':') {
            const int pos = basePrefix2.ReverseFind_PathSepar();
            if (pos >= 0)
                basePrefix2.DeleteFrom((unsigned)(pos + 1));
        }

        t.Enumerator.BasePrefix = us2fs(basePrefix);
        t.Enumerator.BasePrefix_for_Open = us2fs(basePrefix2);

        t.Enumerator.EnterToDirs = enterToDirs;

        t.ShowCompressionInfo = false;

        const UString title = LangString(IDS_CHECKSUM_CALCULATING);

        t.MainWindow = parent;
        t.MainTitle = "7-Zip"; // LangString(IDS_APP_TITLE);
        t.MainAddTitle = title;
        t.MainAddTitle.Add_Space();

        RINOK(t.Create(title, parent))
        if (messageWasDisplayed)
            *messageWasDisplayed = t.ThreadFinishedOK && t.MessagesDisplayed;

        t.ShowFinalResults(parent);
        if (t.Result != S_OK)
            return t.Result;
        return t.Hash.NumErrors ? S_FALSE : S_OK;
    }
}
