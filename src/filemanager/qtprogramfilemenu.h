#pragma once
#include <QList>
#include <memory>

class QAction;
class QMenu;
class CPanel;
class MainWindow;
class QtFileManagerShellMenu;

// Presentation only; actions retain their source command IDs and dispatch.
void QtClarifyOpenCommands(QMenu &menu);
// Group an already filtered source projection. Returned rows are owned by the
// menu and must join its existing disposable-row lifecycle.
QList<QAction *> QtGroupFileCommands(QMenu &menu, bool contextMenu);

// Retains the original program-menu plugin context through source dispatch.
// Static resource actions and disposable shell rows have separate ownership.
class QtProgramFileMenu final {
    MainWindow &_window;
    QMenu &_menu;
    QList<QAction *> _source;
    QList<QAction *> _dynamic;
    std::unique_ptr<QtFileManagerShellMenu> _shell;
    CPanel *_panel = nullptr;
    void ClearDynamicRows();

  public:
    explicit QtProgramFileMenu(MainWindow &window);
    ~QtProgramFileMenu();
    const QList<QAction *> &SourceActions() const { return _source; }
    void Activate();
    void ReloadLanguage();
    bool CanInvoke(unsigned id) const;
    void InvokePluginCommand(unsigned id);
    void ReleasePluginMenus();
};
