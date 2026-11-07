#pragma once

#include <QLabel>

class QVBoxLayout;
class QWidget;
class QPushButton;
class QDialog;

// Shared presentation for redesigned dialogs; callers own fields and actions.
enum class QtDialogContent { Fixed, Scrollable };
QVBoxLayout *QtCreateDialogLayout(QWidget &window, QWidget &footer,
                                  QtDialogContent content = QtDialogContent::Fixed);
QVBoxLayout *QtCreateDialogLayout(QWidget &window, QtDialogContent content);
void QtSizeIconButton(QPushButton &button);
void QtFitScrollableDialog(QWidget &window);
// Opt in only for contextual forms, never modeless reports or operation progress.
void QtUseContextDialogModality(QDialog &dialog);

// Keep wrapped captions in the layout minimum at their current width.
class QtWrappingLabel final : public QLabel {
  public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override;
    int heightForWidth(int width) const override;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};
