// Adapted from 7-Zip EditPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include <QWidget>
class QLineEdit;
class QCheckBox;

struct CEditPageCtrl {
    QLineEdit *Edit = nullptr;
    bool WasChanged = false;
};

class CEditPage : public QWidget {
    Q_OBJECT
    CEditPageCtrl _ctrls[3];
    bool _initMode = true;
    QString _settingsPath;
    QCheckBox *_confirmUnknownFileOpen;
    bool _confirmChanged = false;

  public:
    explicit CEditPage(const QString &settingsPath, QWidget *parent = nullptr);
    bool OnInit();
    long OnApply(QWidget **invalidControl = nullptr);
    bool IsModified() const;

  signals:
    void Changed();
};
