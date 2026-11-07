// Adapted from 7-Zip SettingsPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "settings/RegistryUtils.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QtMemoryLimitSpin;
class CSettingsPage : public QDialog {
    Q_OBJECT
    bool _wasChanged = false;
    bool _initMode = true;
    bool _memx_wasChanged = false;
    bool _appearanceChanged = false;
    CFmSettings _settings;
    QString _settingsPath;
    QCheckBox *_showDots;
    QComboBox *_appearance;
    QWidget *_appearanceControls;
    QWidget *_memoryControls;
    QWidget *_listControls;
    void BuildControls(bool embedded);
    void reloadAppearanceText();
    void changeEvent(QEvent *event) override;
    QCheckBox *_alternativeSelection;
    QCheckBox *_memSet;
    QtMemoryLimitSpin *_memSpin;
    void EnableSpin(bool enable);
    bool OnInit();
    void OnButtonClicked();

  public:
    explicit CSettingsPage(const QString &settingsPath, QWidget *parent = nullptr,
                           bool embedded = false);
    long OnApply();
    void accept() override;
    CFmSettings settings() const { return _settings; }
    QWidget *appearanceControls() const { return _appearanceControls; }
    QWidget *memoryControls() const { return _memoryControls; }
    QWidget *listControls() const { return _listControls; }
    bool IsMemoryModified() const { return _memx_wasChanged; }
    bool IsListModified() const { return _wasChanged; }
    bool IsModified() const { return _wasChanged || _memx_wasChanged || _appearanceChanged; }

  signals:
    void Changed();
};
