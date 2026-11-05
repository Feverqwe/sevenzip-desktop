#include "archive/resources/qtarchivehashoperation.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include <QFileInfo>

QtArchiveHashOperation::QtArchiveHashOperation(QStringList paths, CHashOptions options,
                                               std::shared_ptr<const ArchiveCodecCatalog> catalog,
                                               std::unique_ptr<const CExternalCodecs> providers)
    : catalog_(std::move(catalog)), providers_(std::move(providers)), paths_(std::move(paths)),
      options_(std::move(options)) {}

void QtArchiveHashOperation::cancel() { progress_.cancel(); }
void QtArchiveHashOperation::setPaused(bool paused) { progress_.setPaused(paused); }
HRESULT QtArchiveHashOperation::CheckBreak() { return progress_.checkpoint(); }
HRESULT QtArchiveHashOperation::StartScanning() { return CheckBreak(); }
HRESULT QtArchiveHashOperation::ScanProgress(const CDirItemsStat &st, const FString &path, bool) {
    if (Progress)
        Progress(0, st.GetTotalBytes(), QString::fromWCharArray(fs2us(path).Ptr()));
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::ScanError(const FString &path, DWORD error) {
    Errors.append(QString::fromWCharArray(fs2us(path).Ptr()) + ": " +
                  QString::fromWCharArray(NWindows::NError::MyFormatMessage(error).Ptr()));
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::FinishScanning(const CDirItemsStat &st) {
    return ScanProgress(st, FString(), false);
}
HRESULT QtArchiveHashOperation::SetNumFiles(UInt64) { return CheckBreak(); }
HRESULT QtArchiveHashOperation::SetTotal(UInt64 size) {
    total_ = size;
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::SetCompleted(const UInt64 *completed) {
    if (Progress)
        Progress(completed ? *completed : 0, total_, currentPath_);
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::BeforeFirstFile(const CHashBundle &) { return CheckBreak(); }
HRESULT QtArchiveHashOperation::GetStream(const wchar_t *name, bool isFolder) {
    if (files_ == 0)
        firstFileName_ = name;
    currentIsFolder_ = isFolder;
    currentPath_ = QString::fromWCharArray(name);
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::OpenFileError(const FString &path, DWORD error) {
    const HRESULT result = ScanError(path, error);
    return result == S_OK ? S_FALSE : result;
}
HRESULT QtArchiveHashOperation::SetOperationResult(UInt64, const CHashBundle &hb, bool showHash) {
    if (!currentIsFolder_)
        files_++;
    if (showHash) {
        FOR_VECTOR(i, hb.Hashers) {
            const CHasherState &hasher = hb.Hashers[i];
            char digest[k_HashCalc_DigestSize_Max * 2 + 1];
            hasher.WriteToString(k_HashCalc_Index_Current, digest);
            Output.append(QString::fromLatin1(hasher.Name.Ptr()) + ": " +
                          QString::fromLatin1(digest) + "  " + currentPath_);
        }
    }
    return CheckBreak();
}
HRESULT QtArchiveHashOperation::AfterLastFile(CHashBundle &hb) {
    hb.FirstFileName = firstFileName_;
    Bundle = hb;
    return CheckBreak();
}
void QtArchiveHashOperation::run() {
    try {
        Result = CheckBreak();
        if (Result != S_OK)
            return;
        NWildcard::CCensor censor;
        NWildcard::CCensorPathProps properties;
        properties.WildcardMatching = false;
        for (const QString &path : paths_)
            censor.AddItem(NWildcard::k_RelatPath, true,
                           QFileInfo(path).absoluteFilePath().toStdWString().c_str(), properties);
        // Enumerate links as link objects; never traverse a cyclic directory.
        options_.SymLinks.SetVal_as_Defined(true);
        AString error;
        Result = HashCalc(providers_.get(), censor, options_, error, this);
        if (!error.IsEmpty())
            Errors.append(QString::fromLatin1(error.Ptr()));
    } catch (...) {
        Result = E_OUTOFMEMORY;
    }
}
