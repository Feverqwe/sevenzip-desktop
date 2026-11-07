#pragma once
#include <QWidget>

class QGridLayout;
class QLabel;

// Read-only contextual values. Captions use the ordinary live-language binding;
// values remain literal, selectable text, including archive entry names/paths.
class QtMetadataRows final : public QWidget {
  public:
    struct Row {
        QLabel *caption;
        QLabel *value;
    };
    explicit QtMetadataRows(QWidget *parent = nullptr, int captionWidth = 0);
    Row AddLiteralRow(const QString &caption, const QString &value, const QString &name = {});
    QLabel *AddRow(const char *context, const char *caption, const QString &value,
                   const QString &name = {});

  private:
    QGridLayout *rows_;
    int captionWidth_;
    bool eventFilter(QObject *object, QEvent *event) override;
};
