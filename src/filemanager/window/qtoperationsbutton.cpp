#include "filemanager/window/qtoperationsbutton.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "operations/qtoperationregistry.h"
#include "ui/controls/qtmenutext.h"
#include "ui/theme/qtcommandicons.h"
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QMenu>

namespace {
QString Text(const char *source) {
    return QCoreApplication::translate("QtOperationsButton", source);
}
QString OperationName(QtOperationJob::Command command) {
    using Command = QtOperationJob::Command;
    switch (command) {
    case Command::Test:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Test archive"));
    case Command::Extract:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Extract"));
    case Command::Compress:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Create archive"));
    case Command::Hash:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Checksums"));
    }
    return {};
}
QString Status(const QtOperationListing &entry) {
    if (entry.Result)
        return Text(*entry.Result == QtOperationResult::Outcome::Success
                        ? QT_TRANSLATE_NOOP("QtOperationsButton", "Done")
                        : QT_TRANSLATE_NOOP("QtOperationsButton", "Review result"));
    switch (entry.Presentation.Phase) {
    case QtOperationPhase::Preparing:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Preparing"));
    case QtOperationPhase::Draft:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Options"));
    case QtOperationPhase::Working:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Working"));
    case QtOperationPhase::Question:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Waiting for input"));
    case QtOperationPhase::Reviewing:
        return Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Review result"));
    }
    return {};
}
} // namespace

QtOperationsButton::QtOperationsButton(QWidget *parent)
    : QToolButton(parent), menu_(new QMenu(this)) {
    setObjectName("operationsButton");
    menu_->setObjectName("operationsMenu");
    menu_->setToolTipsVisible(true);
    setMenu(menu_);
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setIcon(QtCommandSymbol(QtCommandIcon::Operations, this));
    setAutoRaise(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QtConfigureChromeToolButton(*this);
    connect(menu_, &QMenu::aboutToShow, this, &QtOperationsButton::Populate);
    connect(&QtOperationRegistry::Application(), &QtOperationRegistry::changed, this,
            &QtOperationsButton::Refresh);
    Refresh();
}

void QtOperationsButton::Refresh() {
    const auto entries = QtOperationRegistry::Application().Listings();
    int active = 0;
    bool needsReview = false;
    for (const auto &entry : entries) {
        active += !entry.Result.has_value();
        needsReview |= entry.Result && *entry.Result != QtOperationResult::Outcome::Success;
    }
    const QString suffix = needsReview ? Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Results"))
                           : active    ? QString::number(active)
                                       : Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Done"));
    setText(Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Operations · %1")).arg(suffix));
    setAccessibleName(text());
    setToolTip(Text(QT_TRANSLATE_NOOP("QtOperationsButton", "Operations")));
    menu_->setTitle(toolTip());
    menu_->setAccessibleName(toolTip());
    setVisible(!entries.isEmpty());
}

void QtOperationsButton::SetCompact(bool compact) {
    setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
    if (compact)
        setFixedWidth(QtWorkspaceTheme::metric(*this, 32));
    else {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void QtOperationsButton::Populate() {
    menu_->clear();
    for (const auto &entry : QtOperationRegistry::Application().Listings()) {
        QString title = OperationName(entry.Operation);
        const QString &source = entry.Source;
        if (!source.isEmpty())
            title += " — " +
                     fontMetrics().elidedText(QFileInfo(source).fileName(), Qt::ElideMiddle, 240);
        auto *action =
            menu_->addAction(QtLiteralMenuText(title) + " · " + QtLiteralMenuText(Status(entry)));
        action->setData(entry.Id);
        action->setToolTip("<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
        action->setEnabled(!entry.Result || *entry.Result != QtOperationResult::Outcome::Success);
        connect(action, &QAction::triggered, this,
                [id = entry.Id] { QtOperationRegistry::Application().Show(id); });
    }
}

void QtOperationsButton::changeEvent(QEvent *event) {
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        Refresh();
}
