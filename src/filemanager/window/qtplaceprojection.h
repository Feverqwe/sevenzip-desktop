#pragma once
#include <QIdentityProxyModel>

namespace QtPlaceRoles {
enum { Identity = Qt::UserRole, Path, CurrentLocation };
}

// Per-panel location state over shared places. Selection stays with the view.
class QtPlaceProjection final : public QIdentityProxyModel {
  public:
    QtPlaceProjection(QAbstractItemModel &places, QObject *parent = nullptr);
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void SetCurrentLocation(const QString &path, const QString &identity = {});

  private:
    QString current_;
    QString identity_;
};
