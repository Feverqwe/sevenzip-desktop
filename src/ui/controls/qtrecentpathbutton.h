#pragma once

#include <QPushButton>
#include <QStringList>

// Presents caller-owned history without changing, interpreting or saving paths.
class QtRecentPathButton final : public QPushButton {
    Q_OBJECT
  public:
    explicit QtRecentPathButton(const QStringList &paths, QWidget *parent = nullptr);

  signals:
    void pathSelected(const QString &path);
};
