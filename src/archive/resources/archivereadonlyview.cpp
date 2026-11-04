#include "Common/ComTry.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivereadonlyview_p.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveitemstream.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QCoreApplication>
#include <QMutexLocker>
#include <QThread>
#include <algorithm>
#include <limits>
#include <new>

namespace {
class ReadRequest final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    const std::atomic_bool &cancelled_;

  public:
    explicit ReadRequest(const std::atomic_bool &cancelled) : cancelled_(cancelled) {}
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return cancelled_ ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return cancelled_ ? E_ABORT : S_OK; }
};
} // namespace

ArchiveReadOnlyView::State::State(
    IUnknown *resource, quint64 quota, QString password,
    std::function<bool(const QString &, quint64)> extractionConsent,
    std::function<bool(const QString &, quint64, Int32)> recoveryConsent, const QString &folder)
    : Resource(resource), Quota(quota), Password(std::move(password)),
      ExtractionConsent(std::move(extractionConsent)), RecoveryConsent(std::move(recoveryConsent)) {
    FolderPrefix = folder;
    FolderPrefix.replace(u'\\', u'/');
    if (!FolderPrefix.isEmpty() && !FolderPrefix.endsWith(u'/'))
        FolderPrefix += u'/';
    if (const auto *owner = dynamic_cast<QtArchiveFileResource *>(resource)) {
        Reader = owner->NativeReaderLease();
        Depth = owner->ContainerDepth();
        Limitations = owner->FilesystemReadLimitations();
        if (Limitations.isEmpty() && owner->AllowsIncompleteDeviceRead())
            Limitations = QCoreApplication::translate(
                "ArchiveReadOnlyView", "Limited access was allowed for this device. Files may be "
                                       "missing, outdated or unreadable.");
    }
}

CMyComPtr<IProgress> ArchiveReadOnlyView::State::readRequest() const {
    return new ReadRequest(Cancelled);
}

ArchiveReadOnlyView::ArchiveReadOnlyView(
    IUnknown *resource, quint64 quota, QString password,
    std::function<bool(const QString &, quint64)> extractionConsent,
    std::function<bool(const QString &, quint64, Int32)> recoveryConsent, const QString &folder)
    : state_(std::make_unique<State>(resource, quota, std::move(password),
                                     std::move(extractionConsent), std::move(recoveryConsent),
                                     folder)) {}
ArchiveReadOnlyView::~ArchiveReadOnlyView() {
    // Close cached file handles before QTemporaryDir removes its files.
    state_->Items.clear();
}
const QList<ArchiveReadOnlyView::Entry> &ArchiveReadOnlyView::entries() const {
    return state_->Entries;
}
QString ArchiveReadOnlyView::limitations() const { return state_->Limitations; }
const QStringList &ArchiveReadOnlyView::skippedEntries() const { return state_->Skipped; }
void ArchiveReadOnlyView::setReadOptions(
    quint64 quota, QString password,
    std::function<bool(const QString &, quint64)> extractionConsent,
    std::function<bool(const QString &, quint64, Int32)> recoveryConsent) {
    auto &state = *state_;
    Q_ASSERT(!state.Active && !state.Directory);
    state.Quota = quota;
    state.Password = std::move(password);
    state.ExtractionConsent = std::move(extractionConsent);
    state.RecoveryConsent = std::move(recoveryConsent);
}
HRESULT ArchiveReadOnlyView::needsCache(bool &required) {
    COM_TRY_BEGIN
    required = false;
    auto &state = *state_;
    if (!state.Ready)
        return E_FAIL;
    const auto request = state.readRequest();
    for (auto item = state.Items.cbegin(); item != state.Items.cend(); ++item) {
        if (state.Cancelled || QThread::currentThread()->isInterruptionRequested())
            return E_ABORT;
        if (!state.Entries.at(qsizetype(item.key() - 1)).Size)
            continue;
        CMyComPtr<ISequentialInStream> stream;
        const HRESULT result = CreateArchiveItemStream(state.Resource, state.Reader, state.Depth,
                                                       item->Index, &stream, request);
        if (result == E_ABORT || result == E_OUTOFMEMORY)
            return result;
        CMyComPtr<IInStream> seekable;
        if (result == S_OK && stream)
            stream.QueryInterface(IID_IInStream, &seekable);
        if (!seekable) {
            required = true;
            break;
        }
    }
    return state.Cancelled ? E_ABORT : S_OK;
    COM_TRY_END
}
void ArchiveReadOnlyView::cancel() {
    state_->Cancelled = true;
    QMutexLocker lock(&state_->OperationMutex);
    if (state_->Operation)
        state_->Operation->cancel();
}

ArchiveReadOnlyView::ReadResult ArchiveReadOnlyView::read(quint64 id, quint64 offset,
                                                          quint32 length) try {
    ReadResult result;
    auto &state = *state_;
    if (state.Cancelled) {
        result.Result = E_ABORT;
        return result;
    }
    if (!state.Items.contains(id) || length > 1024 * 1024 ||
        offset > quint64(std::numeric_limits<qint64>::max())) {
        result.Result = E_INVALIDARG;
        return result;
    }
    const auto size = state.Entries.at(qsizetype(id - 1)).Size;
    if (offset >= size || length == 0) {
        result.Result = S_OK;
        return result;
    }
    result.Result = state.stream(id, result);
    if (result.Result != S_OK)
        return result;
    result.Data.resize(qsizetype(std::min<quint64>(length, size - offset)));
    const auto request = state.readRequest();
    UInt32 actual = 0;
    // Fill the bounded network range across native encryption/compression
    // blocks; a short internal read is not the end of the requested SMB range.
    while (actual < quint64(result.Data.size())) {
        UInt32 count = 0;
        const UInt32 remaining = UInt32(result.Data.size()) - actual;
        result.Result = state.Items[id].Stream->ReadAt(offset + actual, result.Data.data() + actual,
                                                       remaining, &count, request);
        if (result.Result != S_OK) {
            result.Data.clear();
            return result;
        }
        if (count > remaining) {
            result.Data.clear();
            result.Result = E_FAIL;
            result.Error = QStringLiteral("The reader returned an invalid byte count.");
            return result;
        }
        if (!count)
            break;
        actual += count;
    }
    result.Data.resize(actual);
    if (!actual) {
        result.Result = E_FAIL;
        result.Error = QStringLiteral("The source ended before the declared entry size.");
    }
    return result;
} catch (const std::bad_alloc &) {
    return {E_OUTOFMEMORY, -1, {}, QStringLiteral("Insufficient memory for the requested read.")};
} catch (...) {
    return {E_FAIL, -1, {}, QStringLiteral("The reader could not complete the requested read.")};
}

HRESULT ArchiveBackend::createReadOnlyView(
    IUnknown *resource, quint64 cacheLimit, const QString &password,
    std::shared_ptr<ArchiveReadOnlyView> &view, QString &error,
    std::function<bool(const QString &, quint64)> extractionConsent,
    std::function<bool(const QString &, quint64, Int32)> recoveryConsent, const QString &folder) {
    COM_TRY_BEGIN
    view.reset();
    auto opened = retainReadOnlyView(resource, cacheLimit, password, std::move(extractionConsent),
                                     std::move(recoveryConsent), folder);
    const HRESULT result = prepareReadOnlyView(opened, error);
    if (result == S_OK)
        view = std::move(opened);
    return result;
    COM_TRY_END
}

std::shared_ptr<ArchiveReadOnlyView> ArchiveBackend::retainReadOnlyView(
    IUnknown *resource, quint64 cacheLimit, const QString &password,
    std::function<bool(const QString &, quint64)> extractionConsent,
    std::function<bool(const QString &, quint64, Int32)> recoveryConsent, const QString &folder) {
    return std::shared_ptr<ArchiveReadOnlyView>(
        new ArchiveReadOnlyView(resource, cacheLimit, password, std::move(extractionConsent),
                                std::move(recoveryConsent), folder));
}
HRESULT ArchiveBackend::prepareReadOnlyView(const std::shared_ptr<ArchiveReadOnlyView> &view,
                                            QString &error) {
    COM_TRY_BEGIN
    return view ? view->initialize(error) : E_INVALIDARG;
    COM_TRY_END
}
