// Adapted from 7-Zip LangPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source language selection and apply behavior with Qt catalogs.
#include "settings/LangPage.h"

#include "7zip/UI/FileManager/LangPageRes.h"
#include "localization/LangUtils.h"
#include "localization/ResourceString.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>

CLangPage::CLangPage(const QString &settingsPath, QWidget *parent, const QString &directory)
    : QWidget(parent), _settingsPath(settingsPath), _directory(directory) {
    setObjectName("languagePage");
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel("Language:", this);
    label->setObjectName("languageLabel");
    label->setProperty("sourceControlID", IDT_LANG_LANG);
    QtBindResourceText(label, IDT_LANG_LANG, "Language:");
    layout->addWidget(label);
    _langCombo = new QComboBox(this);
    _langCombo->setObjectName("languageChoice");
    _langCombo->setProperty("sourceControlID", IDC_LANG_LANG);
    label->setBuddy(_langCombo);
    layout->addWidget(_langCombo);
    OnInit();
    connect(_langCombo, &QComboBox::currentIndexChanged, this,
            [this] { OnCommand(IDC_LANG_LANG); });
}

bool CLangPage::OnInit() {
    _langCombo->clear();
    for (const auto &language : QtAvailableLanguages(_directory)) {
        _langCombo->addItem(language.name, language.code);
        if (language.code == QString::fromWCharArray(g_LangID))
            _langCombo->setCurrentIndex(_langCombo->count() - 1);
    }
    return true;
}

long CLangPage::OnApply() {
    if (_needSave) {
        const std::wstring code = _langCombo->currentData().toString().toStdWString();
        SaveRegLang(UString(code.c_str()), _settingsPath);
    }
    _needSave = false;
    ReloadLang(_settingsPath, _directory);
    LangWasChanged = true;
    return 0;
}

bool CLangPage::OnCommand(unsigned itemID) {
    if (itemID == IDC_LANG_LANG) {
        _needSave = true;
        Changed();
        return true;
    }
    return false;
}
