#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview_p.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveitemstream.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QCoreApplication>
#include <QDebug>
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
QString Key(const QString &path) { return path.normalized(QString::NormalizationForm_C); }
bool SafeComponent(const QString &name) {
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
           name.size() <= 255 && !name.contains(QChar::Null) && !name.contains(u':') &&
           !name.endsWith(u'.') && !name.endsWith(u' ') && QString::fromUtf8(name.toUtf8()) == name;
}
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

void ArchiveReadOnlyView::State::skip(const QString &path, const QString &reason) {
    const QString message =
        QCoreApplication::translate("ArchiveReadOnlyView", "Skipped %1: %2").arg(path, reason);
    Skipped.append(message);
    qInfo().noquote() << message;
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

HRESULT ArchiveReadOnlyView::initialize(QString &error) {
    auto &state = *state_;
    if (state.Prepared)
        return state.Ready ? S_OK : E_FAIL;
    state.Prepared = true;
    if (!state.Reader) {
        error = QStringLiteral("This container has no retained native reader.");
        return E_NOINTERFACE;
    }
    const auto request = state.readRequest();
    ArchiveNativeReadScope scope(state.Reader, request);
    state.Entries.append({1, 0, {}, 0, true});
    state.Paths.insert({}, 1);
    UInt32 count = 0;
    RINOK(state.Reader->arc().Archive->GetNumberOfItems(&count))
    for (UInt32 index = 0; index < count; ++index) {
        if (state.Cancelled || QThread::currentThread()->isInterruptionRequested())
            return E_ABORT;
        CReadArcItem native;
        const HRESULT itemResult = state.Reader->arc().GetItem(index, native);
        if (itemResult != S_OK) {
            if (itemResult == E_ABORT || itemResult == E_OUTOFMEMORY)
                return itemResult;
            state.skip(
                QStringLiteral("#%1").arg(index),
                QCoreApplication::translate("ArchiveReadOnlyView", "Cannot read entry metadata."));
            continue;
        }
        QString path = QString::fromWCharArray(native.Path.Ptr(), native.Path.Len());
        path.replace(u'\\', u'/');
        bool alternate = false;
        const HRESULT streamResult =
            Archive_IsItem_AltStream(state.Reader->arc().Archive, index, alternate);
        if (!state.FolderPrefix.isEmpty()) {
            const QString root = state.FolderPrefix.chopped(1);
            if (path == root || (native.IsDir && path == state.FolderPrefix))
                continue;
            if (!path.startsWith(state.FolderPrefix) &&
                !(alternate && path.startsWith(root + u':')))
                continue;
        }
        const QString originalPath = path;
        if (streamResult != S_OK) {
            if (streamResult == E_ABORT || streamResult == E_OUTOFMEMORY)
                return streamResult;
            state.skip(path, QCoreApplication::translate("ArchiveReadOnlyView",
                                                         "Cannot read entry metadata."));
            continue;
        }
        if (alternate) {
            state.skip(path,
                       QCoreApplication::translate(
                           "ArchiveReadOnlyView",
                           "Alternate streams and extended attributes are not exposed by SMB."));
            continue;
        }
        path.remove(0, state.FolderPrefix.size());
        NWindows::NCOM::CPropVariant time;
        const bool hasTime =
            state.Reader->arc().Archive->GetProperty(index, kpidMTime, &time) == S_OK &&
            time.vt == VT_FILETIME;
        const quint64 modified = hasTime ? FILETIME_To_UInt64(time.filetime) : 0;
        if (native.IsDir && path.endsWith(u'/'))
            path.chop(1);
        auto parts = path.split(u'/');
        // CPIO/TAR can explicitly describe the root and prefix entries with ./.
        // Remove only current-directory components; retain parent traversal for validation.
        parts.removeAll(QStringLiteral("."));
        if (parts.isEmpty() && native.IsDir && path.size() <= 32767) {
            state.Entries[0].Modified = modified;
            continue;
        }
        if (parts.isEmpty() || path.size() > 32767 ||
            std::any_of(parts.cbegin(), parts.cend(),
                        [](const QString &part) { return !SafeComponent(part); })) {
            state.skip(originalPath,
                       QCoreApplication::translate(
                           "ArchiveReadOnlyView", "The name cannot be represented safely in SMB."));
            continue;
        }
        quint64 parent = 1;
        QString prefix;
        for (qsizetype component = 0; component < parts.size(); ++component) {
            const bool last = component + 1 == parts.size();
            const bool directory = !last || native.IsDir;
            if (!prefix.isEmpty())
                prefix += u'/';
            prefix += parts.at(component);
            const auto key = Key(prefix);
            auto existing = state.Paths.constFind(key);
            if (existing != state.Paths.cend()) {
                auto &entry = state.Entries[qsizetype(*existing - 1)];
                if (!directory || !entry.Directory || entry.Name != parts.at(component)) {
                    state.skip(originalPath, QCoreApplication::translate(
                                                 "ArchiveReadOnlyView",
                                                 "The name conflicts with an existing SMB entry."));
                    break;
                }
                parent = *existing;
                if (last)
                    entry.Modified = modified;
                continue;
            }
            quint64 size = 0;
            if (!directory) {
                NWindows::NCOM::CPropVariant value;
                const HRESULT sizeResult =
                    state.Reader->arc().Archive->GetProperty(index, kpidSize, &value);
                if (sizeResult == E_ABORT || sizeResult == E_OUTOFMEMORY)
                    return sizeResult;
                UInt64 nativeSize;
                if (sizeResult != S_OK || !ConvertPropVariantToUInt64(value, nativeSize)) {
                    state.skip(originalPath, QCoreApplication::translate(
                                                 "ArchiveReadOnlyView",
                                                 "The reader does not report the entry size."));
                    break;
                }
                size = nativeSize;
            }
            const quint64 id = quint64(state.Entries.size()) + 1;
            state.Entries.append(
                {id, parent, parts.at(component), size, directory, last ? modified : 0});
            state.Paths.insert(key, id);
            if (!directory) {
                State::Item item{index};
                NWindows::NCOM::CPropVariant encrypted;
                if (state.Reader->arc().Archive->GetProperty(index, kpidEncrypted, &encrypted) ==
                    S_OK)
                    item.Encrypted = encrypted.vt == VT_BOOL && encrypted.boolVal != VARIANT_FALSE;
                state.Items.insert(id, std::move(item));
            }
            parent = id;
        }
    }
    state.Ready = true;
    return S_OK;
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
    result.Result = state.Items[id].Stream->ReadAt(offset, result.Data.data(),
                                                   UInt32(result.Data.size()), &actual, request);
    if (result.Result != S_OK) {
        result.Data.clear();
        return result;
    }
    if (actual > quint64(result.Data.size())) {
        result.Data.clear();
        result.Result = E_FAIL;
        result.Error = QStringLiteral("The reader returned an invalid byte count.");
        return result;
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
