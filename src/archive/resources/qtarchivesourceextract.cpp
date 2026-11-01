// Retained native extraction, with a typed file-only metadata-service adapter.
#include "7zip/Archive/IArchive.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Compress/CopyCoder.h"
#include "7zip/IPassword.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivebackendrequest.h"
#include "archive/resources/qtarchivefileresource.h"
#include "platform/temp/qtownedtempdir.h"
#include <QFileInfo>
#include <QPointer>

namespace {
class CExtractStreamProgress final : public ICompressProgressInfo, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ICompressProgressInfo)
    IArchiveExtractCallback *_callback;
    UInt64 _offset;

  public:
    CExtractStreamProgress(IArchiveExtractCallback *callback, UInt64 offset)
        : _callback(callback), _offset(offset) {}
    Z7_COM7F_IMF(SetRatioInfo(const UInt64 *input, const UInt64 *)) override {
        if (!input)
            return _callback->SetCompleted(nullptr);
        const UInt64 completed = _offset + *input;
        return _callback->SetCompleted(&completed);
    }
};

HRESULT ExtractItems(ArchiveBackend &backend, const QString &archive, const QString &format,
                     IInArchive *metadata, const UInt32 *indices, UInt32 count, Int32 testMode,
                     IArchiveExtractCallback *callback, CProgressSync *sync, bool encrypted) {
    if (!metadata || !callback || (count != UInt32(-1) && count && !indices))
        return E_INVALIDARG;
    QString password;
    if (encrypted) {
        CMyComPtr<ICryptoGetTextPassword> crypto;
        RINOK(callback->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto))
        CMyComBSTR value;
        RINOK(crypto->CryptoGetTextPassword(&value))
        password = QString::fromWCharArray(value);
    }
    UInt32 total;
    RINOK(metadata->GetNumberOfItems(&total))
    if (count == UInt32(-1)) {
        count = total;
        indices = nullptr; // Native all-items contract ignores the supplied indices.
    }
    UInt64 unpacked = 0;
    for (UInt32 n = 0; n < count; ++n) {
        const UInt32 index = indices ? indices[n] : n;
        if (index >= total)
            return E_INVALIDARG;
        NWindows::NCOM::CPropVariant size;
        RINOK(metadata->GetProperty(index, kpidSize, &size))
        if (size.vt == VT_UI8) {
            if (size.uhVal.QuadPart > UInt64(-1) - unpacked)
                return E_INVALIDARG;
            unpacked += size.uhVal.QuadPart;
        }
    }
    RINOK(callback->SetTotal(unpacked))
    UInt64 completed = 0;
    QtOwnedTempDir staging;
    if (!staging.isValid())
        return E_FAIL;
    const Int32 mode =
        testMode ? NArchive::NExtract::NAskMode::kTest : NArchive::NExtract::NAskMode::kExtract;
    for (UInt32 n = 0; n < count; ++n) {
        const UInt32 index = indices ? indices[n] : n;
        RINOK(callback->SetCompleted(&completed))
        NWindows::NCOM::CPropVariant path, directory, size, altStream;
        RINOK(metadata->GetProperty(index, kpidPath, &path))
        RINOK(metadata->GetProperty(index, kpidIsDir, &directory))
        RINOK(metadata->GetProperty(index, kpidSize, &size))
        RINOK(metadata->GetProperty(index, kpidIsAltStream, &altStream))
        if (path.vt != VT_BSTR || directory.vt != VT_BOOL)
            return E_FAIL;
        const QString name = QString::fromWCharArray(path.bstrVal);
        // The file-only service selects names rather than archive indices.
        // Ambiguous duplicate entries require a retained reader/index transport.
        for (UInt32 other = 0; other < total; ++other) {
            if (other == index)
                continue;
            NWindows::NCOM::CPropVariant otherPath;
            RINOK(metadata->GetProperty(other, kpidPath, &otherPath))
            if (otherPath.vt == VT_BSTR && name == QString::fromWCharArray(otherPath.bstrVal))
                return E_NOTIMPL;
        }
        CMyComPtr<ISequentialOutStream> output;
        RINOK(callback->GetStream(index, &output, mode))
        if (!output && !testMode && directory.boolVal == VARIANT_FALSE) {
            if (size.vt == VT_UI8)
                completed += size.uhVal.QuadPart;
            continue; // Native overwrite/skip decision.
        }
        RINOK(callback->PrepareOperation(mode))
        HRESULT result = S_OK;
        Int32 operation = NArchive::NExtract::NOperationResult::kOK;
        if (directory.boolVal == VARIANT_FALSE) {
            const QString streamPath = staging.filePath(QString::number(n));
            result = RunArchiveBackendRequest(
                backend,
                [&] {
                    backend.extractSourceEntry(archive, name, streamPath, format, password,
                                               altStream.vt == VT_BOOL &&
                                                   altStream.boolVal != VARIANT_FALSE);
                },
                callback, sync);
            if (result == S_FALSE)
                operation = backend.lastSourceExtractOperation();
            if (operation < 0)
                operation = NArchive::NExtract::NOperationResult::kDataError;
            if (output &&
                (result == S_OK || (result == S_FALSE && QFileInfo::exists(streamPath)))) {
                const HRESULT decoded = result;
                CMyComPtr2<ISequentialInStream, CInFileStream> input;
                input.Create_if_Empty();
                if (!input->Open(us2fs(streamPath.toStdWString().c_str())))
                    result = GetLastError_noZero_HRESULT();
                else {
                    CMyComPtr<ICompressProgressInfo> progress =
                        new CExtractStreamProgress(callback, completed);
                    result = NCompress::CopyStream(input.Interface(), output, progress);
                    if (result != S_OK)
                        return result;
                    result = decoded;
                }
            }
        }
        output.Release();
        // Native engines stop extraction on E_ABORT without reporting a data
        // error for the interrupted item. Preserve that cancellation contract.
        if (result == E_ABORT)
            return E_ABORT;
        const HRESULT reported = callback->SetOperationResult(
            result == S_OK || result == S_FALSE ? operation
                                                : NArchive::NExtract::NOperationResult::kDataError);
        if (result != S_OK && result != S_FALSE)
            return result;
        RINOK(reported)
        if (size.vt == VT_UI8)
            completed += size.uhVal.QuadPart;
    }
    RINOK(callback->SetCompleted(&completed))
    return S_OK;
}
} // namespace

void QtArchiveFileResource::AttachExtractService(ArchiveBackend &backend, const QString &format) {
    AttachProgressService(backend);
    _extract = [this, backend = QPointer<ArchiveBackend>(&backend),
                format](IInArchive *metadata, bool nativeIndexes, const UInt32 *indices,
                        UInt32 count, Int32 testMode, IArchiveExtractCallback *callback) {
        COM_TRY_BEGIN
        if (nativeIndexes && HasNativeExtractService()) {
            const auto reader = NativeReaderLease();
            return ArchiveBackend::extractNativeArchive(reader, indices, count, testMode, callback);
        }
        if (HasStreamInput() || (_nativeOpen && (_nativeOpen->hasNativeRarExtractor() ||
                                                 _nativeOpen->hasNativeHashExtractor())))
            return E_FAIL;
        if (!backend)
            return E_FAIL;
        return ExtractItems(*backend, EnginePath, format, metadata, indices, count, testMode,
                            callback, _sync, Info.encrypted);
        COM_TRY_END
    };
}
