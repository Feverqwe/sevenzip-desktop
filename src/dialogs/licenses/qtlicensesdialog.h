#pragma once
#include "dialogs/licenses/qtlicensedocuments.h"
#include <QDialog>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QSplitter;

class QtLicensesDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit QtLicensesDialog(QWidget *parent = nullptr,
                              const QString &directory = QtLicenseDocuments::InstalledDirectory());

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void Filter();
    void ShowDocument();
    void UpdateOrientation();
    QtLicenseDocuments documents_;
    QString indexError_;
    QString documentText_;
    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
    QPlainTextEdit *text_ = nullptr;
    QPushButton *copy_ = nullptr;
    QLabel *message_ = nullptr;
    QSplitter *splitter_ = nullptr;
};
