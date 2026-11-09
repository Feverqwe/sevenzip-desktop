// Adapted from 7-Zip SettingsPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt replaces property-page controls; source state/apply/RAM calculation retained.
#include "settings/SettingsPage.h"

#include "Common/IntToString.h"
#include "Common/StringToInt.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/operations/ZipRegistry.h"
#include "filemanager/panels/Panel.h"
#include "localization/qtlangcontrols.h"
#include "platform/memory/qtmemorylimitinfo.h"
#include "ui/controls/qtmemorylimitspin.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

CSettingsPage::CSettingsPage(const QString &settingsPath, QWidget *parent, bool embedded)
    : QDialog(parent), _settingsPath(settingsPath) {
    BuildControls(embedded);
    OnInit();
}

bool CSettingsPage::OnInit() {
    _initMode = true;
    _wasChanged = false;
    _memx_wasChanged = false;
    _appearanceChanged = false;
    _settings.Load(_settingsPath);
    _appearance->setCurrentIndex(_appearance->findData(int(_settings.ColorScheme)));
    _showDots->setChecked(_settings.ShowDots);
    _showHiddenFolders->setChecked(_settings.ShowHiddenFolders);
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
        st.ShowHiddenFolders = _showHiddenFolders->isChecked();
        st.Save(_settingsPath);
        _settings = st;
        _wasChanged = false;
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
