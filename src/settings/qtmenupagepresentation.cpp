// Qt presentation for the native MenuPage state/apply owner.
#include "7zip/UI/FileManager/MenuPageRes.h"
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "localization/qtlangcontrols.h"
#include "platform/shell/qtmenupageservices.h"
#include "settings/MenuPage.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

void CMenuPage::BuildPresentation(bool zoneSupported) {
    setObjectName("menuPage");
    auto *layout = new QVBoxLayout(this);
    // MenuPage2.rc separates 10-DLU checkboxes by 4 DLU. Pixel spacing
    // compresses their native Cocoa widget frames into each other.
    layout->setSpacing(QtDialogUnits(font()).pixels(QSize(0, 4)).height());
    auto addCheck = [&](const QString &text, const char *name, unsigned id) {
        auto *check = new QCheckBox(text, this);
        check->setObjectName(name);
        check->setProperty("languageWrapWidth", 380);
        check->setProperty("sourceControlID", id);
        QtBindResourceText(check, id, text);
        check->setEnabled(false);
        QtBindLocalToolTip(
            check, "MenuPage",
            QT_TRANSLATE_NOOP("MenuPage", "Shell integration is unavailable in this build."));
        layout->addWidget(check);
        connect(check, &QCheckBox::clicked, this, [this, id] { OnButtonClicked(id); });
        return check;
    };
    addCheck("Integrate 7-Zip to shell context menu", "shellIntegration",
             IDX_SYSTEM_INTEGRATE_TO_MENU);
    addCheck("Integrate 7-Zip to shell context menu", "shellIntegration32",
             IDX_SYSTEM_INTEGRATE_TO_MENU_2);
    _cascaded = addCheck("Cascaded context menu", "cascadedMenu", IDX_SYSTEM_CASCADED_MENU);
    _menuIcons = addCheck("Icons in context menu", "menuIcons", IDX_SYSTEM_ICON_IN_MENU);
    _elimDup =
        addCheck("Eliminate duplication of root folder", "menuElimDup", IDX_EXTRACT_ELIM_DUP);
    const bool commandsAvailable = _services && _services->CommandsAvailable();
    for (auto *control : {_cascaded, _menuIcons, _elimDup}) {
        control->setEnabled(commandsAvailable);
        if (commandsAvailable)
            QtBindLocalToolTip(control);
    }
    auto *zoneRow = new QHBoxLayout;
    auto *zoneLabel = new QLabel("Propagate Zone.Id stream:", this);
    zoneLabel->setObjectName("menuZoneLabel");
    zoneLabel->setWordWrap(true);
    zoneLabel->setProperty("sourceControlID", IDT_SYSTEM_ZONE);
    QtBindResourceText(zoneLabel, IDT_SYSTEM_ZONE, "Propagate Zone.Id stream:");
    _zoneCombo = new QComboBox(this);
    _zoneCombo->setObjectName("menuZone");
    _zoneCombo->setProperty("sourceControlID", IDC_SYSTEM_ZONE);
    _zoneCombo->setEnabled(zoneSupported);
    _zoneCombo->setVisible(zoneSupported);
    zoneLabel->setVisible(zoneSupported);
    if (!zoneSupported)
        QtBindLocalToolTip(
            _zoneCombo, "MenuPage",
            QT_TRANSLATE_NOOP("MenuPage", "Zone.Identifier propagation is available on Windows."));
    zoneLabel->setBuddy(_zoneCombo);
    zoneRow->addWidget(zoneLabel, 1);
    zoneRow->addWidget(_zoneCombo);
    layout->addLayout(zoneRow);
    auto *itemsLabel = new QLabel("Context menu items:", this);
    itemsLabel->setObjectName("menuItemsLabel");
    itemsLabel->setProperty("sourceControlID", IDT_SYSTEM_CONTEXT_MENU_ITEMS);
    QtBindResourceText(itemsLabel, IDT_SYSTEM_CONTEXT_MENU_ITEMS, "Context menu items:");
    layout->addWidget(itemsLabel);
    _listView = new QListWidget(this);
    _listView->setObjectName("menuItems");
    _listView->setProperty("sourceControlID", IDL_SYSTEM_OPTIONS);
    _listView->setEnabled(commandsAvailable);
    QtBindLocalToolTip(
        _listView, "MenuPage",
        QT_TRANSLATE_NOOP("MenuPage", "Shell integration is unavailable in this build."));
    if (commandsAvailable)
        QtBindLocalToolTip(_listView);
    _listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(_listView, 1);
    connect(_zoneCombo, &QComboBox::currentIndexChanged, this,
            [this] { OnCommand(IDC_SYSTEM_ZONE); });
    connect(_listView, &QListWidget::itemChanged, this, &CMenuPage::OnItemChanged);
}
