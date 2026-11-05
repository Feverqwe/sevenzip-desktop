#pragma once
#include "operations/qtoperationregistry.h"
#include <QDialog>

class QtTaskResultDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit QtTaskResultDialog(const QtOperationReview &review);
};
