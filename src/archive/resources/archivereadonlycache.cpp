#include "7zip/Common/FileStreams.h"
#include "Common/StringConvert.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivereadonlyview_p.h"
#include "archive/resources/qtarchiveitemstream.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QScopeGuard>

bool ArchiveReadOnlyView::State::evict(quint64 except, quint64 needed) {
    while (Cached > Quota - needed || Active >= 64) {
        auto oldest = Items.end();
        for (auto i = Items.begin(); i != Items.end(); ++i)
            if (i.key() != except && i->Stream && (oldest == Items.end() || i->Used < oldest->Used))
                oldest = i;
        if (oldest == Items.end())
            return false;
        oldest->Stream.reset();
        --Active;
        if (!oldest->Path.isEmpty()) {
            if (!QFile::remove(oldest->Path))
                return false;
            oldest->Path.clear();
        }
        Cached -= oldest->Bytes;
        oldest->Bytes = 0;
        // Keep item identity, but count only retained streams below.
    }
    return true;
}

HRESULT ArchiveReadOnlyView::State::stream(quint64 id, ReadResult &result) {
    auto &item = Items[id];
    item.Used = ++Clock;
    if (item.Stream) {
        describe(item, result);
        return S_OK;
    }
    const auto request = readRequest();
    CMyComPtr<ISequentialInStream> sequential;
    const HRESULT streamed =
        CreateArchiveItemStream(Resource, Reader, Depth, item.Index, &sequential, request);
    if (streamed == E_ABORT)
        return E_ABORT;
    CMyComPtr<IInStream> seekable;
    if (streamed == S_OK && sequential)
        sequential.QueryInterface(IID_IInStream, &seekable);
    if (seekable) {
        if (!evict(id, 0))
            return E_OUTOFMEMORY;
        const auto opened = ArchiveInputLayer::Create(seekable, request).transform([&](auto layer) {
            item.Stream = std::move(layer);
            ++Active;
        });
        return opened ? S_OK : opened.error();
    }

    if (!Quota) {
        result.Error = QStringLiteral("The reader could not provide direct access to this entry.");
        return streamed != S_OK ? streamed : E_NOTIMPL;
    }
    return materialize(id, request, result);
}

HRESULT ArchiveReadOnlyView::State::materialize(quint64 id, const CMyComPtr<IProgress> &request,
                                                ReadResult &result) {
    auto &item = Items[id];
    const quint64 size = Entries.at(qsizetype(id - 1)).Size;
    if (size > Quota) {
        result.Error = QStringLiteral("This entry requires decoding beyond the mount cache limit.");
        return E_OUTOFMEMORY;
    }
    QString name;
    for (quint64 current = id; current > 1;) {
        const auto &entry = Entries.at(qsizetype(current - 1));
        name = name.isEmpty() ? entry.Name : entry.Name + u'/' + name;
        current = entry.Parent;
    }
    // No temporary output or decoding starts without explicit consent.
    if (!ExtractionConsent || !ExtractionConsent(name, size)) {
        result.Error =
            QStringLiteral("Extracting this entry into the mount cache requires confirmation.");
        return E_ABORT;
    }
    if (Cancelled)
        return E_ABORT;
    if (!evict(id, size)) {
        result.Error = QStringLiteral("Could not release space in the mount cache.");
        return E_OUTOFMEMORY;
    }
    if (!Directory)
        Directory = std::make_unique<QTemporaryDir>();
    if (!Directory->isValid()) {
        result.Error = Directory->errorString();
        return E_FAIL;
    }
    const QString path = Directory->filePath(QString::number(id));
    QtArchiveReadOperation operation(Reader, Password);
    operation.Output.NativeIndex = item.Index;
    operation.Output.Path = path;
    operation.Output.Limit = size;
    {
        const auto clearOperation = qScopeGuard([this] {
            QMutexLocker lock(&OperationMutex);
            Operation = nullptr;
        });
        {
            QMutexLocker lock(&OperationMutex);
            Operation = &operation;
            if (Cancelled)
                operation.cancel();
        }
        operation.start();
        operation.wait();
    }
    result.Operation = operation.OutputOperation;
    const qint64 actual = QFileInfo(path).size();
    const bool complete = operation.Result == S_OK && operation.OutputClosed && actual >= 0 &&
                          quint64(actual) == size;
    using namespace NArchive::NExtract::NOperationResult;
    const bool recoverable =
        !item.Encrypted && operation.Result != E_ABORT && operation.OutputClosed && actual > 0 &&
        quint64(actual) <= size &&
        (operation.OutputOperation == kDataError || operation.OutputOperation == kCRCError ||
         operation.OutputOperation == kUnexpectedEnd || operation.OutputOperation == kDataAfterEnd);
    const bool recovered = !complete && recoverable && RecoveryConsent &&
                           RecoveryConsent(name, quint64(actual), operation.OutputOperation);
    if ((!complete && !recovered) || Cancelled) {
        QFile::remove(path);
        result.Error = QStringLiteral("The entry could not be decoded completely and verified.");
        return Cancelled ? E_ABORT : operation.Result == S_OK ? E_FAIL : operation.Result;
    }
    CMyComPtr2<IInStream, CInFileStream> file;
    file.Create_if_Empty();
    if (!file->Open(us2fs(path.toStdWString().c_str()))) {
        QFile::remove(path);
        return E_FAIL;
    }
    auto opened = ArchiveInputLayer::Create(file.Interface(), request);
    if (!opened) {
        file.Detach()->Release();
        QFile::remove(path);
        return opened.error();
    }
    item.Stream = std::move(*opened);
    item.Path = path;
    item.Bytes = quint64(actual);
    item.Operation = operation.OutputOperation;
    describe(item, result);
    Cached += item.Bytes;
    ++Active;
    return S_OK;
}

void ArchiveReadOnlyView::State::describe(const Item &item, ReadResult &result) {
    result.Operation = item.Operation;
    if (item.Operation > 0)
        result.Error = QStringLiteral("Unverified or incomplete extracted data.");
}
