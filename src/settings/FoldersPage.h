// Adapted from 7-Zip FoldersPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include <QWidget>

class UString;
class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QPushButton;

class CFoldersPage : public QWidget {
    Q_OBJECT
    QLineEdit *m_WorkPath;
    QButtonGroup *_workModes;
    QPushButton *_workBrowse;
    QCheckBox *_forRemovable;
    QString _settingsPath;
    bool _needSave = false;
    bool _initMode = true;
    void MyEnableControls();
    void ModifiedEvent();
    void OnFoldersWorkButtonPath();
    bool ReadPath(UString &path);
    int GetWorkMode() const;
    bool OnInit();
    bool OnButtonClicked(unsigned buttonID);

  public:
    explicit CFoldersPage(const QString &settingsPath, QWidget *parent = nullptr);
    long OnApply();
    bool IsModified() const { return _needSave; }

  signals:
    void Changed();
};
