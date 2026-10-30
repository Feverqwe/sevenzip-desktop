#pragma once
#include "filemanager/panels/qtpaneldragnative.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include <QObject>
#include <QPoint>
class MainWindow;
class QEvent;
class QtFileManagerDrag final : public QObject,
                                public IQtPanelDragServices,
                                public IQtDropTargetPresentation {
  public:
    explicit QtFileManagerDrag(MainWindow &window);
    ~QtFileManagerDrag() override;
    void StartDrag(unsigned panel, bool rightButton = false);
    void AttachPanel(unsigned panel);
    void Revoke();
    bool Active() const { return active_; }
    HRESULT DoDragDrop(CDataObject &, CDropSource &, DWORD, DWORD &) override;
    HRESULT CompressFiles(CPanel &, const UString &, const UString &, const UStringVector &,
                          bool) override;
    int PanelAtPoint(int x, int y) const override;
    bool PanelEnabled(unsigned panel) const override;
    int FolderAtPoint(unsigned panel, int x, int y, UString &name) const override;
    void Highlight(unsigned panel, int item, bool enabled) override;
    bool IsSameDrive(const UString &, const UStringVector &) const override;
    bool ConfirmCopyToArchive(CPanel &, DWORD) override;
    UInt32 Drag_OnContextMenu(int x, int y, UInt32 mask) override;

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    enum class DroppedPathAction { Open, Compress, Cancel };
    bool HandleDropEvent(QWidget &, QEvent &);
    HRESULT ExecuteDrop(CDataObject *, DWORD keys, const QPoint &position, DWORD &effect);
    QString PathToOpen(CDataObject *, DWORD keys) const;
    DroppedPathAction ChooseDroppedPathAction(const QString &path);
    CDataObject *ReadData(const QMimeData &);
    bool HandleRightDrag(QWidget &, QEvent &);
    void SuspendHover(bool suspended);
    MainWindow &window_;
    CDataObject external_;
#ifdef _WIN32
    QPointer<QtNativePanelDragConverter> nativeConverter_;
#endif
    bool active_ = false, revoked_ = false;
    bool menuDrag_ = false;
    QPointer<QWidget> rightPressWidget_;
    QPoint rightPressPosition_;
    bool contextDeferred_ = false, rightDragConsumed_ = false;
};
