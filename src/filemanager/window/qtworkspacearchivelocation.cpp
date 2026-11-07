#include "archive/resources/qtarchivedirectoryread.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

namespace {
HRESULT ReadDirectory(CPanel &source, QWidget *parent, const QString &settings,
                      bool interactive = true) {
    // The joined worker owns provider reads only within this scope. Release
    // the guard before render can replace retained scan-operation guards.
    const CPanel::CDisableNotify notifications(source);
    return ReadArchiveDirectory(*static_cast<CAgentFolder *>(source.GetFolder()), false, parent,
                                settings, !interactive);
}

HRESULT FollowDirectories(CPanel &source, const QList<QtArchiveLocation::Directory> &directories,
                          const QString &folder, QWidget *parent, const QString &settings,
                          bool interactive = true) {
    const unsigned depth = source.GetParentFolders().Size();
    for (const auto &directory : directories) {
        if (directory.name.isEmpty() || directory.target.isEmpty())
            return E_INVALIDARG;
        HRESULT read = ReadDirectory(source, parent, settings, interactive);
        if (read != S_OK)
            return read;
        {
            const CPanel::CDisableNotify notifications(source);
            CMyComPtr<IFolderFolder> child;
            read = source.GetFolder()->BindToFolder(ToUString(directory.name), &child);
            if (read != S_OK)
                return read;
            if (!child)
                return E_INVALIDARG;
            source.SetNewFolder(child);
            source.LoadFullPath();
        }
        if (!source.IsArcFolder() || source.GetParentFolders().Size() != depth ||
            ToQString(source.GetCurrentFolderPrefix()) != directory.target)
            return E_FAIL;
    }
    if (ToQString(GetFolderPath(source.GetFolder())) != folder)
        return E_FAIL;
    return ReadDirectory(source, parent, settings, interactive);
}
} // namespace

HRESULT QtWindowWorkspace::OpenArchiveDirectory(unsigned panel, const QtArchiveLocation &location) {
    QtAppOperationState::CallScope call(window_.app_.QtOperations());
    const QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
    auto &source = window_.app_.Panels[panel];
    window_.cancelListPublication();
    // Walk native parent interfaces without closing this archive's reader.
    for (;;) {
        CMyComPtr<IFolderFolder> parent;
        const HRESULT result = source.GetFolder()->BindToParentFolder(&parent);
        if (result != S_OK)
            return result;
        if (!parent)
            break;
        source.SetNewFolder(parent);
    }
    source.LoadFullPath();
    if (!GetFolderPath(source.GetFolder()).IsEmpty())
        return E_FAIL;
    HRESULT result = FollowDirectories(source, location.directories, location.folder, &window_,
                                       window_.toolbarSettingsPath());
    if (result == S_OK)
        result = window_.renderBoundFolder();
    if (result == S_OK && window_.panelUi_[panel].scanningArchive)
        result = window_.panelUi_[panel].listProjection->waitForFinished();
    return result;
}

HRESULT QtWindowWorkspace::OpenArchiveLocation(unsigned panel, const QtArchiveLocation &location,
                                               const std::vector<ReopenLayer> *layers,
                                               bool interactive) {
    QtAppOperationState::CallScope call(window_.app_.QtOperations());
    const QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
    auto &source = window_.app_.Panels[panel];
    if (layers && layers->size() != size_t(location.nested.size() + 1))
        return E_INVALIDARG;
    const auto resetPassword = qScopeGuard([&] { source.SetArchiveOpenPassword(std::nullopt); });
    window_.cancelListPublication();
    // Retry can still display the old archive root while the candidate source
    // is in a subfolder. Bind the native source, bypassing display-path shortcuts.
    // Drop a previous failed candidate so Retry reads a replaced archive too.
    COpenResult reset;
    HRESULT result = source.BindToPath(UString(), UString(), reset);
    if (result != S_OK)
        return result;
    COpenResult physical;
    source.SetArchiveOpenPassword(layers ? layers->front().password : std::nullopt);
    result = source.BindToPath(ToUString(location.physicalPath),
                               layers ? ToUString(layers->front().format) : UString(), physical);
    if (result != S_OK)
        return result;

    const auto &parents = source.GetParentFolders();
    if (!source.IsArcFolder() || parents.Size() != 1 ||
        ToQString(parents.Front().VirtualPath) != location.physicalPath ||
        !GetFolderPath(source.GetFolder()).IsEmpty())
        return S_FALSE;
    result = ReadDirectory(source, &window_, window_.toolbarSettingsPath(), interactive);
    if (result != S_OK)
        return result;

    // Each boundary comes from a recorded native link, not from splitting a
    // displayed archive path. The native opener retains stream/password/temp
    // policy and performs all archive decoding.
    unsigned level = 1;
    for (const auto &link : location.nested) {
        result = FollowDirectories(source, link.directories, link.parentFolder, &window_,
                                   window_.toolbarSettingsPath(), interactive);
        if (result != S_OK)
            return result;
        result = source.LoadListItems();
        if (result != S_OK)
            return result;
        std::optional<UInt32> entry;
        for (UInt32 index = 0; index < source.GetLoadedItemCount(); ++index)
            if (ToQString(source.GetItemRelPath(index)) == link.entry) {
                if (entry)
                    return E_INVALIDARG;
                entry = index;
            }
        if (!entry || source.IsItem_Folder(*entry))
            return E_INVALIDARG;
        const unsigned depth = source.GetParentFolders().Size();
        source.LoadFullPath();
        source.SetArchiveOpenPassword(layers ? (*layers)[level].password : std::nullopt);
        const UString format = layers ? ToUString((*layers)[level].format) : UString();
        result = source.OpenItemInArchiveInternal(*entry, layers ? format.Ptr() : nullptr);
        ++level;
        window_.cancelListPublication();
        if (result != S_OK)
            return result;
        if (!source.IsArcFolder() || source.GetParentFolders().Size() != depth + 1 ||
            ToQString(source.GetParentFolders().Back().RelPath) != link.entry)
            return S_FALSE;
    }
    result = FollowDirectories(source, location.directories, location.folder, &window_,
                               window_.toolbarSettingsPath(), interactive);
    if (result != S_OK)
        return result;
    if (!source.IsArcFolder() || source.GetParentFolders().Size() != location.nested.size() + 1 ||
        ToQString(GetFolderPath(source.GetFolder())) != location.folder)
        return E_FAIL;
    result = window_.renderBoundFolder();
    if (result == S_OK && window_.panelUi_[panel].scanningArchive)
        result = window_.panelUi_[panel].listProjection->waitForFinished();
    return result;
}
