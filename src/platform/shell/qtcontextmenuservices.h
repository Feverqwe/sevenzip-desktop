#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"

// Original Explorer invocation arguments, transported without shell parsing.
// Implementations own launch/workflow integration; the source owner chooses
// commands, names and extraction policies.
class IQtContextMenuServices {
  public:
    virtual ~IQtContextMenuServices() = default;
    virtual void Open(const UString &path, const UString &type) = 0;
    virtual void ExtractArchives(const UStringVector &paths, const UString &folder, bool showDialog,
                                 bool elimDup, UInt32 writeZone) = 0;
    virtual void TestArchives(const UStringVector &paths, bool hashMode = false) = 0;
    virtual void CompressFiles(const UString &folder, const UString &name, const UString &type,
                               bool addExtension, const UStringVector &paths, bool email,
                               bool showDialog, bool waitFinish) = 0;
    virtual void CalcChecksum(const UStringVector &paths, const UString &method) = 0;
    virtual void ShowErrorMessage(const UString &message) = 0;
    virtual void ShowErrorMessageRes(UInt32 resourceID) = 0;
};
