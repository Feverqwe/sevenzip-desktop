#pragma once
#include "archive/operations/Update.h"
#include <functional>

// A native enumeration snapshot survives staging and final archive publication.
// It is not a list of paths to delete regardless of subsequent source changes.
class QtArchiveDeleteInputs {
    CDirItems DirItems;
    CByteBuffer ProcessedItems;
    CBoolPair SymLinks;

  public:
    QtArchiveDeleteInputs(const CDirItems &items, const CByteBuffer &processed,
                          const CBoolPair &symLinks);
    HRESULT Delete(IUpdateDeletionCallback &callback) const;
};

class QtUpdateDeletionCallback final : public IUpdateDeletionCallback {
  public:
    std::function<HRESULT(const FString &, bool)> Progress;
    HRESULT DeletingAfterArchiving(const FString &path, bool isDir) override;
    HRESULT FinishDeletingAfterArchiving() override;
};
