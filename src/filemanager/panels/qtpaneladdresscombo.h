#pragma once

#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include <QComboBox>
#include <QPointer>
#include <functional>

class CPanel;
enum class QtAddressCommand { DropDown, SelectionAccepted };
enum class QtAddressEditReason { Escape, Return };

// Control/platform services for the original PanelFolderChange address bodies.
class IQtPanelAddressCombo {
  public:
    virtual ~IQtPanelAddressCombo() = default;
    virtual void ResetContent() = 0;
    virtual void InsertItem(const UString &name, int iconIndex, unsigned indent,
                            UInt32 namespaceID) = 0;
    virtual int GetCurSel() const = 0;
    virtual void SetCurSel(int index) = 0;
    virtual int GetIconIndex(const UString &path, DWORD attributes) = 0;
    virtual int GetPathIconIndex(const UString &path, DWORD attributes) = 0;
    virtual int GetNamespaceIconIndex(UInt32 namespaceID) = 0;
    virtual void SetCurrentIcon(int index) = 0;
    virtual void GetLogicalDriveStrings(FStringVector &paths) = 0;
    virtual UString GetText() const = 0;
    virtual void SetText(const UString &text) = 0;
    virtual HRESULT BindToPathAndRefresh(const UString &path, bool sourcePath) = 0;
    virtual void SetFocusToList() = 0;
};

class QtPanelAddressCombo final : public QComboBox, public IQtPanelAddressCombo {
  public:
    QtPanelAddressCombo(CPanel &panel, std::function<HRESULT(const QString &, bool)> navigate,
                        QWidget *parent, std::function<void()> focusList = {});
    ~QtPanelAddressCombo() override;
    void showPopup() override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void ResetContent() override;
    void InsertItem(const UString &name, int iconIndex, unsigned indent,
                    UInt32 namespaceID) override;
    int GetCurSel() const override;
    void SetCurSel(int index) override;
    int GetIconIndex(const UString &path, DWORD attributes) override;
    int GetPathIconIndex(const UString &path, DWORD attributes) override;
    int GetNamespaceIconIndex(UInt32 namespaceID) override;
    void SetCurrentIcon(int index) override;
    void ShowCurrentPath(const QString &displayPath);
    void ReloadCurrentPath();
    void GetLogicalDriveStrings(FStringVector &paths) override;
    UString GetText() const override;
    void SetText(const UString &text) override;
    HRESULT BindToPathAndRefresh(const UString &path, bool sourcePath) override;
    void SetFocusToList() override;

  private:
    void CompleteListFocus();
    bool eventFilter(QObject *object, QEvent *event) override;
    QPointer<CPanel> panel_;
    std::function<HRESULT(const QString &, bool)> navigate_;
    std::function<void()> focusList_;
    QAction *currentIcon_ = nullptr;
    bool listFocusPending_ = false;
    // Disconnect queued/signal callbacks before state and base widgets disappear.
    QObject callbacks_;
};
