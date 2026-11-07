#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
class QVBoxLayout;

QVBoxLayout *QtCreateDisclosureLayout();

// Presentation-only disclosure: hiding content never changes its values/enabled state.
class QtDisclosure final : public QWidget {
    Q_OBJECT
  public:
    // A collapsed description occupies the same slot as the expanded content.
    explicit QtDisclosure(QWidget *content, QWidget *parent = nullptr,
                          QWidget *description = nullptr);
    QToolButton *button() const { return button_; }
    void setExpanded(bool expanded);
    bool isExpanded() const;
    void setSummary(const QString &text);

  private:
    QWidget *content_;
    QWidget *description_;
    QToolButton *button_;
    QLabel *summary_;
};
