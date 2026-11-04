// Adapted from 7-Zip SettingsPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt replaces property-page controls; source state/apply/RAM calculation retained.
#include "settings/SettingsPage.h"
#include "filemanager/HelpUtils.h"

#include "7zip/UI/FileManager/SettingsPageRes.h"
#include "Common/IntToString.h"
#include "Common/StringToInt.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/operations/ZipRegistry.h"
#include "filemanager/panels/Panel.h"
#include "localization/qtlangcontrols.h"
#include "platform/memory/qtmemorylimitinfo.h"
#include "platform/memory/qtmemorylockservices.h"
#include "ui/controls/qtmemorylimitspin.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#define kSettingsTopic "FM/options.htm#settings"
void CSettingsPage::OnNotifyHelp() { ShowHelpWindow(kSettingsTopic, this); }

CSettingsPage::CSettingsPage(const QString &settingsPath, QWidget *parent, bool embedded,
                             const IQtMemoryLockServices *memoryLockServices)
    : QDialog(parent), _settingsPath(settingsPath), _memoryLockServices(memoryLockServices) {
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
    _showSystemMenu = new QCheckBox(this);
    QtBindResourceText(_showSystemMenu, IDX_SETTINGS_SHOW_SYSTEM_MENU, "Show system &menu");
    _showSystemMenu->setObjectName(QStringLiteral("showSystemMenuOption"));
    for (auto *control : {_showDots, _showRealFileIcons, _fullRow, _showGrid, _singleClick,
                          _alternativeSelection, _showSystemMenu}) {
        listLayout->addWidget(control);
        connect(control, &QCheckBox::toggled, this, &CSettingsPage::OnButtonClicked);
    }
    auto *memoryLayout = group(_memoryControls);
    _largePages = new QCheckBox(this);
    QtBindResourceText(_largePages, IDX_SETTINGS_LARGE_PAGES, "Use &large memory pages");
    _largePages->setObjectName("largePagesOption");
    memoryLayout->addWidget(_largePages);
    connect(_largePages, &QCheckBox::toggled, this, [this] {
        if (!_initMode) {
            _largePages_wasChanged = true;
            Changed();
        }
    });
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
    OnInit();
}

bool CSettingsPage::OnInit() {
    _initMode = true;
    _wasChanged = false;
    _largePages_wasChanged = false;
    _memx_wasChanged = false;
    _appearanceChanged = false;
    _settings.Load(_settingsPath);
    _appearance->setCurrentIndex(_appearance->findData(int(_settings.ColorScheme)));
    _showDots->setChecked(_settings.ShowDots);
    _showRealFileIcons->setChecked(_settings.ShowRealFileIcons);
    _fullRow->setChecked(_settings.FullRow);
    _showGrid->setChecked(_settings.ShowGrid);
    _singleClick->setChecked(_settings.SingleClick);
    _alternativeSelection->setChecked(_settings.AlternativeSelection);
    _showSystemMenu->setChecked(_settings.ShowSystemMenu);
    const QtMemoryLockServices nativeMemoryLock;
    const auto &memoryLock = _memoryLockServices ? *_memoryLockServices : nativeMemoryLock;
    _largePages->setVisible(memoryLock.IsSupported());
    _largePages->setEnabled(memoryLock.IsSupported());
    if (memoryLock.IsSupported())
        _largePages->setChecked(ReadLockMemoryEnable(_settingsPath));
    else
        _largePages->setEnabled(false);
    {
        const auto memory = QtQueryMemoryLimitInfo();
        QtBindMemoryLimitUnitText(findChild<QLabel *>("extractMemoryRam"), memory.RamSizeDefined,
                                  memory.RamSizeGB);
        UInt32 limit = NExtract::Read_LimitGB(_settingsPath);
        if (limit != 0 && limit != (UInt32)(Int32)-1)
            _memSet->setChecked(true);
        else {
            limit = 4;
            EnableSpin(false);
        }
        _memSpin->setRange(1, memory.MaximumGB);
        UString s;
        s.Add_UInt32(limit);
        _memSpin->setText(ToQString(s));
    }
    _initMode = false;
    return true;
}

void CSettingsPage::OnButtonClicked() {
    if (!_initMode) {
        _wasChanged = true;
        Changed();
    }
}

void CSettingsPage::EnableSpin(bool enable) { _memSpin->setEnabled(enable); }

long CSettingsPage::OnApply() {
    if (_wasChanged) {
        CFmSettings st = _settings;
        st.ShowDots = _showDots->isChecked();
        st.ShowRealFileIcons = _showRealFileIcons->isChecked();
        st.FullRow = _fullRow->isChecked();
        st.ShowGrid = _showGrid->isChecked();
        st.SingleClick = _singleClick->isChecked();
        st.AlternativeSelection = _alternativeSelection->isChecked();
        st.ShowSystemMenu = _showSystemMenu->isChecked();
        st.Save(_settingsPath);
        _settings = st;
        _wasChanged = false;
    }
    if (_largePages_wasChanged) {
        const QtMemoryLockServices nativeMemoryLock;
        const auto &memoryLock = _memoryLockServices ? *_memoryLockServices : nativeMemoryLock;
        if (memoryLock.IsSupported()) {
            const bool enable = _largePages->isChecked();
            memoryLock.EnablePrivilege_LockMemory(enable);
            SaveLockMemoryEnable(enable, _settingsPath);
        }
        _largePages_wasChanged = false;
    }
    if (_memx_wasChanged) {
        UInt32 val = (UInt32)(Int32)-1;
        if (_memSet->isChecked()) {
            UString s = ToUString(_memSpin->text());
            const wchar_t *end;
            val = ConvertStringToUInt32(s.Ptr(), &end);
            if (s.IsEmpty() || *end != 0 || val > (1u << 30)) {
                ShowErrorMessageQt(this, NWindows::NError::MyFormatMessage(E_INVALIDARG));
                _memSpin->setFocus();
                return 1; // PSNRET_INVALID: keep the property sheet open.
            }
        }
        NExtract::Save_LimitGB(_settingsPath, val);
        _memx_wasChanged = false;
    }
    if (_appearanceChanged) {
        _settings.ColorScheme = Qt::ColorScheme(_appearance->currentData().toInt());
        _settings.Save(_settingsPath);
        _appearanceChanged = false;
    }
    return 0;
}

void CSettingsPage::accept() {
    if (OnApply() == 0)
        QDialog::accept();
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
