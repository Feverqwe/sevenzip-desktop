#include "7zip/UI/FileManager/SettingsPageRes.h"
#include "localization/qtlangcontrols.h"
#include "settings/SettingsPage.h"
#include "ui/controls/qtmemorylimitspin.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

void CSettingsPage::BuildControls(bool embedded) {
    setObjectName(embedded ? "settingsPage" : "settingsDialog");
    if (embedded)
        setWindowFlags(Qt::Widget);
    QtBindResourceText(this, IDD_SETTINGS, "Settings");
    auto *layout = new QVBoxLayout(this);
    const auto group = [this, layout](QWidget *&content) {
        content = new QWidget(this);
        auto *fields = new QVBoxLayout(content);
        fields->setContentsMargins(0, 0, 0, 0);
        fields->setSpacing(8);
        layout->addWidget(content);
        return fields;
    };
    auto *appearanceLayout = group(_appearanceControls);
    auto *appearanceLabel = new QLabel(this);
    appearanceLabel->setObjectName("appearanceLabel");
    QtBindLocalText(appearanceLabel, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Appearance"));
    _appearance = new QComboBox(this);
    _appearance->setObjectName("appearanceChoice");
    appearanceLabel->setBuddy(_appearance);
    reloadAppearanceText();
    appearanceLayout->addWidget(appearanceLabel);
    appearanceLayout->addWidget(_appearance);
    connect(_appearance, &QComboBox::currentIndexChanged, this, [this] {
        if (!_initMode) {
            _appearanceChanged = true;
            Changed();
        }
    });
    auto *listLayout = group(_listControls);
    _showDots = new QCheckBox(this);
    QtBindResourceText(_showDots, IDX_SETTINGS_SHOW_DOTS, "Show \"..\" item");
    _showDots->setObjectName(QStringLiteral("showDotsOption"));
    _showRealFileIcons = new QCheckBox(this);
    QtBindResourceText(_showRealFileIcons, IDX_SETTINGS_SHOW_REAL_FILE_ICONS,
                       "Show real file &icons");
    _showRealFileIcons->setObjectName("realFileIconsOption");
    _fullRow = new QCheckBox(this);
    QtBindResourceText(_fullRow, IDX_SETTINGS_FULL_ROW, "&Full row select");
    _fullRow->setObjectName(QStringLiteral("fullRowOption"));
    _showGrid = new QCheckBox(this);
    QtBindResourceText(_showGrid, IDX_SETTINGS_SHOW_GRID, "Show &grid lines");
    _showGrid->setObjectName("gridOption");
    _singleClick = new QCheckBox(this);
    QtBindResourceText(_singleClick, IDX_SETTINGS_SINGLE_CLICK, "&Single-click to open an item");
    _singleClick->setObjectName("singleClickOption");
    _alternativeSelection = new QCheckBox(this);
    QtBindResourceText(_alternativeSelection, IDX_SETTINGS_ALTERNATIVE_SELECTION,
                       "&Alternative selection mode");
    _alternativeSelection->setObjectName(QStringLiteral("alternativeSelectionOption"));
    for (auto *control : {_showDots, _showRealFileIcons, _fullRow, _showGrid, _singleClick,
                          _alternativeSelection}) {
        listLayout->addWidget(control);
        connect(control, &QCheckBox::toggled, this, &CSettingsPage::OnButtonClicked);
    }
    auto *memoryLayout = group(_memoryControls);
    auto *memoryLabel = new QLabel(this);
    QtBindResourceText(memoryLabel, IDT_MEM_USAGE_EXTRACT,
                       "Maximum amount of RAM memory usage allowed to unpack archives:");
    memoryLabel->setObjectName("extractMemoryLabel");
    memoryLabel->setWordWrap(true);
    memoryLayout->addWidget(memoryLabel);
    auto *memoryRow = new QHBoxLayout;
    memoryRow->setSpacing(12);
    _memSet = new QCheckBox(":", this);
    _memSet->setObjectName("extractMemorySet");
    QtBindLangText(_memSet, IDT_MEM_USAGE_EXTRACT, {}, true);
    _memSpin = new QtMemoryLimitSpin(this);
    _memSpin->setObjectName("extractMemorySpin");
    QtBindLangText(_memSpin, IDT_MEM_USAGE_EXTRACT, {}, true);
    _memSpin->setFixedWidth(90);
    setFocusProxy(_memSpin);
    auto *ramLabel = new QLabel(this);
    ramLabel->setObjectName("extractMemoryRam");
    memoryRow->addWidget(_memSet);
    memoryRow->addWidget(_memSpin);
    memoryRow->addWidget(ramLabel);
    memoryRow->addStretch();
    memoryLayout->addLayout(memoryRow);
    // Keep the source settings and memory rows together when the sheet grows.
    layout->addStretch();
    connect(_memSet, &QCheckBox::toggled, this, [this](bool checked) {
        if (!_initMode) {
            _memx_wasChanged = true;
            EnableSpin(checked);
            Changed();
        }
    });
    connect(_memSpin, &QtMemoryLimitSpin::textChanged, this, [this] {
        if (!_initMode) {
            _memx_wasChanged = true;
            Changed();
        }
    });
    if (!embedded) {
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->setObjectName("settingsButtons");
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &CSettingsPage::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
}

void CSettingsPage::reloadAppearanceText() {
    const QSignalBlocker blocked(_appearance);
    const QVariant selected = _appearance->currentData();
    _appearance->clear();
    _appearance->addItem(QCoreApplication::translate("SettingsRedesign", "Follow system"),
                         int(Qt::ColorScheme::Unknown));
    _appearance->addItem(QCoreApplication::translate("SettingsRedesign", "Light"),
                         int(Qt::ColorScheme::Light));
    _appearance->addItem(QCoreApplication::translate("SettingsRedesign", "Dark"),
                         int(Qt::ColorScheme::Dark));
    if (selected.isValid())
        _appearance->setCurrentIndex(_appearance->findData(selected));
}

void CSettingsPage::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        reloadAppearanceText();
}
