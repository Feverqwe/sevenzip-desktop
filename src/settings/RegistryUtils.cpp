// Adapted from 7-Zip RegistryUtils.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// QSettings replaces the current-user File Manager registry key.
#include "settings/RegistryUtils.h"
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

static void SaveCuString(const QString &settingsPath, const char *valuePath, const UString &value) {
    if (settingsPath.isEmpty())
        return;
    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("FM");
    key.setValue(QString::fromLatin1(valuePath), QString::fromWCharArray(value.Ptr()));
}

static void ReadCuString(const QString &settingsPath, const char *valuePath, UString &res) {
    res.Empty();
    if (settingsPath.isEmpty())
        return;
    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("FM");
    const auto value = key.value(QString::fromLatin1(valuePath)).toString().toStdWString();
    res = value.c_str();
}

void SaveRegLang(const UString &path, const QString &settingsPath) {
    SaveCuString(settingsPath, "Lang", path);
}

void ReadRegLang(UString &path, const QString &settingsPath) {
    ReadCuString(settingsPath, "Lang", path);
}

QString FmSettingsPath() {
    const QString overridden = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    if (!overridden.isEmpty())
        return overridden;
#ifdef _WIN32
    // Explorer has no project QCoreApplication identity. Keep the application
    // and its in-process shell DLL on the same project settings owner.
    const QString directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            .filePath("Sevenzip");
#else
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    QDir().mkpath(directory);
    return QDir(directory).filePath("toolbar.ini");
}

static const char *const kShowDots = "ShowDots";

static void SaveOption(QSettings &key, const char *name, bool enabled) {
    key.setValue(QString::fromLatin1(name), enabled);
}

static void ReadOption(QSettings &key, const char *name, bool &dest) {
    const QVariant value = key.value(QString::fromLatin1(name));
    // Registry GetValue_bool_IfOk preserves the default for invalid values.
    if (value.toString() == "true")
        dest = true;
    else if (value.toString() == "false")
        dest = false;
    else {
        bool ok = false;
        const qulonglong number = value.toString().toULongLong(&ok);
        if (ok && number <= 0xFFFFFFFFu)
            dest = number != 0;
    }
}

void SaveLockMemoryEnable(bool enable, const QString &settingsPath) {
    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("Options");
    SaveOption(key, "LargePages", enable);
}

bool ReadLockMemoryEnable(const QString &settingsPath) {
    bool enabled = false;
    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("Options");
    ReadOption(key, "LargePages", enabled);
    return enabled;
}

void CFmSettings::Save(const QString &settingsPath) const {
    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("FM");
    SaveOption(key, kShowDots, ShowDots);
    key.setValue("ColorScheme", int(ColorScheme));
}

void CFmSettings::Load(const QString &settingsPath) {
    ShowDots = false;

    QSettings key(settingsPath, QSettings::IniFormat);
    key.beginGroup("FM");
    ReadOption(key, kShowDots, ShowDots);
    bool valid = false;
    const int scheme = key.value("ColorScheme").toInt(&valid);
    ColorScheme =
        valid && (scheme == int(Qt::ColorScheme::Light) || scheme == int(Qt::ColorScheme::Dark))
            ? Qt::ColorScheme(scheme)
            : Qt::ColorScheme::Unknown;
}
