#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "archive/opening/archivedirectoryaccess.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview_p.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QThread>
#include <algorithm>

namespace {
QString Key(const QString &path) { return path.normalized(QString::NormalizationForm_C); }
} // namespace
bool ArchiveReadOnlyView::State::safeComponent(const QString &name) {
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
           name.size() <= 255 && !name.contains(QChar::Null) && !name.contains(u':') &&
           !name.contains(u'/') && !name.contains(u'\\') && !name.endsWith(u'.') &&
           !name.endsWith(u' ') && QString::fromUtf8(name.toUtf8()) == name;
}

void ArchiveReadOnlyView::State::skip(const QString &path, const QString &reason) {
    const QString message =
        QCoreApplication::translate("ArchiveReadOnlyView", "Skipped %1: %2").arg(path, reason);
    Skipped.append(message);
    qInfo().noquote() << message;
}

HRESULT ArchiveReadOnlyView::State::symlinkTarget(UInt32 index, const QString &path,
                                                  QString &target) {
    NWindows::NCOM::CPropVariant link, mode;
    RINOK(Reader->arc().Archive->GetProperty(index, kpidSymLink, &link))
    RINOK(Reader->arc().Archive->GetProperty(index, kpidPosixAttrib, &mode))
    if (link.vt == VT_EMPTY && !(mode.vt == VT_UI4 && (mode.ulVal & 0170000) == 0120000))
        return S_OK;
    if (link.vt == VT_BSTR)
        target = QString::fromWCharArray(link.bstrVal, SysStringLen(link.bstrVal));
    const auto slash = path.lastIndexOf(u'/');
    const auto resolved = QDir::cleanPath((slash < 0 ? QString() : path.left(slash + 1)) + target);
    if (target.isEmpty() || target.size() > 4096 || target.contains(QChar::Null) ||
        target.contains(u'\\') || target.contains(u':') || target.startsWith(u'/') ||
        resolved == ".." || resolved.startsWith("../")) {
        skip(path,
             QCoreApplication::translate("ArchiveReadOnlyView",
                                         "The symbolic link cannot be represented safely in SMB."));
        return S_FALSE;
    }
#ifdef Q_OS_MACOS
    // The native SMB client's target validation rejects every parent component,
    // including references that remain inside this share.
    if (target.split(u'/').contains(QStringLiteral(".."))) {
        skip(path, QCoreApplication::translate(
                       "ArchiveReadOnlyView",
                       "The macOS SMB client cannot copy this symbolic link. Extract it "
                       "directly from Sevenzip to preserve it."));
        return S_FALSE;
    }
#endif
    return S_OK;
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
    state.reportPreparation(PreparationProgress::Stage::ReadingDirectories, 0, 0);
    if (state.LazyRequested && state.Reader->directoryReadRequested()) {
        state.Reader->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess,
                                                   (void **)&state.Directories);
        if (state.Directories) {
            RINOK(state.isolateDirectoryReader())
            ArchiveNativeReadScope scope(state.Reader, request);
            RINOK(state.initializeDirectories())
            state.Ready = true;
            return S_OK;
        }
    }
    ArchiveNativeReadScope scope(state.Reader, request);
    CMyComPtr<IArchiveDirectoryAccess> directories;
    state.Reader->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess, (void **)&directories);
    if (directories) {
        UInt32 root = UINT32_MAX;
        if (!state.FolderPrefix.isEmpty()) {
            UInt32 loaded = 0;
            RINOK(state.Reader->arc().Archive->GetNumberOfItems(&loaded))
            for (UInt32 index = 0; index < loaded; ++index) {
                RINOK(request->SetCompleted(nullptr))
                CReadArcItem item;
                RINOK(state.Reader->arc().GetItem(index, item))
                QString path = QString::fromWCharArray(item.Path);
                path.replace(u'\\', u'/');
                if (item.IsDir &&
                    (path == state.FolderPrefix || path == state.FolderPrefix.chopped(1))) {
                    root = index;
                    break;
                }
            }
        }
        // A panel-selected directory is already published by the native reader.
        // Do not enumerate unrelated subtrees merely to export that directory.
        RINOK(directories->LoadDirectory(root, true, request))
    }
    QHash<QString, quint64> paths;
    state.Entries.append({1, 0, {}, 0, true});
    paths.insert({}, 1);
    UInt32 count = 0;
    RINOK(state.Reader->arc().Archive->GetNumberOfItems(&count))
    state.reportPreparation(PreparationProgress::Stage::ListingEntries, 0, count);
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
        state.reportPreparation(PreparationProgress::Stage::ListingEntries, index, count, path);
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
        QString target;
        const HRESULT linkResult = state.symlinkTarget(index, path, target);
        if (linkResult == S_FALSE)
            continue;
        RINOK(linkResult)
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
                        [](const QString &part) { return !State::safeComponent(part); })) {
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
            auto existing = paths.constFind(key);
            if (existing != paths.cend()) {
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
            state.Entries.append({id, parent, parts.at(component), size, directory,
                                  last ? modified : 0, true, last ? target : QString()});
            paths.insert(key, id);
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
    state.reportPreparation(PreparationProgress::Stage::ListingEntries, count, count);
    state.Ready = true;
    return S_OK;
}
