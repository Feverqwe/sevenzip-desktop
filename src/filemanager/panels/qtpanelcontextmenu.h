#pragma once
#include "filemanager/panels/qtpanelcontextmenuservices.h"
#include <memory>

class MainWindow;
class QAbstractItemView;

class QtPanelContextMenu final : public IQtPanelContextMenuServices {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void Populate(unsigned owner);

  public:
    QtPanelContextMenu(MainWindow &window, QAbstractItemView &view);
    ~QtPanelContextMenu() override;
    bool IsHeader(QWidget *handle) const override;
    bool IsList(QWidget *handle) const override;
    void ShowColumnsContextMenu(int x, int y) override;
    bool GetItemRect(int index, QRect &rect) const override;
    void ClientToScreen(int &x, int &y) const override;
    void CreatePopup() override;
    IQtFileMenuServices &Menu() override;
    IQtPanelPluginMenuServices &Plugins() override;
    unsigned Track(int x, int y) override;
    void InvokePluginCommand(unsigned id) override;
    bool ExecuteFileCommand(unsigned id) override;
};
