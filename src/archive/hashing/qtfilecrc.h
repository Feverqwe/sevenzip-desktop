// Entry adapter for the original PanelCrc filesystem worker.
#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include <functional>
struct CExternalCodecs;
struct CHashBundle;
class QWidget;

HRESULT CalculateFileCrc(const UString &method, const CExternalCodecs *providers,
                         const UString &basePrefix, const UStringVector &paths, bool enterToDirs,
                         QWidget *parent, bool *messageWasDisplayed = nullptr,
                         const std::function<bool(const CHashBundle &)> &presentResult = {});
