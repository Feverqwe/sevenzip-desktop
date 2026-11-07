// Adapted from 7-Zip RegistryUtils.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include <QString>

// Shared Qt configuration-path adapter for callers of registry-ported modules.
QString FmSettingsPath();
void SaveRegLang(const UString &path, const QString &settingsPath);
void ReadRegLang(UString &path, const QString &settingsPath);
bool ReadLockMemoryEnable(const QString &settingsPath);
void SaveLockMemoryEnable(bool enable, const QString &settingsPath);
void SaveRegEditor(bool useEditor, const UString &path, const QString &settingsPath);
void ReadRegEditor(bool useEditor, UString &path, const QString &settingsPath);
bool ReadConfirmUnknownFileOpen(const QString &settingsPath);
void SaveConfirmUnknownFileOpen(bool enabled, const QString &settingsPath);

struct CFmSettings {
    bool ShowDots;
    bool FullRow;
    bool ShowGrid;
    bool AlternativeSelection;
    // Qt presentation preference; Unknown follows the native system theme.
    Qt::ColorScheme ColorScheme = Qt::ColorScheme::Unknown;

    void Save(const QString &settingsPath) const;
    void Load(const QString &settingsPath);
};
