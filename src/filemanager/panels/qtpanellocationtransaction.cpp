#include "filemanager/panels/qtpanellocationtransaction.h"
#include "filemanager/panels/Panel.h"
#include <QScopeGuard>
#include <utility>

struct QtPanelLocationTransaction::Location {
    CPanel::CDisableTimerProcessing timer;
    CPanel::CDisableNotify notifications;
    NWindows::NDLL::CLibrary library;
    CObjectVector<CFolderLink> parents;
    CMyComPtr<IFolderFolder> folder;
    CMyComPtr<IFolderCompare> compare;
    CMyComPtr<IFolderGetItemName> names;
    CMyComPtr<IArchiveGetRawProps> raw;
    CMyComPtr<IFolderAltStreams> streams;
    CMyComPtr<IFolderOperations> operations;
    CMyComPtr<IFolderGetSystemIconIndex> icons;
    UString prefix;
    CRecordVector<bool> directories;
    CRecordVector<Int32> itemIcons;
    CRecordVector<bool> systemIcons;
    CRecordVector<bool> selected;
    HRESULT loadResult = S_OK;
    std::optional<UInt32> itemCount;
    bool drivesIcons = false;
    bool archiveIcons = false;
    bool flat = false;
    bool deleted = false;
    explicit Location(CPanel &panel)
        : timer(panel), notifications(panel), prefix(panel.GetCurrentFolderPrefix()) {}
    ~Location() {
        // Exceptional commit cleanup releases children before parent DLLs.
        // It neither prompts nor deletes retained recovery files.
        folder.Release();
        compare.Release();
        names.Release();
        raw.Release();
        streams.Release();
        operations.Release();
        icons.Release();
        library.Free();
        while (!parents.IsEmpty())
            parents.DeleteBack();
    }
};

namespace {
template <typename T> void ExchangeInterface(CMyComPtr<T> &left, CMyComPtr<T> &right) {
    T *value = left.Detach();
    left.Attach(right.Detach());
    right.Attach(value);
}
} // namespace

QtPanelLocationTransaction::QtPanelLocationTransaction(CPanel &panel)
    : panel_(panel), previous_(std::make_unique<Location>(panel)) {
    Exchange();
    panel_._currentFolderPrefix.Empty();
    // The caller captured the visible selection. Pending refresh hints belong
    // to that publication, rather than to a newly probed source.
    panel_._refreshSelection.reset();
}

QtPanelLocationTransaction::~QtPanelLocationTransaction() {
    if (!finished_) {
        try {
            Rollback();
        } catch (...) {
            // Rollback restores ownership even if candidate close fails.
        }
    }
}

void QtPanelLocationTransaction::Exchange() {
    auto &saved = *previous_;
    ExchangeInterface(panel_._folder, saved.folder);
    ExchangeInterface(panel_._folderCompare, saved.compare);
    ExchangeInterface(panel_._folderGetItemName, saved.names);
    ExchangeInterface(panel_._folderRawProps, saved.raw);
    ExchangeInterface(panel_._folderAltStreams, saved.streams);
    ExchangeInterface(panel_._folderOperations, saved.operations);
    ExchangeInterface(panel_._folderGetSystemIconIndex, saved.icons);
    panel_._parentFolders.Swap(saved.parents);
    const auto library = panel_._library.Detach();
    panel_._library.Attach(saved.library.Detach());
    saved.library.Attach(library);
    std::swap(panel_._flatMode, saved.flat);
    std::swap(panel_._thereAreDeletedItems, saved.deleted);
    panel_._isDirVector.Swap(saved.directories);
    panel_._itemIconIndices.Swap(saved.itemIcons);
    panel_._itemSystemIcons.Swap(saved.systemIcons);
    panel_._selectedStatusVector.Swap(saved.selected);
    std::swap(panel_._listLoadResult, saved.loadResult);
    std::swap(panel_._loadedItemCount, saved.itemCount);
    std::swap(panel_._iconIsFSDrivesFolder, saved.drivesIcons);
    std::swap(panel_._iconIsArcFolder, saved.archiveIcons);
    ++panel_._folderBindingRevision;
}

void QtPanelLocationTransaction::Commit() {
    if (finished_)
        return;
    // Native close runs on the real panel with its host/callback, including
    // modified nested-archive handling. No interactive close in the destructor.
    {
        const UString acceptedPrefix(panel_._currentFolderPrefix);
        Exchange();
        // The view already presents the accepted source. Native close still
        // runs on its real panel, but must not erase that source's rows.
        auto view = std::exchange(panel_._selectionList, QtPanelList{});
        const auto accept = qScopeGuard([this, &acceptedPrefix] {
            Exchange();
            panel_._currentFolderPrefix.SetFrom(acceptedPrefix.Ptr(), acceptedPrefix.Len());
            finished_ = true;
        });
        const auto restoreView = qScopeGuard([&] { panel_._selectionList = std::move(view); });
        // Empty/SetFrom retain the panel buffer. Both prefixes already fit,
        // so cleanup restores the accepted path without allocation/provider calls.
        panel_._currentFolderPrefix.SetFrom(previous_->prefix.Ptr(), previous_->prefix.Len());
        panel_.CloseOpenFolders();
    }
}

void QtPanelLocationTransaction::Rollback() {
    if (finished_)
        return;
    {
        const auto restore = qScopeGuard([this] {
            Exchange();
            panel_._currentFolderPrefix.SetFrom(previous_->prefix.Ptr(), previous_->prefix.Len());
            finished_ = true;
        });
        panel_.CloseOpenFolders();
    }
}
