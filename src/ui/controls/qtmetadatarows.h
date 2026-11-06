#pragma once
#include <QWidget>

class QGridLayout;
class QLabel;

// Read-only contextual values. Captions use the ordinary live-language binding;
// values remain literal, selectable text, including archive entry names/paths.
class QtMetadataRows final : public QWidget {
  public:
    explicit QtMetadataRows(QWidget *parent = nullptr);
    QLabel *AddRow(const char *context, const char *caption, const QString &value,
                   const QString &name = {});

  private:
    QGridLayout *rows_;
    bool eventFilter(QObject *object, QEvent *event) override;
};
