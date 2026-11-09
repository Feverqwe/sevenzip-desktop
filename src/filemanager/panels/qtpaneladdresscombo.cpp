#include "filemanager/panels/qtpaneladdresscombo.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileName.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/RootFolder.h"
#include "fileops/volumes/qtvolumemounts.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <utility>

namespace {
constexpr int kIndentRole = Qt::UserRole + 12;
class AddressItemDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem indented(option);
        indented.rect.adjust(index.data(kIndentRole).toInt() * option.fontMetrics.height(), 0, 0,
                             0);
        QStyledItemDelegate::paint(painter, indented, index);
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.rwidth() += index.data(kIndentRole).toInt() * option.fontMetrics.height();
        return size;
    }
};
} // namespace

QtPanelAddressCombo::QtPanelAddressCombo(CPanel &panel,
                                         std::function<HRESULT(const QString &, bool)> navigate,
                                         QWidget *parent, std::function<void()> focusList)
    : QComboBox(parent), panel_(&panel), navigate_(std::move(navigate)),
      focusList_(std::move(focusList)) {
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
    setEditable(true);
    // Source address edits are submitted only by Return or a popup choice.
    // Qt's implicit label matching on blur can activate a second hierarchy row.
    disconnect(lineEdit(), SIGNAL(editingFinished()), nullptr, nullptr);
    currentIcon_ = lineEdit()->addAction(QIcon(), QLineEdit::LeadingPosition);
    currentIcon_->setObjectName("currentAddressIcon");
    lineEdit()->installEventFilter(this);
    setItemDelegate(new AddressItemDelegate(this));
    view()->installEventFilter(this);
    connect(this, &QComboBox::activated, &callbacks_, [this](int) {
        if (panel_)
            panel_->OnComboBoxCommand(QtAddressCommand::SelectionAccepted, *this);
    });
    connect(lineEdit(), &QLineEdit::returnPressed, &callbacks_, [this] {
        if (panel_)
            panel_->OnNotifyComboBoxEndEdit(QtAddressEditReason::Return, *this);
    });
    connect(lineEdit(), &QLineEdit::textEdited, &callbacks_, [this] {
        ++userEditRevision_;
        lastUserDraft_.emplace(*lineEdit());
    });
    const auto updateSelection = [this] {
        if (lastUserDraft_ && lastUserDraft_->text == lineEdit()->text())
            lastUserDraft_.emplace(*lineEdit());
    };
    connect(lineEdit(), &QLineEdit::selectionChanged, &callbacks_, updateSelection);
    connect(lineEdit(), &QLineEdit::cursorPositionChanged, &callbacks_, updateSelection);
}

QtPanelAddressCombo::~QtPanelAddressCombo() {
    if (lineEdit())
        lineEdit()->removeEventFilter(this);
    view()->removeEventFilter(this);
}

void QtPanelAddressCombo::showPopup() {
    if (!panel_)
        return;
    CancelPendingListFocus();
    const QtAddressDraft draft(*lineEdit());
    {
        const QSignalBlocker blocker(this);
        panel_->LoadFullPath();
        panel_->OnComboBoxCommand(QtAddressCommand::DropDown, *this);
        setCurrentIndex(-1);
        draft.Restore(*lineEdit());
    }
    popupDraft_ = draft;
    QComboBox::showPopup();
}

void QtPanelAddressCombo::hidePopup() {
    const auto draft = std::exchange(popupDraft_, std::nullopt);
    QComboBox::hidePopup();
    if (draft && draft->text == lineEdit()->text())
        draft->Restore(*lineEdit());
}

QSize QtPanelAddressCombo::sizeHint() const {
    QSize size = QComboBox::sizeHint();
    size.setHeight(qMax(size.height(), qMax(34, fontMetrics().height() + 16)));
    return size;
}

QSize QtPanelAddressCombo::minimumSizeHint() const {
    QSize size = QComboBox::minimumSizeHint();
    size.setHeight(qMax(size.height(), qMax(34, fontMetrics().height() + 16)));
    return size;
}

void QtPanelAddressCombo::ResetContent() { clear(); }
void QtPanelAddressCombo::InsertItem(const UString &name, int iconIndex, unsigned indent,
                                     UInt32 namespaceID) {
    QIcon icon = g_Ext_to_Icon_Map.GetIcon(iconIndex);
    if (namespaceID) {
#ifdef _WIN32
        icon = g_Ext_to_Icon_Map.GetSystemIcon(iconIndex);
        if (icon.isNull())
#endif
        {
            const QStyle::StandardPixmap kind =
                namespaceID == IDS_COMPUTER    ? QStyle::SP_ComputerIcon
                : namespaceID == IDS_DOCUMENTS ? QStyle::SP_DirHomeIcon
                : namespaceID == IDS_NETWORK   ? QStyle::SP_DriveNetIcon
                                               : QStyle::SP_DriveHDIcon;
            icon = style()->standardIcon(kind);
        }
    }
    addItem(icon, name.IsEmpty() ? QStringLiteral("/") : ToQString(name));
    setItemData(count() - 1, indent, kIndentRole);
}
int QtPanelAddressCombo::GetCurSel() const { return currentIndex(); }
void QtPanelAddressCombo::SetCurSel(int index) {
    const QString text = currentText();
    setCurrentIndex(index);
    lineEdit()->setText(text);
}
int QtPanelAddressCombo::GetIconIndex(const UString &path, DWORD attributes) {
    return g_Ext_to_Icon_Map.GetIconIndex(attributes, path);
}
int QtPanelAddressCombo::GetPathIconIndex(const UString &path, DWORD attributes) {
    return g_Ext_to_Icon_Map.GetPathIconIndex(path, attributes);
}
int QtPanelAddressCombo::GetNamespaceIconIndex(UInt32 namespaceID) {
#ifdef _WIN32
    int nativeIndex = -1;
    if (namespaceID == IDS_COMPUTER)
        RootFolder_GetName_Computer(nativeIndex);
    else
        RootFolder_GetName_Network(nativeIndex);
    const QIcon native = g_Ext_to_Icon_Map.GetSystemIcon(nativeIndex);
    if (!native.isNull())
        return g_Ext_to_Icon_Map.GetPlatformIconIndex(
            QStringLiteral("shell-namespace:") + QString::number(nativeIndex), native);
#endif
    const auto kind =
        namespaceID == IDS_COMPUTER ? QStyle::SP_ComputerIcon : QStyle::SP_DriveNetIcon;
    return g_Ext_to_Icon_Map.GetPlatformIconIndex(QString::number(kind),
                                                  style()->standardIcon(kind));
}
void QtPanelAddressCombo::SetCurrentIcon(int index) {
    currentIcon_->setIcon(g_Ext_to_Icon_Map.GetIcon(index));
    currentIcon_->setVisible(index >= 0);
}
void QtPanelAddressCombo::ShowCurrentPath(const QString &displayPath) {
    if (panel_)
        panel_->LoadFullPathAndShow(*this, ToUString(displayPath));
}
void QtPanelAddressCombo::ReloadCurrentPath() {
    if (!panel_)
        return;
    const QString previousPrefix = ToQString(panel_->GetCurrentFolderPrefix());
    const bool editing = currentText() != previousPrefix;
    panel_->LoadFullPath();
    if (editing)
        panel_->LoadFullPathAndShow(*this, std::nullopt);
    else
        ShowCurrentPath(ToQString(panel_->GetCurrentFolderPrefix()));
}
const std::vector<QtAddressSegment> &QtPanelAddressCombo::LocationSegments() {
    if (panel_) {
        panel_->LoadFullPath();
        const UString &path = panel_->GetCurrentFolderPrefix();
        if (segmentsPath_ != path || segmentsRevision_ != panel_->FolderBindingRevision()) {
            segmentsPath_ = path;
            segmentsRevision_ = panel_->FolderBindingRevision();
            segments_ = panel_->AddressAncestors(*this);
        }
    }
    return segments_;
}
void QtPanelAddressCombo::GetLogicalDriveStrings(FStringVector &paths) {
#ifdef _WIN32
    NWindows::NFile::NFind::MyGetLogicalDriveStrings(paths);
#else
    paths.Clear();
    for (const auto &mount : QtSystemVolumeMounts()) {
        UString path = ToUString(mount.path);
        NWindows::NFile::NName::NormalizeDirPathPrefix(path);
        paths.Add(us2fs(path));
    }
#endif
}
UString QtPanelAddressCombo::GetText() const { return ToUString(currentText()); }
void QtPanelAddressCombo::SetText(const UString &text) { lineEdit()->setText(ToQString(text)); }
void QtPanelAddressCombo::DiscardDraft(const UString &path) {
    ++userEditRevision_;
    lastUserDraft_.reset();
    SetText(path);
}
QtAddressDraft::QtAddressDraft(const QLineEdit &edit)
    : text(edit.text()), cursor_(edit.cursorPosition()), anchor_(cursor_),
      modified_(edit.isModified()) {
    if (edit.hasSelectedText())
        anchor_ = edit.selectionStart() == cursor_ ? edit.selectionEnd() : edit.selectionStart();
}

void QtAddressDraft::Restore(QLineEdit &edit) const {
    if (edit.text() != text)
        edit.setText(text);
    edit.setModified(modified_);
    if (anchor_ == cursor_)
        edit.setCursorPosition(cursor_);
    else
        edit.setSelection(anchor_, cursor_ - anchor_);
}

HRESULT QtPanelAddressCombo::BindToPathAndRefresh(const UString &path, bool sourcePath) {
    const QtAddressDraft draft(*lineEdit());
    const HRESULT result =
        panel_ ? navigate_(ToQString(CPanel::PrepareBindPath(path)), sourcePath) : E_ABORT;
    if (result != S_OK) {
        if (window()->isActiveWindow())
            lineEdit()->setFocus();
        draft.Restore(*lineEdit());
    }
    return result;
}
void QtPanelAddressCombo::SetFocusToList() {
    if (!panel_)
        return;
    listFocusPending_ = true;
    QTimer::singleShot(0, &callbacks_, [this] { CompleteListFocus(); });
}

void QtPanelAddressCombo::CompleteListFocus() {
    // Source navigation can process events before the selection popup closes.
    // Its Hide notification completes delivery after Qt restores combo focus.
    if (!panel_ || !listFocusPending_ || view()->isVisible())
        return;
    listFocusPending_ = false;
    // Deliver only while the address owns focus: a newer same-pane interaction
    // may have opened a list editor. Do not activate an inactive window.
    QWidget *focus = QApplication::focusWidget();
    if (!focus)
        focus = window()->focusWidget();
    if (focus && (focus == this || isAncestorOf(focus))) {
        if (focusList_)
            focusList_();
        else
            panel_->SetFocusToList();
    }
}

bool QtPanelAddressCombo::eventFilter(QObject *object, QEvent *event) {
    if (!panel_)
        return QComboBox::eventFilter(object, event);
    if (object == view() && event->type() == QEvent::Hide && listFocusPending_)
        QTimer::singleShot(0, &callbacks_, [this] { CompleteListFocus(); });
    if (object == lineEdit()) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
            listFocusPending_ = false;
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            return panel_->OnNotifyComboBoxEndEdit(QtAddressEditReason::Escape, *this);
        }
    }
    return QComboBox::eventFilter(object, event);
}
