#pragma once

// Narrow presentation/workflow boundary for the original command switch.
// The CApp command owner supplies capability policy before invoking a workflow.
class IQtFileManagerCommandServices {
  public:
    virtual ~IQtFileManagerCommandServices() = default;
    virtual bool CanExecuteCommand(unsigned id) = 0;
    virtual bool IsShiftDown() const = 0;
    virtual void InvokePluginCommand(unsigned id) = 0;
    virtual void ReleasePluginMenus() = 0;
    virtual void OpenItem() = 0;
    virtual void OpenItemInside(const wchar_t *type) = 0;
    virtual void OpenItemOutside() = 0;
    virtual void EditItem(bool useEditor) = 0;
    virtual void Rename() = 0;
    virtual void CopyTo() = 0;
    virtual void MoveTo() = 0;
    virtual void Delete(bool toRecycleBin) = 0;
    virtual void CalculateCrc(const char *methodName) = 0;
    virtual void Split() = 0;
    virtual void Combine() = 0;
    virtual void Properties() = 0;
    virtual void Comment() = 0;
    virtual void CreateFolder() = 0;
    virtual void CreateFile() = 0;
    virtual void Link() = 0;
    virtual void OpenAltStreams() = 0;
};

class IQtFileManagerMenuCommandServices : public IQtFileManagerCommandServices {
  public:
    virtual void AddToArchive() = 0;
    virtual void ExtractArchives() = 0;
    virtual void TestArchives() = 0;
    virtual bool ExecuteExtensionCommand(unsigned id) = 0;
    virtual void CloseWindow() = 0;
    virtual void SelectAll(bool select) = 0;
    virtual void InvertSelection() = 0;
    virtual void SelectSpec(bool select) = 0;
    virtual void SelectByType(bool select) = 0;
    virtual void Refresh_StatusBar() = 0;
    virtual void SetListViewMode(unsigned index) = 0;
    virtual void SortItemsWithPropID(unsigned propID) = 0;
    virtual void OpenRootFolder() = 0;
    virtual void OpenParentFolder() = 0;
    virtual void FoldersHistory() = 0;
    virtual void RefreshView() = 0;
    virtual void SwitchOnOffOnePanel() = 0;
    virtual void OptionsDialog() = 0;
    virtual void AboutDialog() = 0;
    virtual void MyBrowseForTempFolder() = 0;
    virtual void OpenBookmark(unsigned index) = 0;
    virtual void SetBookmark(unsigned index) = 0;
};

class CApp;
bool WndProc(unsigned commandID, CApp &app, IQtFileManagerMenuCommandServices &services);

// Source command dispatch is shared by native window routing and Qt consumers.
const char *GetHashMethodForCommand(unsigned id);
bool ExecuteFileCommand(unsigned id, IQtFileManagerCommandServices &app);
bool OnMenuCommand(unsigned id, IQtFileManagerMenuCommandServices &app);
