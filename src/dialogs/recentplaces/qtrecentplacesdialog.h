#pragma once
#include "filemanager/window/qtrecentplacesstore.h"
#include <QCoreApplication>
#include <QDialog>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class QtRecentPlacesDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(QtRecentPlacesDialog)
  public:
    explicit QtRecentPlacesDialog(QtRecentPlacesStore &store, QWidget *parent = nullptr);
    void SetOpenHandler(std::function<void(const QtRecentPlace &)> open);
    void Complete(bool success, const QString &error = {});
    void accept() override;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void Rebuild();
    void UpdateSelection();
    void ResizeRows();
    QtRecentPlacesStore &store_;
    std::function<void(const QtRecentPlace &)> open_;
    QLineEdit *search_;
    QListWidget *list_;
    QLabel *empty_;
    QLabel *error_;
    QPushButton *remove_;
    QPushButton *clear_;
    QPushButton *openButton_;
    bool busy_ = false;
    bool rowResizePending_ = false;
};
