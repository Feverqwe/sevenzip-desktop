// Adapted from 7-Zip CPP/7zip/UI/FileManager/SplitDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyTypes.h"
#include "common/collections/MyVector.h"
#include <QDialog>

class QComboBox;

class CSplitDialog : public QDialog {
    Q_OBJECT
  public:
    CSplitDialog(const QString &filePath, const QString &path, QWidget *parent = nullptr);
    QString FilePath;
    QString Path;
    CRecordVector<UInt64> VolumeSizes;
    void accept() override;

  private:
    QComboBox *_pathCombo = nullptr;
    QComboBox *_volumeCombo = nullptr;
    bool OnInit();
    void OnOK();
    void OnButtonSetPath();
    bool ReadPath(QString &path);
};
