#pragma once
#include <QList>
#include <QWidget>

// Equal native columns when they fit; a stack at narrower widths.
class QtResponsiveRow final : public QWidget {
  public:
    explicit QtResponsiveRow(const QList<QWidget *> &items, QWidget *parent = nullptr);
};
