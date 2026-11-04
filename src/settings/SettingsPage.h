// Adapted from 7-Zip SettingsPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "settings/RegistryUtils.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QtMemoryLimitSpin;
class IQtMemoryLockServices;
class CSettingsPage : public QDialog {
    Q_OBJECT
    bool _wasChanged = false;
    bool _initMode = true;
    bool _largePages_wasChanged = false;
    bool _memx_wasChanged = false;
    bool _appearanceChanged = false;
    CFmSettings _settings;
    QString _settingsPath;
    const IQtMemoryLockServices *_memoryLockServices;
    QCheckBox *_showDots;
    QComboBox *_appearance;
    QWidget *_appearanceControls;
    QWidget *_memoryControls;
    QWidget *_listControls;
    void BuildControls(bool embedded);
    void reloadAppearanceText();
    void changeEvent(QEvent *event) override;
    QCheckBox *_showRealFileIcons;
    QCheckBox *_fullRow;
    QCheckBox *_showGrid;
    QCheckBox *_singleClick;
    QCheckBox *_alternativeSelection;
    QCheckBox *_showSystemMenu;
    QCheckBox *_largePages;
    QCheckBox *_memSet;
    QtMemoryLimitSpin *_memSpin;
    void EnableSpin(bool enable);
    bool OnInit();
    void OnButtonClicked();

  public:
    explicit CSettingsPage(const QString &settingsPath, QWidget *parent = nullptr,
                           bool embedded = false,
                           const IQtMemoryLockServices *memoryLockServices = nullptr);
    long OnApply();
    void OnNotifyHelp();
    void accept() override;
    CFmSettings settings() const { return _settings; }
    QWidget *appearanceControls() const { return _appearanceControls; }
    QWidget *memoryControls() const { return _memoryControls; }
    QWidget *listControls() const { return _listControls; }
    bool IsMemoryModified() const { return _memx_wasChanged || _largePages_wasChanged; }
    bool IsListModified() const { return _wasChanged; }
    bool IsModified() const {
        return _wasChanged || _largePages_wasChanged || _memx_wasChanged || _appearanceChanged;
    }

  signals:
    void Changed();
};
