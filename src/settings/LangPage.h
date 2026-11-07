// Adapted from 7-Zip LangPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include <QWidget>
class QComboBox;

class CLangPage : public QWidget {
    Q_OBJECT
    QComboBox *_langCombo;
    bool _needSave = false;
    QString _settingsPath;
    QString _directory;

  public:
    bool LangWasChanged = false;
    explicit CLangPage(const QString &settingsPath, QWidget *parent = nullptr,
                       const QString &directory = {});
    bool OnInit();
    bool OnCommand(unsigned itemID);
    long OnApply();
    bool IsModified() const { return _needSave; }

  signals:
    void Changed();
};
