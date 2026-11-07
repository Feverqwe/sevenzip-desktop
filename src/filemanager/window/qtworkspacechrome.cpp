#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtoperationsbutton.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtmenutext.h"
#include "ui/theme/qtcommandicons.h"
#include <QScopedValueRollback>

namespace {
void RefreshCommandCaption(QToolBar &toolbar, QAction &proxy, const QAction &source) {
    proxy.setText(QString(source.text()).remove('&'));
    const QString shortcut = source.shortcut().toString(QKeySequence::NativeText);
    proxy.setToolTip(shortcut.isEmpty() ? proxy.text()
                                        : QStringLiteral("%1 (%2)").arg(proxy.text(), shortcut));
    if (auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(&proxy))) {
        button->setAccessibleName(proxy.text());
        button->setToolTip(proxy.toolTip());
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoRaise(true);
        QtConfigureMainToolbarButton(*button);
    }
}
} // namespace

void QtWindowWorkspace::CreateActions(unsigned panel, QVBoxLayout &layout) {
    auto &state = panels_[panel];
    state.actions = new QToolBar;
    QtConfigureMainToolbar(*state.actions);
    state.actions->setObjectName(QStringLiteral("panelFileActions%1").arg(panel));
    state.actions->setIconSize(QSize(16, 16));
    state.actions->setToolButtonStyle(Qt::ToolButtonIconOnly);
    state.actions->setMovable(false);
    const auto add = [this, panel, &state](QAction *source, QtCommandIcon icon) {
        auto *action =
            state.actions->addAction(QtCommandSymbol(icon, state.actions), source->text());
        action->setProperty("sourceAction", QVariant::fromValue(source));
        connect(source, &QAction::changed, action, [toolbar = state.actions, action, source] {
            RefreshCommandCaption(*toolbar, *action, *source);
        });
        connect(action, &QAction::triggered, this, [this, panel, source] {
            window_.activatePanelUi(panel);
            if (window_.projectedPanel_ == panel)
                source->trigger();
        });
    };
    add(window_.createFolder_, QtCommandIcon::NewFolder);
    add(window_.clipboard_->cut, QtCommandIcon::Cut);
    add(window_.clipboard_->copy, QtCommandIcon::Copy);
    add(window_.clipboard_->paste, QtCommandIcon::Paste);
    add(window_.rename_, QtCommandIcon::Rename);
    add(window_.remove_, QtCommandIcon::Delete);
    state.actionRow = new QWidget;
    auto *row = new QHBoxLayout(state.actionRow);
    row->setContentsMargins(0, 0, 8, 0);
    row->setSpacing(0);
    row->addWidget(state.actions, 1);
    row->addWidget(new QtOperationsButton(state.actionRow));
    layout.addWidget(state.actionRow);
}

void QtWindowWorkspace::AttachContent(unsigned panel, QVBoxLayout &layout) {
    auto &ui = window_.panelUi_[panel];
    ui.navigationNotice = new QWidget;
    auto *notice = new QHBoxLayout(ui.navigationNotice);
    notice->setContentsMargins(12, 6, 12, 6);
    ui.navigationError = new QLabel(ui.navigationNotice);
    ui.navigationError->setTextFormat(Qt::PlainText);
    ui.navigationError->setWordWrap(true);
    notice->addWidget(ui.navigationError, 1);
    auto *retry = new QPushButton(ui.navigationNotice);
    retry->setObjectName(QStringLiteral("navigationRetry%1").arg(panel));
    QtBindLocalText(retry, "QtWindowWorkspace", QT_TRANSLATE_NOOP("QtWindowWorkspace", "Retry"));
    notice->addWidget(retry);
    auto *parent = new QPushButton(ui.navigationNotice);
    parent->setObjectName(QStringLiteral("navigationParent%1").arg(panel));
    QtBindLocalText(parent, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Parent folder"));
    notice->addWidget(parent);
    connect(retry, &QPushButton::clicked, this, [this, panel] { RetryLocation(panel, false); });
    connect(parent, &QPushButton::clicked, this, [this, panel] { RetryLocation(panel, true); });
    layout.addWidget(ui.navigationNotice);
    ui.navigationNotice->hide();
    layout.addWidget(window_.panelUi_[panel].viewStack, 1);
    if (!window_.findChild<QMenu *>("savedPlacesMenu")) {
        auto *favorites = window_.findChild<QMenu *>("favoritesMenu");
        auto *add = new QAction(tr("Add current location"), favorites);
        add->setObjectName("addFavoriteAction");
        QtBindLocalText(add, "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Add current location"));
        favorites->insertAction(favorites->actions().first(), add);
        connect(add, &QAction::triggered, this, &QtWindowWorkspace::AddFavorite);
        auto *saved = favorites->addMenu(tr("Saved places"));
        saved->setObjectName("savedPlacesMenu");
        saved->menuAction()->setProperty("nativeCommandID", 1 << 16);
        QtBindLocalText(saved->menuAction(), "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Saved places"));
        connect(saved, &QMenu::aboutToShow, this, [this, saved] {
            saved->clear();
            for (int row = 0; row < favorites_->rowCount(); ++row) {
                const auto entry = favorites_->Favorite(row);
                auto *action = saved->addAction(QtLiteralMenuText(entry.label));
                action->setToolTip(entry.path);
                connect(action, &QAction::triggered, this, [this, id = entry.id] {
                    const auto index = favorites_->IndexForId(id);
                    if (index.isValid())
                        OpenFavorite(favorites_->Favorite(index.row()));
                });
            }
        });
    }
}

void QtWindowWorkspace::RefreshChrome() {
    for (unsigned panel = 0; panel < 2; ++panel) {
        const auto &state = panels_[panel];
        if (!state.actions || !window_.commands_ || !window_.panelUi_[panel].table)
            continue;
        if (state.location.archiveRevision != state.location.archiveApplied)
            ShowArchiveChangeNotice(panel);
        if (window_.panelUi_[panel].navigationNotice)
            window_.panelUi_[panel].navigationNotice->setVisible(
                window_.panelUi_[panel].locationUnavailable ||
                state.location.archiveRevision != state.location.archiveApplied);
        if (auto *row =
                dynamic_cast<QtNavigationRow *>(window_.panelUi_[panel].navigation->parentWidget()))
            row->SetActivePane(window_.app_.NumPanels == 2 && window_.projectedPanel_ == panel);
        state.actions->setToolButtonStyle(window_.app_.NumPanels == 1 && window_.width() > 800
                                              ? Qt::ToolButtonTextBesideIcon
                                              : Qt::ToolButtonIconOnly);
        if (auto *operations = dynamic_cast<QtOperationsButton *>(
                state.actionRow->findChild<QToolButton *>("operationsButton")))
            operations->SetCompact(window_.app_.NumPanels == 2);
        const auto native = window_.commands_->AvailabilityForPanel(panel);
        const auto clipboard = window_.clipboard_->AvailabilityForPanel(panel);
        auto *container = window_.panelUi_[panel].container;
        if (auto *button = container->findChild<QAbstractButton *>(
                QStringLiteral("panelParentButton%1").arg(panel)))
            button->setEnabled(native.value(window_.up_->property("nativeCommandID").toUInt()));
        if (auto *button =
                container->findChild<QToolButton *>(QStringLiteral("panelBackButton%1").arg(panel)))
            button->setEnabled(native.value(window_.back_->property("nativeCommandID").toUInt()));
        if (auto *button = container->findChild<QToolButton *>(
                QStringLiteral("panelForwardButton%1").arg(panel)))
            button->setEnabled(
                native.value(window_.forward_->property("nativeCommandID").toUInt()));
        for (auto *action : state.actions->actions()) {
            auto *source = action->property("sourceAction").value<QAction *>();
            if (!source)
                continue;
            const auto id = source->property("nativeCommandID");
            const bool enabled = source == window_.clipboard_->cut    ? clipboard.cut
                                 : source == window_.clipboard_->copy ? clipboard.copy
                                 : source == window_.clipboard_->paste
                                     ? clipboard.paste
                                     : native.value(id.toUInt(), false);
            action->setEnabled(enabled);
            RefreshCommandCaption(*state.actions, *action, *source);
        }
    }
}

void QtWindowWorkspace::AddFavorite() {
    if (!Ready())
        return;
    const QString path = CurrentLocation();
    const auto &location = panels_[window_.projectedPanel_].location;
    const auto nativeIdentity = location.nativePlaceIdentity;
    const auto archive = location.archiveLocation;
    const QString defaultName = path.isEmpty() && nativeIdentity == "locations"
                                    ? tr("Locations")
                                    : QFileInfo(QDir::cleanPath(path)).fileName();
    bool accepted = false;
    const QString label = QInputDialog::getText(&window_, tr("Add favorite"), tr("Name"),
                                                QLineEdit::Normal, defaultName, &accepted);
    if (accepted && !label.trimmed().isEmpty())
        favorites_->Add(path, label.trimmed(), nativeIdentity, archive);
}

void QtWindowWorkspace::OpenFavorite(const QtFavorite &favorite) {
    if (!Ready())
        return;
    const unsigned panel = window_.projectedPanel_;
    const auto previous =
        !window_.projectedUi().locationUnavailable && window_.app_.Panels[panel].GetFolder()
            ? std::optional(CaptureVisit(panel))
            : std::nullopt;
    Navigate(
        [&] {
            return OpenRecordedLocation(panel, favorite.path, favorite.nativeIdentity,
                                        favorite.archive);
        },
        [this, panel, favorite, previous](HRESULT result) {
            if (result == S_OK || window_.app_.QtOperations().closed())
                return;
            auto &location = panels_[panel].location;
            if (previous) {
                if (location.cursor < 0) {
                    location.visits.push_back(*previous);
                    location.cursor = 0;
                } else if (location.visits[location.cursor].SameLocation(*previous))
                    location.visits[location.cursor] = *previous;
            }
            location.nativePlaceIdentity = favorite.nativeIdentity;
            location.archiveLocation = favorite.archive;
            CompleteLocationOpen(panel, favorite.path, result);
            window_.refreshActions();
        });
}
