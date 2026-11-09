#pragma once

#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include "filemanager/panels/qtaddresssegment.h"
#include <QComboBox>
#include <QPointer>
#include <functional>
#include <optional>
#include <vector>

class CPanel;
class QLineEdit;

struct QtAddressDraft {
    explicit QtAddressDraft(const QLineEdit &edit);
    void Restore(QLineEdit &edit) const;

    QString text;

  private:
    int cursor_;
    int anchor_;
    bool modified_;
};

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
    virtual void DiscardDraft(const UString &path) = 0;
    virtual HRESULT BindToPathAndRefresh(const UString &path, bool sourcePath) = 0;
    virtual void SetFocusToList() = 0;
};

class QtPanelAddressCombo final : public QComboBox, public IQtPanelAddressCombo {
  public:
    QtPanelAddressCombo(CPanel &panel, std::function<HRESULT(const QString &, bool)> navigate,
                        QWidget *parent, std::function<void()> focusList = {});
    ~QtPanelAddressCombo() override;
    void showPopup() override;
    void hidePopup() override;
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
    const std::vector<QtAddressSegment> &LocationSegments();
    void GetLogicalDriveStrings(FStringVector &paths) override;
    UString GetText() const override;
    void SetText(const UString &text) override;
    void DiscardDraft(const UString &path) override;
    HRESULT BindToPathAndRefresh(const UString &path, bool sourcePath) override;
    void SetFocusToList() override;
    void CancelPendingListFocus() { listFocusPending_ = false; }
    quint64 UserEditRevision() const { return userEditRevision_; }
    const std::optional<QtAddressDraft> &LastUserDraft() const { return lastUserDraft_; }

  private:
    void CompleteListFocus();
    bool eventFilter(QObject *object, QEvent *event) override;
    QPointer<CPanel> panel_;
    std::function<HRESULT(const QString &, bool)> navigate_;
    std::function<void()> focusList_;
    QAction *currentIcon_ = nullptr;
    bool listFocusPending_ = false;
    quint64 userEditRevision_ = 0;
    std::optional<QtAddressDraft> lastUserDraft_;
    std::optional<QtAddressDraft> popupDraft_;
    UString displayedFolderPrefix_;
    std::optional<QString> displayedPath_;
    UString segmentsPath_;
    UInt64 segmentsRevision_ = UInt64(-1);
    std::vector<QtAddressSegment> segments_;
    // Disconnect queued/signal callbacks before state and base widgets disappear.
    QObject callbacks_;
};
