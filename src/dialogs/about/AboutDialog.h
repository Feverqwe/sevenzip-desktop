// Adapted from 7-Zip AboutDialog.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Modified 2026-10-18: remove the obsolete presentation-only text setter.
#ifndef ZIP7_INC_ABOUT_DIALOG_H
#define ZIP7_INC_ABOUT_DIALOG_H

#include "7zip/UI/FileManager/AboutDialogRes.h"
#include "Common/MyString.h"
#include <QDialog>
#include <QVector>
class QLabel;
class QVBoxLayout;
class QtSourceSummary;

class CAboutDialog : public QDialog {
  public:
    explicit CAboutDialog(QWidget *parent = nullptr);
    bool OnInit();
    int Create() { return exec(); }

  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    void BuildPresentation();
    void BuildIdentity(QVBoxLayout &layout);
    void BuildUpstream(QVBoxLayout &layout);
    void BuildDetails(QVBoxLayout &layout);
    QString ValueOrUnknown(const QString &value) const;
    struct BuildDetail {
        QString value;
        QLabel *label;
    };
    QVector<BuildDetail> buildDetails_;
    QtSourceSummary *identity_ = nullptr;
    QSize initialPresentationSize_;
    bool initialPresentation_ = true;
};

#endif
