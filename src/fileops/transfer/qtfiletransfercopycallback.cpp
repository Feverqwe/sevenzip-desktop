#include "fileops/transfer/qtfiletransfercopycallback.h"
#include "Common/StringConvert.h"

CFileTransferCopyCallback::CFileTransferCopyCallback(
    Decide decide, std::function<bool(quint64)> advance,
    std::function<void(const QString &)> currentFile)
    : decide_(std::move(decide)), advance_(std::move(advance)),
      currentFile_(std::move(currentFile)) {}

Z7_COM7F_IMF(CFileTransferCopyCallback::SetTotal(UInt64)) { return S_OK; }

Z7_COM7F_IMF(CFileTransferCopyCallback::SetCompleted(const UInt64 *completed)) {
    const quint64 value = completed ? *completed : previous_;
    const quint64 delta = value >= previous_ ? value - previous_ : 0;
    previous_ = value;
    return !advance_ || advance_(delta) ? S_OK : E_ABORT;
}

Z7_COM7F_IMF(CFileTransferCopyCallback::SetNumFiles(UInt64)) { return S_OK; }

Z7_COM7F_IMF(CFileTransferCopyCallback::SetCurrentFilePath(const wchar_t *path)) {
    if (currentFile_)
        currentFile_(QString::fromWCharArray(path));
    return S_OK;
}

Z7_COM7F_IMF(CFileTransferCopyCallback::ShowMessage(const wchar_t *message)) {
    errorMessage = QString::fromWCharArray(message);
    return S_OK;
}

Z7_COM7F_IMF(CFileTransferCopyCallback::AskWrite(const wchar_t *srcPath, Int32 srcIsFolder,
                                                 const FILETIME *srcTime, const UInt64 *srcSize,
                                                 const wchar_t *destPath, BSTR *destPathResult,
                                                 Int32 *writeAnswer)) {
    if (srcIsFolder || !destPathResult || !writeAnswer)
        return E_INVALIDARG;
    *destPathResult = nullptr;
    *writeAnswer = 0;
    const auto source =
        NativeOverwriteFile(QString::fromWCharArray(srcPath), srcTime, srcSize, true);
    QString effective = QString::fromWCharArray(destPath);
    bool write = false;
    const HRESULT result = decide_(source, effective, effective, write);
    if (result != S_OK)
        return result;
    if (!write)
        return S_OK;
    *writeAnswer = 1;
    writtenSources << source.Path;
    const std::wstring wide = effective.toStdWString();
    return StringToBstr(wide.c_str(), destPathResult);
}
