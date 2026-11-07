// Adapted from 7-Zip MenuPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include <QWidget>
#include <memory>
class IQtMenuPageServices;
class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;

// Windows source ArchiveExtractCallback can write Zone.Identifier.
bool IsZonePropagationSupported();

struct CShellDll {
    FString Path;
    bool wasChanged;
    bool prevValue;
    unsigned ctrl;
    UInt32 wow;
    CShellDll() : wasChanged(false), prevValue(false), ctrl(0), wow(0) {}
};

class CMenuPage : public QWidget {
    Q_OBJECT
    bool _initMode = true;
    bool _cascaded_Changed = false;
    bool _menuIcons_Changed = false;
    bool _elimDup_Changed = false;
    bool _writeZone_Changed = false;
    bool _flags_Changed = false;
    QString _settingsPath;
    std::shared_ptr<IQtMenuPageServices> _services;
    CShellDll _dlls[2];
    QCheckBox *ShellControl(unsigned id) const;
    QCheckBox *_cascaded;
    QCheckBox *_menuIcons;
    QCheckBox *_elimDup;
    QListWidget *_listView;
    QComboBox *_zoneCombo;
    void BuildPresentation(bool zoneSupported);
    void Clear_MenuChanged();
    bool OnButtonClicked(unsigned buttonID);
    bool OnCommand(unsigned itemID);
    bool OnItemChanged(QListWidgetItem *item);

  public:
    explicit CMenuPage(const QString &settingsPath, QWidget *parent = nullptr,
                       bool zoneSupported = IsZonePropagationSupported(),
                       std::shared_ptr<IQtMenuPageServices> services = {});
    bool OnInit();
    long OnApply();
    bool IsModified() const;
    bool IntegrationSupported() const;
    void ReloadLangItems();

  signals:
    void Changed();
};
