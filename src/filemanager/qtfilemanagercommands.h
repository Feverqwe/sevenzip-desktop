#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <functional>
#include <optional>

class QAction;
class QWidget;
class CApp;
class MainWindow;
class CPanel;
class QtArchiveMountManager;
class QtInlineFolderName;

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
        CalculateDirectorySize,
        SortAscending,
        SortDescending,
        Forward
    };
    explicit QtFileManagerCommands(MainWindow &window);
    void AttachActions();
    void RefreshActions();
    QHash<unsigned, bool> AvailabilityForPanel(unsigned panel);
    void RefreshViewActions();
    void ActivateItems(Qt::KeyboardModifiers modifiers);
    bool CanCloseMounts();
    QWidget *MountCloseBlocker() const;
    bool CanExecuteCommand(unsigned id) override;
    void InvokePluginCommand(unsigned id) override;
    void ReleasePluginMenus() override;
    void OpenItem() override;
    void OpenItemInside(const wchar_t *type) override;
    void OpenItemOutside() override;
    void EditItem(bool useEditor) override;
    void Rename() override;
    void CopyTo() override;
    void MoveTo() override;
    void MoveToTrash() override;
    void CalculateCrc(const char *methodName) override;
    void Properties() override;
    void CreateFolder() override;
    void DiscardStaleFolderDraft(unsigned panel);
    void AddToArchive() override;
    void ExtractArchives() override;
    void TestArchives() override;
    bool ExecuteExtensionCommand(unsigned id) override;
    void CloseWindow() override;
    void SelectAll(bool select) override;
    void Refresh_StatusBar() override;
    void SetListViewMode(unsigned index) override;
    void SortItemsWithPropID(unsigned propID) override;
    void OpenRootFolder() override;
    void OpenParentFolder() override;
    void FoldersHistory() override;
    void RefreshView() override;
    void SwitchOnOffOnePanel() override;
    void OptionsDialog() override;
    void AboutDialog() override;
    void OpenBookmark(unsigned index) override;
    void SetBookmark(unsigned index) override;

  private:
    bool CanSwitchPanels() const;
    bool CanExit() const;
    void AttachMountActions();
    void MountReadOnlyContainer();
    void ShowMounts();
    void AttachDirectoryActions();
    bool CanCalculateDirectorySize() const;
    void CalculateDirectorySizes();
    void OpenDevice();
    struct TransportCapabilities {
        bool filesystem = false, archive = false, writable = false, mutationWritable = false;
        bool filesystemWritable = false;
        bool filesystemStreams = false;
        bool batchArchives = false;
        bool copy = false;
        bool root = false;
        bool focusedNonParent = false;
        QHash<unsigned, bool> hashMethods;
        bool one = false, oneFile = false, operated = false, focused = false;
        bool focusedFile = false, folderSize = false, selectedParent = false;
        bool rows = false, foldersHistory = false;
    };
    TransportCapabilities ReadTransportCapabilities();
    bool TransportAllows(unsigned id, const TransportCapabilities &capabilities) const;
    QHash<unsigned, bool> Availability();
    enum class ActionRefresh { Idle, Publishing, Pending };
    void PublishActions();
    void AttachFileActions();
    void AttachPanelActions();
    void AttachViewActions();
    void Bind(QAction *action, unsigned id, bool viewContext = false);
    void InvokeViewCommand(unsigned id);
    std::optional<unsigned> CapturedViewPanel() const;
    void OpenNative(bool internal, bool external, const wchar_t *type);
    void RunNativeOpen(const std::function<void(CPanel &)> &open);
    MainWindow &window_;
    struct FolderDraft {
        QPointer<QtInlineFolderName> editor;
        CMyComPtr<IFolderFolder> folder;
        UString prefix;
        struct Submission {
            QString name;
            quint64 interaction;
        };
        std::optional<Submission> pending = std::nullopt;
    };
    QHash<unsigned, FolderDraft> folderDrafts_;
    QHash<unsigned, QList<QAction *>> actions_;
    std::optional<QPointer<CPanel>> viewMenuSource_;
    quint64 viewMenuRequest_ = 0;
    ActionRefresh actionRefresh_ = ActionRefresh::Idle;
};
