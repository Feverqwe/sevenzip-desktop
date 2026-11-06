#pragma once
#include <QDialog>
#include <QIcon>
#include <QStringList>

class QLabel;
class QPushButton;
class QtSourceSummary;

// Presentation of a captured selection. The caller owns validation and deletion.
class QtDeleteConfirmation final : public QDialog {
  public:
    enum class Mode { Trash, Permanent, Archive };
    struct Request {
        Mode mode = Mode::Permanent;
        QString location;
        QStringList names;
        QIcon icon;
        bool includesFolders = false;
        bool readOnly = false;
    };
    explicit QtDeleteConfirmation(Request request, QWidget *parent = nullptr);
    void accept() override;

  protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    const Request request_;
    bool initialSize_ = true;
    QtSourceSummary *source_ = nullptr;
    QLabel *heading_ = nullptr;
    QLabel *consequence_ = nullptr;
    QLabel *folders_ = nullptr;
    QLabel *restriction_ = nullptr;
    QPushButton *confirm_ = nullptr;
    void buildPresentation();
    void refreshText();
};
