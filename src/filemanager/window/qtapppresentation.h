#pragma once
#include "filemanager/App.h"
#include <optional>

class MainWindow;
class QMenu;
class QWidget;

// Qt child controls and legacy settings migration for the native app owner.
class QtAppPresentation final : public IQtAppPresentation {
  public:
    explicit QtAppPresentation(MainWindow &window) : window_(window) {}
    void Initialize(CApp &app) override;
    bool CreatePanel(CPanel &panel, unsigned index) override;
    CPanelCallback *PanelCallback(unsigned index) override;
    bool AllowPanelFocusChange(unsigned index) const override;
    void PrepareFolderChange(unsigned index) override;
    bool OpenFolderInPeer(unsigned index, std::optional<UInt32> item) override;
    bool OpenBoundFolderInPeer(unsigned source, IFolderFolder *folder,
                               const UString &fallbackPath) override;
    void ExecuteCopy(bool move, bool copyToSame, unsigned sourceIndex, const QString &destination,
                     const CRecordVector<UInt32> &indices, QtCopyRoute route) override;
    void SetListSettings(unsigned index, const CFmSettings &settings) override;
    void ShowPanel(unsigned index, bool visible) override;
    void MoveSubWindows() override;
    void CopyFinished(bool preserveSourceSelection = false);
    void CompleteCopyIfReady();
    void CopyDestinationRefreshed(unsigned index);
    void RefreshCopyPanel(unsigned index);
    void CopySourceRefreshed(unsigned index);
    bool IsCopyPending() const { return copy_.has_value(); }
    void RequestInitialFocus(unsigned index);
    void RequestListFocus(unsigned index);
    void RestoreWidgetFocus(QWidget *widget);
    void CompleteInitialFocusIfReady();
    void CancelInitialFocus() { initialFocus_.reset(); }

  private:
    MainWindow &window_;
    QMenu *editMenu_ = nullptr;
    std::optional<unsigned> initialFocus_;
    struct CopyContinuation {
        CopyContinuation(unsigned index, bool move, bool copyToSame)
            : source(index), refreshSource(move || copyToSame), refreshDestination(!copyToSame) {}
        const unsigned source;
        bool executing = false;
        bool finished = false;
        bool preserveSourceSelection = false;
        bool refreshSource;
        bool refreshDestination;
    };
    std::optional<CopyContinuation> copy_;
};
