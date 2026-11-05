#include "filemanager/window/qtplaceprojection.h"
#include <QDir>

namespace {
QString LocationKey(const QString &path) {
    return path.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
}
} // namespace

QtPlaceProjection::QtPlaceProjection(QAbstractItemModel &places, QObject *parent)
    : QIdentityProxyModel(parent) {
    setSourceModel(&places);
}

QVariant QtPlaceProjection::data(const QModelIndex &index, int role) const {
    if (role == QtPlaceRoles::CurrentLocation && !identity_.isEmpty())
        return index.isValid() &&
               QIdentityProxyModel::data(index, QtPlaceRoles::Identity).toString() == identity_;
    if (role == QtPlaceRoles::CurrentLocation)
        return index.isValid() && !current_.isEmpty() &&
               LocationKey(QIdentityProxyModel::data(index, QtPlaceRoles::Path).toString()) ==
                   current_;
    return QIdentityProxyModel::data(index, role);
}

void QtPlaceProjection::SetCurrentLocation(const QString &path, const QString &identity) {
    const QString current = LocationKey(path);
    if (current_ == current && identity_ == identity)
        return;
    current_ = current;
    identity_ = identity;
    if (rowCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {QtPlaceRoles::CurrentLocation});
}
