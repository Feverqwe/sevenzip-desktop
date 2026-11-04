#include "platform/shell/qtnativeshellmenu.h"
#ifdef _WIN32
#include "Windows/Menu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/panelmenuids.h"
#include "platform/shell/qtnativeshellmenupresentation.h"
#include <QMenu>
#include <QSet>
#include <shlobj.h>

struct QtNativeShellMenu::State {
    // Balance only successful initialization; Qt may already own the STA.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CMyComPtr<IContextMenu> context;
    std::unique_ptr<QtNativeShellMenuPresentation> presentation;
    CPanel *panel = nullptr;
    QSet<unsigned> commands;
    ~State() {
        presentation.reset();
        context.Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }
};

QtNativeShellMenu::QtNativeShellMenu() : _state(std::make_unique<State>()) {}
QtNativeShellMenu::~QtNativeShellMenu() = default;

bool QtNativeShellMenu::Create(CPanel &panel, bool extended, const CRecordVector<UInt32> &indices,
                               QMenu &menu, std::function<void(unsigned)> dispatch) {
    NWindows::CMenu nativeMenu;
    NWindows::CMenuDestroyer release(nativeMenu);
    if (!nativeMenu.CreatePopup())
        return false;
    panel.CreateSystemMenu(nativeMenu, extended, indices, _state->context);
    if (!_state->context || nativeMenu.GetItemCount() == 0)
        return false;
    _state->panel = &panel;
    _state->presentation = std::make_unique<QtNativeShellMenuPresentation>(
        nativeMenu, _state->context, kSystemStartMenuID);
    nativeMenu.Detach();
    _state->commands = _state->presentation->Render(menu, dispatch);
    return true;
}

bool QtNativeShellMenu::CanInvoke(unsigned id) const { return _state->commands.contains(id); }

HRESULT QtNativeShellMenu::Invoke(unsigned offset) {
    if (!_state->panel || !CanInvoke(kSystemStartMenuID + offset))
        return E_INVALIDARG;
    return _state->panel->InvokeShellContextMenu(offset, _state->context);
}
#endif
