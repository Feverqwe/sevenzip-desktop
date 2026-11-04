#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview_p.h"
#include "archive/resources/qtarchiveitemstream.h"
#include "archive/resources/qtarchivenativeopenprogress.h"
#include <QCoreApplication>
#include <QSet>

namespace {
HRESULT DirectoryItems(IArchiveDirectoryAccess *reader, UInt32 parent, CUIntVector &items) {
    UInt32 count = 0;
    RINOK(reader->GetDirectoryItems(parent, &count, nullptr))
    items.ClearAndSetSize(count);
    return reader->GetDirectoryItems(parent, &count, items.Size() ? &items[0] : nullptr);
}
} // namespace

HRESULT ArchiveReadOnlyView::State::isolateDirectoryReader() {
    CMyComPtr<IInStream> input;
    RINOK(CreateArchiveInputStream(Resource, Reader, Depth ? Depth - 1 : 0, &input))
    const auto request = readRequest();
    const auto layer = ArchiveInputLayer::Create(input, request);
    if (!layer)
        return layer.error();
    CMyComPtr<IInStream> cursor;
    RINOK((*layer)->OpenCursor(&cursor))
    const auto catalog = Reader->codecCatalog();
    const QString format =
        QString::fromWCharArray(Reader->nativeCodecs().GetFormatNamePtr(Reader->arc().FormatIndex));
    CObjectVector<COpenType> types;
    CIntVector excluded;
    if (!catalog->parseOpenTypes(format, types))
        return E_INVALIDARG;
    COpenOptions options;
    options.props = nullptr;
    options.types = &types;
    options.excludedFormats = &excluded;
    options.stream = cursor;
    options.PreferDirectoryRead = true;
    CNativeOpenProgress progress(request, nullptr, nullptr);
    std::shared_ptr<ArchiveNativeOpen> isolated;
    RINOK(ArchiveBackend::openNativeArchive(catalog, QStringLiteral("mount-filesystem.img"), format,
                                            &progress, isolated, &options))
    CMyComPtr<IArchiveDirectoryAccess> directories;
    RINOK(
        isolated->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess, (void **)&directories))
    Reader = std::move(isolated);
    Directories = directories;
    return S_OK;
}

HRESULT ArchiveReadOnlyView::State::initializeDirectories() {
    UInt32 parent = UINT32_MAX;
    const auto request = readRequest();
    for (const auto &part : FolderPrefix.split(u'/', Qt::SkipEmptyParts)) {
        if (!safeComponent(part))
            return E_INVALIDARG;
        RINOK(Directories->LoadDirectory(parent, false, request))
        CUIntVector items;
        RINOK(DirectoryItems(Directories, parent, items))
        bool found = false;
        FOR_VECTOR(i, items) {
            RINOK(request->SetCompleted(nullptr))
            NWindows::NCOM::CPropVariant name, directory;
            RINOK(Reader->arc().Archive->GetProperty(items[i], kpidName, &name))
            RINOK(Reader->arc().Archive->GetProperty(items[i], kpidIsDir, &directory))
            if (directory.vt == VT_BOOL && directory.boolVal != VARIANT_FALSE &&
                name.vt == VT_BSTR && QString::fromWCharArray(name.bstrVal) == part) {
                parent = items[i];
                found = true;
                break;
            }
        }
        if (!found)
            return E_INVALIDARG;
    }
    Entries.append({1, 0, {}, 0, true, 0, false});
    NativeDirectories.insert(1, parent);
    return S_OK;
}

HRESULT ArchiveReadOnlyView::State::loadDirectory(quint64 id) {
    if (!id || id > quint64(Entries.size()) || !Entries[qsizetype(id - 1)].Directory)
        return E_INVALIDARG;
    if (Entries[qsizetype(id - 1)].ChildrenLoaded)
        return S_OK;
    const auto request = readRequest();
    const auto parent = NativeDirectories.constFind(id);
    if (parent == NativeDirectories.cend())
        return E_FAIL;
    RINOK(Directories->LoadDirectory(*parent, false, request))
    CUIntVector nativeItems;
    RINOK(DirectoryItems(Directories, *parent, nativeItems))
    QList<Entry> additions;
    QHash<quint64, Item> files;
    QHash<quint64, UInt32> directories;
    QSet<QString> names;
    FOR_VECTOR(i, nativeItems) {
        RINOK(request->SetCompleted(nullptr))
        const UInt32 index = nativeItems[i];
        bool alternate = false;
        RINOK(Archive_IsItem_AltStream(Reader->arc().Archive, index, alternate))
        NWindows::NCOM::CPropVariant name, isDirectory, size, time, encrypted;
        RINOK(Reader->arc().Archive->GetProperty(index, kpidName, &name))
        if (name.vt != VT_BSTR)
            return S_FALSE;
        const QString text = QString::fromWCharArray(name.bstrVal);
        if (alternate || !safeComponent(text) ||
            names.contains(text.normalized(QString::NormalizationForm_C))) {
            skip(text, QCoreApplication::translate(
                           "ArchiveReadOnlyView", "The name cannot be represented safely in SMB."));
            continue;
        }
        RINOK(Reader->arc().Archive->GetProperty(index, kpidIsDir, &isDirectory))
        QString path = text;
        for (quint64 ancestor = id; ancestor > 1;
             ancestor = Entries[qsizetype(ancestor - 1)].Parent)
            path.prepend(Entries[qsizetype(ancestor - 1)].Name + u'/');
        QString target;
        const HRESULT linkResult = symlinkTarget(index, path, target);
        if (linkResult == S_FALSE)
            continue;
        RINOK(linkResult)
        const bool directory = isDirectory.vt == VT_BOOL && isDirectory.boolVal != VARIANT_FALSE;
        UInt64 bytes = 0;
        if (!directory) {
            RINOK(Reader->arc().Archive->GetProperty(index, kpidSize, &size))
            if (!ConvertPropVariantToUInt64(size, bytes))
                return S_FALSE;
        }
        RINOK(Reader->arc().Archive->GetProperty(index, kpidMTime, &time))
        const quint64 entryId = quint64(Entries.size() + additions.size()) + 1;
        additions.append({entryId, id, text, bytes, directory,
                          time.vt == VT_FILETIME ? FILETIME_To_UInt64(time.filetime) : 0,
                          !directory, target});
        if (directory)
            directories.insert(entryId, index);
        else {
            Item item{};
            item.Index = index;
            RINOK(Reader->arc().Archive->GetProperty(index, kpidEncrypted, &encrypted))
            item.Encrypted = encrypted.vt == VT_BOOL && encrypted.boolVal != VARIANT_FALSE;
            files.insert(entryId, std::move(item));
        }
        names.insert(text.normalized(QString::NormalizationForm_C));
    }
    struct Rollback {
        State &Owner;
        qsizetype OldSize;
        quint64 Parent;
        const QList<Entry> &Added;
        bool Committed = false;
        ~Rollback() {
            if (Committed)
                return;
            Owner.Entries.resize(OldSize);
            Owner.Children.remove(Parent);
            for (const auto &entry : Added) {
                Owner.Items.remove(entry.Id);
                Owner.NativeDirectories.remove(entry.Id);
            }
        }
    } rollback{*this, Entries.size(), id, additions};
    QList<quint64> children;
    for (const auto &entry : additions)
        children.append(entry.Id);
    Entries.append(additions);
    Items.insert(files);
    NativeDirectories.insert(directories);
    Children.insert(id, std::move(children));
    Entries[qsizetype(id - 1)].ChildrenLoaded = true;
    rollback.Committed = true;
    return S_OK;
}

ArchiveReadOnlyView::DirectoryResult ArchiveReadOnlyView::listDirectory(quint64 id) {
    DirectoryResult result;
    try {
        auto &state = *state_;
        if (state.Cancelled) {
            result.Result = E_ABORT;
            return result;
        }
        if (!state.Ready || !id || id > quint64(state.Entries.size()) ||
            !state.Entries.at(qsizetype(id - 1)).Directory) {
            result.Result = E_INVALIDARG;
            return result;
        }
        if (state.Directories) {
            const auto request = state.readRequest();
            ArchiveNativeReadScope scope(state.Reader, request);
            result.Result = state.loadDirectory(id);
            if (result.Result != S_OK)
                return result;
            for (const auto child : state.Children.value(id))
                result.Entries.append(state.Entries.at(qsizetype(child - 1)));
        } else
            for (const auto &entry : state.Entries)
                if (entry.Parent == id)
                    result.Entries.append(entry);
        result.Result = S_OK;
    } catch (const std::bad_alloc &) {
        result.Result = E_OUTOFMEMORY;
    } catch (...) {
        result.Result = E_FAIL;
    }
    return result;
}
