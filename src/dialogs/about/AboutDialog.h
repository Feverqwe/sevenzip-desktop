// Adapted from 7-Zip AboutDialog.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Modified 2026-10-18: remove the obsolete presentation-only text setter.
#ifndef ZIP7_INC_ABOUT_DIALOG_H
#define ZIP7_INC_ABOUT_DIALOG_H

#include "7zip/UI/FileManager/AboutDialogRes.h"
#include "Common/MyString.h"
#include <QDialog>
#include <QUrl>
#include <QVector>
#include <functional>
class CCodecs;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QtSourceSummary;

class CAboutDialog : public QDialog {
  public:
    using OpenUrl = std::function<bool(const QUrl &)>;
    explicit CAboutDialog(QWidget *parent = nullptr, const CCodecs *codecs = nullptr,
                          OpenUrl openUrl = {});
    bool OnInit();
    bool OnButtonClicked(unsigned buttonID, QWidget *buttonHWND);
    int Create() { return exec(); }

  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    void BuildPresentation();
    void BuildIdentity(QVBoxLayout &layout);
    void BuildUpstream(QVBoxLayout &layout);
    void BuildDetails(QVBoxLayout &layout);
    void BuildUrlNotice(QVBoxLayout &layout);
    void ShowLicenses();
    void ShowDiagnostics();
    void OpenExternal(const QUrl &url);
    QString Information() const;
    QString ValueOrUnknown(const QString &value) const;
    struct BuildDetail {
        const char *caption;
        QString value;
        QLabel *label;
    };
    QVector<BuildDetail> buildDetails_;
    QString codecMessage_;
    QString failedUrl_;
    OpenUrl openUrl_;
    QWidget *codecNotice_ = nullptr;
    QWidget *urlNotice_ = nullptr;
    QLabel *urlMessage_ = nullptr;
    QPushButton *licenseButton_ = nullptr;
    QtSourceSummary *identity_ = nullptr;
    QSize initialPresentationSize_;
    bool initialPresentation_ = true;
    const CCodecs *codecs_ = nullptr; // Caller retains the immutable catalog lease.
};

#endif
