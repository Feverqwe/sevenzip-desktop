#include "mount/qtmountrow.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtresponsiverow.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QAction>
#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyleOptionFocusRect>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

QtMountRow::QtMountRow(const QIcon &icon, QWidget *parent) : QWidget(parent) {
    setObjectName("mountEntry");
    setFocusPolicy(Qt::StrongFocus);
    auto *body = new QVBoxLayout(this);
    body->setContentsMargins(0, 8, 0, 8);
    body->setSpacing(8);
    source_ = new QtSourceSummary(icon, {}, {}, this);
    source_->setObjectName("mountSourceSummary");
    body->addWidget(source_);
    error_ = new QtWrappingLabel(this);
    error_->setObjectName("mountError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    body->addWidget(error_);
    report_ = new QPlainTextEdit(this);
    report_->setObjectName("mountDetails");
    report_->setReadOnly(true);
    QtBindLocalText(report_, "QtArchiveMountManager",
                    QT_TRANSLATE_NOOP("QtArchiveMountManager", "Mount details"), true);
    report_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    report_->setMinimumHeight(fontMetrics().lineSpacing() * 3);
    report_->setMaximumHeight(fontMetrics().lineSpacing() * 8);
    details_ = new QtDisclosure(report_, this);
    QtBindLocalText(details_->button(), "QtArchiveMountManager",
                    QT_TRANSLATE_NOOP("QtArchiveMountManager", "Mount details"));
    body->addWidget(details_);
    const auto action = [this](Action id, const char *caption, const char *name) {
        auto *button = new QPushButton(this);
        button->setObjectName(name);
        button->setAutoDefault(false);
        QtBindLocalText(button, "QtArchiveMountManager", caption);
        connect(button, &QPushButton::clicked, this, [this, id] { emit Requested(id); });
        return button;
    };
    open_ = action(Action::Open, QT_TRANSLATE_NOOP("QtArchiveMountManager", "Open Folder"),
                   "openMountFolderButton");
    copy_ = action(Action::CopyPath, QT_TRANSLATE_NOOP("QtArchiveMountManager", "Copy Path"),
                   "copyMountPathButton");
    unmount_ = action(Action::Unmount, QT_TRANSLATE_NOOP("QtArchiveMountManager", "Unmount"),
                      "unmountButton");
    force_ =
        action(Action::ForceUnmount, QT_TRANSLATE_NOOP("QtArchiveMountManager", "Force Unmount…"),
               "forceUnmountButton");
    remove_ = action(Action::Remove, QT_TRANSLATE_NOOP("QtArchiveMountManager", "Remove from List"),
                     "removeMountButton");
    body->addWidget(new QtResponsiveRow({open_, copy_, unmount_}, this));
    body->addWidget(new QtResponsiveRow({force_, remove_}, this));
    removeAction_ = new QAction(this);
    removeAction_->setObjectName("removeMountAction");
    QtBindLocalText(removeAction_, "QtArchiveMountManager",
                    QT_TRANSLATE_NOOP("QtArchiveMountManager", "Remove from List"));
    addAction(removeAction_);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(removeAction_, &QAction::triggered, this, [this] { emit Requested(Action::Remove); });
    SetData({});
}

void QtMountRow::SetData(const QtMountRowData &data) {
    auto *focused = QApplication::focusWidget();
    const bool ownedFocus = focused && (focused == this || isAncestorOf(focused));
    source_->SetSource(data.name, data.sourcePath);
    source_->SetDescription(data.status);
    error_->setText(data.error);
    error_->setVisible(!data.error.isEmpty());
    if (report_->toPlainText() != data.report)
        report_->setPlainText(data.report);
    details_->setVisible(!data.report.isEmpty());
    open_->setEnabled(data.canOpen);
    open_->setToolTip(data.mountPath);
    copy_->setEnabled(data.canOpen);
    copy_->setToolTip(data.mountPath);
    unmount_->setEnabled(data.canUnmount);
    QtBindLocalText(unmount_, "QtArchiveMountManager",
                    data.preparing ? QT_TRANSLATE_NOOP("QtArchiveMountManager", "Cancel")
                                   : QT_TRANSLATE_NOOP("QtArchiveMountManager", "Unmount"));
    force_->setVisible(data.canForce);
    remove_->setVisible(data.canRemove);
    removeAction_->setEnabled(data.canRemove);
    if (ownedFocus && (!focused->isEnabled() || !focused->isVisible()))
        FocusPrimaryAction();
    setAccessibleName(data.name);
    updateGeometry();
}

void QtMountRow::FocusPrimaryAction() {
    for (auto *button : {open_, unmount_, remove_})
        if (button->isVisible() && button->isEnabled()) {
            button->setFocus(Qt::OtherFocusReason);
            return;
        }
    setFocus(Qt::OtherFocusReason);
}

void QtMountRow::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    if (hasFocus()) {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.rect = rect().adjusted(1, 1, -1, -1);
        QStylePainter painter(this);
        painter.drawPrimitive(QStyle::PE_FrameFocusRect, option);
    }
}

int QtMountRow::heightForWidth(int width) const { return layout()->totalHeightForWidth(width); }
