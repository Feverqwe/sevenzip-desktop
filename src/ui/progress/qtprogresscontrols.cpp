#include "ui/progress/qtprogresscontrols.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogunits.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <cmath>
#include <utility>

namespace {
void ConfigureQuestion(QDialog &dialog, QWidget *parent) {
    // Both independent tasks and source-owned window-modal progress keep
    // their nested questions scoped to that same owner on every platform.
    if (parent && parent->windowModality() != Qt::ApplicationModal)
        dialog.setWindowModality(Qt::WindowModal);
    else
        QtUseContextDialogModality(dialog);
}

class ProgressStopQuestion final : public QMessageBox {
    void refreshText() {
        setText(QCoreApplication::translate("ProgressRedesign", "Stop this operation?"));
        QtBindLocalText(button(QMessageBox::Yes), "ProgressRedesign",
                        QT_TRANSLATE_NOOP("ProgressRedesign", "Stop"));
        QtBindLocalText(button(QMessageBox::No), "ProgressRedesign",
                        QT_TRANSLATE_NOOP("ProgressRedesign", "Continue working"));
    }
    void changeEvent(QEvent *event) override {
        QMessageBox::changeEvent(event);
        if (event->type() == QEvent::LanguageChange) {
            refreshText();
            // QMessageBox also retranslates its standard button box. Restore
            // these operation-specific captions after that child notification.
            QTimer::singleShot(0, this, [this] { refreshText(); });
        }
    }

  public:
    ProgressStopQuestion(const UString &title, QWidget *parent)
        : QMessageBox(QMessageBox::NoIcon, QString::fromWCharArray(title.Ptr()), {},
                      QMessageBox::Yes | QMessageBox::No, parent) {
        setTextFormat(Qt::PlainText);
        refreshText();
    }
};
} // namespace

void CQtProgressBar::Attach(QWidget *widget) { _view = static_cast<QProgressBar *>(widget); }
void CQtProgressBar::SetRange32(int minimum, int maximum) { _view->setRange(minimum, maximum); }
void CQtProgressBar::SetPos(int position) {
    // PBM_SETPOS clamps to the nearest boundary; QProgressBar ignores an
    // out-of-range value. Preserve the source control service's behavior.
    _view->setValue(qBound(_view->minimum(), position, _view->maximum()));
}

void CQtProgressDialog::AddControl(unsigned id, const char *name, QWidget *widget) {
    widget->setObjectName(QString::fromLatin1(name));
    _controls.insert(id, widget);
}

CQtProgressDialog::CQtProgressDialog(QWidget *parent) : QDialog(parent) {
    QtTaskPresentation::Watch(*this, QtOperationPhase::Working);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setObjectName("progressDialog");
    const QtDialogUnits units(font());
    _unitX = units.horizontalScale();
    _clock.start();
    BuildPresentation();
    _initialSize = size();
}

void CQtProgressDialog::showEvent(QShowEvent *event) {
    const bool fit = std::exchange(_firstShow, false) && size() == _initialSize;
    QDialog::showEvent(event);
    if (fit)
        QtFitScrollableDialog(*this);
}

QWidget *CQtProgressDialog::GetItem(unsigned id) const { return _controls.value(id); }
qintptr CQtProgressDialog::CreateModal(QWidget *parent) {
    QtTaskPresentation::SetPhase(*this, QtOperationPhase::Working);
    if (parentWidget() != parent)
        setParent(parent, windowFlags());
    return exec();
}
void CQtProgressDialog::End(qintptr result) { done(int(result)); }
void CQtProgressDialog::SetDefaultButton(unsigned id) {
    auto *button = static_cast<QPushButton *>(GetItem(id));
    for (auto *other : findChildren<QPushButton *>())
        other->setDefault(false);
    button->setEnabled(true);
    button->setAutoDefault(true);
    button->setDefault(true);
    button->setFocus();
}
void CQtProgressDialog::MessageBox(QWidget *parent, const UString &message, const UString &title,
                                   bool error) {
    QMessageBox box(error ? QMessageBox::Critical : QMessageBox::NoIcon,
                    QString::fromWCharArray(title.Ptr()), QString::fromWCharArray(message.Ptr()),
                    QMessageBox::Ok, parent);
    box.setObjectName("progressFinalMessage");
    ConfigureQuestion(box, parent);
    QtTaskPresentation::Watch(box, QtOperationPhase::Reviewing);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}
int CQtProgressDialog::AskCancel(QWidget *parent, const UString &title) {
    ProgressStopQuestion box(title, parent);
    box.setObjectName("progressCancelQuestion");
    ConfigureQuestion(box, parent);
    QtTaskPresentation::Watch(box, QtOperationPhase::Question);
    box.setTextFormat(Qt::PlainText);
    box.setDefaultButton(QMessageBox::No);
    box.setEscapeButton(QMessageBox::No);
    _stopQuestion = &box;
    const int result = box.exec();
    _stopQuestion = nullptr;
    if (result == QMessageBox::Yes)
        return 6; // Native IDYES.
    if (result == QMessageBox::No)
        return 7; // Native IDNO.
    return 2;     // Native IDCANCEL if the owner dismisses the nested dialog.
}
void CQtProgressDialog::DismissStopQuestion() {
    if (_stopQuestion)
        _stopQuestion->done(QMessageBox::No);
}
#ifdef _WIN32
HWND CQtProgressDialog::GetNativeTaskbarWindow(QWidget *widget) {
    return widget ? reinterpret_cast<HWND>(widget->winId()) : nullptr;
}
#endif
void CQtProgressDialog::SetItemText(unsigned id, const UString &text) {
    QWidget *widget = GetItem(id);
    if (auto *label = qobject_cast<QLabel *>(widget)) {
        label->setText(ToQString(text));
        if (id == IDT_PROGRESS_STATUS || id == IDT_PROGRESS_FILE_NAME)
            label->setVisible(!text.IsEmpty());
    } else if (auto *button = qobject_cast<QPushButton *>(widget))
        button->setText(ToQString(text));
}
void CQtProgressDialog::GetItemText(unsigned id, UString &text) const {
    QWidget *widget = GetItem(id);
    if (auto *label = qobject_cast<QLabel *>(widget))
        text = ToUString(label->text());
    else if (auto *button = qobject_cast<QPushButton *>(widget))
        text = ToUString(button->text());
}
void CQtProgressDialog::SetText(const UString &text) { setWindowTitle(ToQString(text)); }
void CQtProgressDialog::ShowItem_Bool(unsigned id, bool visible) {
    if (auto *item = GetItem(id))
        item->setVisible(visible);
}
void CQtProgressDialog::HideItem(unsigned id) { ShowItem_Bool(id, false); }
void CQtProgressDialog::GetItemSizes(unsigned id, int &width, int &height) const {
    width = GetItem(id)->width();
    height = GetItem(id)->height();
}
void CQtProgressDialog::GetClientRectOfItem(unsigned id, CQtProgressRect &rect) const {
    const QRect geometry = GetItem(id)->geometry();
    rect = {geometry.x(), geometry.y(), geometry.x() + geometry.width(),
            geometry.y() + geometry.height()};
}
void CQtProgressDialog::MoveItem(unsigned id, int x, int y, int width, int height) {
    GetItem(id)->setGeometry(x, y, width, height);
}
int CQtProgressDialog::Units_To_Pixels_X(int units) const {
    return int(std::round(units * _unitX));
}
UInt32 CQtProgressDialog::GetTickCount() const { return UInt32(_clock.elapsed()); }
void CQtProgressDialog::ChangeSubWindowSizeX(QWidget *widget, int width) {
    widget->resize(width, widget->height());
}
void CQtProgressDialog::ClipboardSetText(const UString &text) {
    QApplication::clipboard()->setText(ToQString(text));
}
void CQtProgressDialog::NormalizeSize(bool fullNormalize) {
    QtNormalizeDialogSize(*this, fullNormalize);
}

QIcon CQtProgressDialog::LoadIcon(int id) {
    // Static-library resources need an explicit reference to their initializer.
    static const bool initialized = [] {
        Q_INIT_RESOURCE(upstream_progress_icons);
        return true;
    }();
    Q_UNUSED(initialized)
    if (id == IDI_ICON)
        return QIcon(QStringLiteral(":/upstream/7zip/UI/FileManager/FM.ico"));
    return {};
}
void CQtProgressDialog::SetIcon(const QIcon &icon) { setWindowIcon(icon); }
