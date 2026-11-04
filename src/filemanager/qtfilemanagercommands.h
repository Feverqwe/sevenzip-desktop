#pragma once
#include "Common/MyWindows.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <functional>

class QAction;
class CApp;
class MainWindow;
class CPanel;
class QtArchiveMountManager;

// Qt presentation and existing transport consumers for CApp's native entry.
// Native menu policy is recomputed for execution, never read from QAction state.
class QtFileManagerCommands final : public IQtFileManagerMenuCommandServices {
  public:
    enum Extension : unsigned {
        Back = 1000,
        OpenArchivePicker,
        OpenDevicePicker,
        MountReadOnly,
        ManageMounts,
        CalculateDirectorySize
    };
    explicit QtFileManagerCommands(MainWindow &window);
    void AttachActions();
    void RefreshActions();
    void RefreshViewActions();
    void ActivateItems(Qt::KeyboardModifiers modifiers);
    bool CanCloseMounts();
    bool CanExecuteCommand(unsigned id) override;
    bool IsShiftDown() const override;
    void InvokePluginCommand(unsigned id) override;
    void ReleasePluginMenus() override;
    void OpenItem() override;
    void OpenItemInside(const wchar_t *type) override;
    void OpenItemOutside() override;
    void EditItem(bool useEditor) override;
    void Rename() override;
    void CopyTo() override;
    void MoveTo() override;
    void Delete(bool toRecycleBin) override;
    void CalculateCrc(const char *methodName) override;
    void DiffFiles() override;
    void VerCtrl(unsigned id) override;
    void Split() override;
    void Combine() override;
    void Properties() override;
    void Comment() override;
    void CreateFolder() override;
    void CreateFile() override;
    void Link() override;
    void OpenAltStreams() override;
    void AddToArchive() override;
    void ExtractArchives() override;
    void TestArchives() override;
    bool ExecuteExtensionCommand(unsigned id) override;
    void CloseWindow() override;
    void SelectAll(bool select) override;
    void InvertSelection() override;
    void SelectSpec(bool select) override;
    void SelectByType(bool select) override;
    void Refresh_StatusBar() override;
    void SetListViewMode(unsigned index) override;
    void SortItemsWithPropID(unsigned propID) override;
    void OpenRootFolder() override;
    void OpenParentFolder() override;
    void FoldersHistory() override;
    void ChangeFlatMode() override;
    void RefreshView() override;
    void Change_AutoRefresh_Mode() override;
    void SwitchOnOffOnePanel() override;
    void SwitchStandardToolbar() override;
    void SwitchArchiveToolbar() override;
    void SwitchButtonsLables() override;
    void SwitchLargeButtons() override;
    bool &TimestampShowUTC() override;
    void RedrawListItems_InPanels() override;
    unsigned TimestampLevelCount() const override;
    int TimestampLevel(unsigned index) const override;
    void SetTimestampLevel(int level) override;
    void OptionsDialog() override;
    void MyBenchmark(bool totalMode) override;
    void ShowHelpWindow(const char *topic) override;
    void AboutDialog() override;
    void MyBrowseForTempFolder() override;
    void OpenBookmark(unsigned index) override;
    void SetBookmark(unsigned index) override;

  private:
    bool CanSwitchPanels() const;
    bool CanExit() const;
    QPointer<QtArchiveMountManager> mounts_;
    void AttachMountActions();
    void MountReadOnlyContainer();
    void ShowMounts();
    void AttachDirectoryActions();
    bool CanCalculateDirectorySize() const;
    void CalculateDirectorySizes();
    void OpenDevice();
    struct TransportCapabilities {
        bool filesystem = false, archive = false, writable = false, mutationWritable = false;
        bool filesystemStreams = false;
        bool batchArchives = false;
        bool copy = false;
        bool root = false;
        bool comment = false;
        bool focusedNonParent = false;
        bool benchmark = false;
        QHash<unsigned, bool> hashMethods;
        bool one = false, oneFile = false, operated = false, focused = false;
        bool focusedFile = false, folderSize = false, selectedParent = false;
        bool split = false, combine = false, rows = false, foldersHistory = false;
    };
    TransportCapabilities ReadTransportCapabilities();
    bool TransportAllows(unsigned id, const TransportCapabilities &capabilities) const;
    QHash<unsigned, bool> Availability();
    enum class ActionRefresh { Idle, Publishing, Pending };
    void PublishActions();
    void AttachFileActions();
    void AttachPanelActions();
    void AttachViewActions();
    void Bind(QAction *action, unsigned id);
    void BindToggle(QAction *action, unsigned id, bool (*readState)(CApp &));
    void OpenNative(bool internal, bool external, const wchar_t *type);
    void RunNativeOpen(const std::function<void(CPanel &)> &open);
    MainWindow &window_;
    QHash<unsigned, QList<QAction *>> actions_;
    ActionRefresh actionRefresh_ = ActionRefresh::Idle;
};
