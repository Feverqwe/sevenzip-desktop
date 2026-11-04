#pragma once
#ifdef _WIN32
#include "Common/MyCom.h"
#include <QSet>
#include <functional>
#include <memory>
#include <shlobj.h>

class QMenu;

// Qt presentation of the native menu/provider produced by the panel shell provider.
// Adopts the HMENU; command generation and invocation stay with their source owners.
class QtNativeShellMenuPresentation final {
    struct State;
    std::shared_ptr<State> _state;

  public:
    QtNativeShellMenuPresentation(HMENU menu, const CMyComPtr<IContextMenu> &context,
                                  unsigned commandStart);
    ~QtNativeShellMenuPresentation();
    QSet<unsigned> Render(QMenu &target, const std::function<void(unsigned)> &dispatch);
};
#endif
