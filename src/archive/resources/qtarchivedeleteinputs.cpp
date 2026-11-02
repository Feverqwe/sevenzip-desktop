#include "archive/resources/qtarchivedeleteinputs.h"

QtArchiveDeleteInputs::QtArchiveDeleteInputs(const CDirItems &items, const CByteBuffer &processed,
                                             const CBoolPair &symLinks)
    : DirItems(items), ProcessedItems(processed), SymLinks(symLinks) {
    DirItems.Callback = nullptr;
}

HRESULT QtArchiveDeleteInputs::Delete(IUpdateDeletionCallback &callback) const {
    return UpdateArchive(DirItems, ProcessedItems, SymLinks, &callback);
}

HRESULT QtUpdateDeletionCallback::DeletingAfterArchiving(const FString &path, bool isDir) {
    return Progress ? Progress(path, isDir) : S_OK;
}
HRESULT QtUpdateDeletionCallback::FinishDeletingAfterArchiving() { return S_OK; }
