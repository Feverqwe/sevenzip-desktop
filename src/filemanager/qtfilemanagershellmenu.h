#pragma once
#include "filemanager/panels/qtpanelpluginservices.h"
#include "platform/shell/ContextMenu.h"
#include "platform/shell/qtshellarchiveservices.h"
#include <functional>

class MainWindow;
class QMenu;
class QIcon;
#ifdef _WIN32
class QtNativeShellMenu;
#endif

// Popup lifetime and Qt rendering for the original Panel/Explorer menu owner.
// The copied owner supplies paths, IDs, labels and workflow decisions.
class QtFileManagerShellMenu final : public IQtPanelPluginServices,
                                     public IQtPanelPluginMenuServices {
    MainWindow &_window;
    QMenu &_menu;
    QtShellArchiveServices _services;
    std::unique_ptr<CZipContextMenu> _context;
#ifdef _WIN32
    std::unique_ptr<QtNativeShellMenu> _system;
#endif
    quint64 _generation = 0;
    std::function<void(unsigned)> _dispatch;
    void Render(QMenu &menu, const NQtShellMenu::CMenuHandle &tree);
    void RenderItems(QMenu &menu, const NQtShellMenu::CMenuHandle &tree, const QIcon &icon);
    static bool Exposed(const CZipContextMenu::CCommandMapItem &command);
    bool Available(const CZipContextMenu::CCommandMapItem &command) const;
    HRESULT Open(const UString &path, const UString &type);
    void Dispatch(unsigned id, quint64 generation);

  public:
    QtFileManagerShellMenu(MainWindow &window, QMenu &menu, std::function<void(unsigned)> dispatch);
    ~QtFileManagerShellMenu();
    bool CanInvoke(unsigned id) const;
    void InvokePluginCommand(unsigned id);
    void ReleasePluginMenus() override;
    bool IsShiftDown() const override;
    bool ShowSystemMenu() const override;
    void CreateSevenZipMenu(CPanel &panel, bool extended, const CRecordVector<UInt32> &indices,
                            int firstDirIndex) override;
    void CreateSystemMenu(CPanel &panel, bool extended,
                          const CRecordVector<UInt32> &indices) override;
    bool HasContextMenu(bool system) const override;
    HRESULT InvokeCommand(unsigned offset, bool system, const UString &directory) override;
    void ShowInvokeError(HRESULT result) override;
};
