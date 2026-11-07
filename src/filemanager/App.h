// Adapted from 7-Zip CPP/7zip/UI/FileManager/App.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "filemanager/state/qtpanelslots.h"

#include "filemanager/AppState.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/panelmenuids.h"
#include "filemanager/state/qtappoperationstate.h"
#include "settings/RegistryUtils.h"
#include <memory>

const unsigned kNumPanelsMax = 2;
const int kMenuCmdID_Toolbar_Start = 1070;
enum {
    kMenuCmdID_Toolbar_Add = kMenuCmdID_Toolbar_Start,
    kMenuCmdID_Toolbar_Extract,
    kMenuCmdID_Toolbar_Test,
    kMenuCmdID_Toolbar_End
};
class IQtFileManagerMenuCommandServices;

struct QtArchiveTransferResult {
    HRESULT Result;
    QString Error;
};

void SetButtonText(int commandID, UString &s);

class CApp;
class CDropTarget;

class CPanelCallbackImp final : public CPanelCallback {
    CApp *_app = nullptr;
    unsigned _index = 0;

  public:
    void Init(CApp *app, unsigned index) {
        _app = app;
        _index = index;
    }
    void OnTab() override;
    void SetFocusToPath(unsigned index) override;
    void OnCopy(bool move, bool copyToSame) override;
    void OnSetSameFolder() override;
    void OnSetSubFolder() override;
    void PanelWasFocused() override;
    void DragBegin() override;
    void DragEnd() override;
    void RefreshTitle(bool always) override;
};

enum class QtCopyRoute { FileSystem, DirectFileSystem, DestinationPanel, StagedDestinationPanel };

// Qt replaces menu/toolbar handles and panel child-control construction. Native
// creation, path binding, per-panel state and release order remain CApp/CPanel.
class IQtAppPresentation {
  public:
    virtual ~IQtAppPresentation() = default;
    virtual void Initialize(CApp &app) = 0;
    virtual bool CreatePanel(CPanel &panel, unsigned index) = 0;
    virtual CPanelCallback *PanelCallback(unsigned index) = 0;
    virtual bool AllowPanelFocusChange(unsigned) const { return true; }
    virtual void PrepareFolderChange(unsigned) {}
    virtual void ExecuteCopy(bool, bool, unsigned, const QString &, const CRecordVector<UInt32> &,
                             QtCopyRoute) {}
    virtual void SetListSettings(unsigned index, const CFmSettings &settings) = 0;
    virtual void ShowPanel(unsigned index, bool visible) = 0;
    virtual void MoveSubWindows() {}
};

class CApp {
  public:
    CApp();
    ~CApp();
    unsigned NumPanels = 2;
    unsigned LastFocusedPanel = 0;
    CAppState AppState;
    CPanelCallbackImp m_PanelCallbackImp[kNumPanelsMax];
    QtPanelSlots Panels;

    HRESULT Create(QWidget *window, const UString &mainPath, const UString &arcFormat,
                   int xSizes[2], bool needOpenArc, COpenResult &openRes,
                   IQtAppPresentation &presentation, const QString &settingsPath);
    HRESULT CreateOnePanel(unsigned panelIndex, const UString &mainPath, const UString &arcFormat,
                           bool needOpenArc, COpenResult &openRes);
    void SetListSettings();
    void RefreshView() { GetFocusedPanel().OnReload(); }
    void RefreshAllPanels() {
        for (unsigned i = 0; i < NumPanels; i++) {
            unsigned index = i;
            if (NumPanels == 1)
                index = LastFocusedPanel;
            Panels[index].OnReload();
        }
    }
    void MoveSubWindows();
    HRESULT SwitchOnOffOnePanel();
    void SetFocusedPanel(unsigned index);
    void NotifyPanelFocus(unsigned index);
    UString PrevTitle;
    void RefreshTitle(bool always = false);
    void RefreshTitlePanel(unsigned index, bool always = false);
    // Existing Qt command consumers remain behind this routing boundary until
    // their full application command bodies are ported in P0-03/P0-08.
    void OnCopy(bool move, bool copyToSame, unsigned index);
    void CompleteCopy();
    bool RefreshCopyPanel(unsigned index);
    QtArchiveTransferResult CopyArchiveToArchive(unsigned srcPanelIndex,
                                                 const CRecordVector<UInt32> &indices,
                                                 UStringVector *messages);
    void OnSetSameFolder(unsigned index);
    void OnSetSubFolder(unsigned index);
    void DragBegin(unsigned index);
    void DragEnd();
    void CreateDragTarget();
    CDropTarget &DropTarget() { return *_dropTargetSpec; }
    void Save();
    void ReleaseApp();
    void AttachQtCommandServices(IQtFileManagerMenuCommandServices *services) {
        _qtCommandServices = services;
    }
    bool ExecuteCommand(unsigned id);
    void RefreshTitleAlways() { RefreshTitle(true); }
    HRESULT CalculateCrc2(const UString &methodName,
                          const CExternalCodecs *externalCodecs = nullptr);
    void CalculateCrc(const char *methodName);
    QtAppOperationState &QtOperations() { return _qtOperations; }
    const QtAppOperationState &QtOperations() const { return _qtOperations; }

    unsigned GetFocusedPanelIndex() const { return LastFocusedPanel; }
    bool IsPanelVisible(unsigned index) const { return NumPanels > 1 || index == LastFocusedPanel; }
    CPanel &GetFocusedPanel() Z7_lifetimebound { return Panels[GetFocusedPanelIndex()]; }
    UInt32 GetListViewMode() { return GetFocusedPanel().GetListViewMode(); }
    void SetListViewMode(UInt32 index) { GetFocusedPanel().SetListViewMode(index); }
    PROPID GetSortID() { return GetFocusedPanel().GetSortID(); }
    void ExtractArchives(IQtCompressCallServices &services) {
        GetFocusedPanel().ExtractArchives(services);
    }
    void TestArchives(IQtCompressCallServices *services = nullptr) {
        GetFocusedPanel().TestArchives(services);
    }
    void OpenRootFolder() { GetFocusedPanel().OpenDrivesFolder(); }
    void SetFocusToLastItem() { Panels[LastFocusedPanel].SetFocusToLastRememberedItem(); }

  private:
    std::unique_ptr<CDropTarget> _dropTargetSpec;
    QtAppOperationState _qtOperations;
    IQtFileManagerMenuCommandServices *_qtCommandServices = nullptr;
    QPointer<QWidget> _window;
    IQtAppPresentation *_presentation = nullptr;
    QString _settingsPath;
    struct CopyRequest;
    std::shared_ptr<const CopyRequest> _copyRequest;
};
