#pragma once
#include "ui/theme/qtcommandicons.h"
#include <QAbstractListModel>
class CPanel;

// Value snapshot of the native root namespaces, shared by all sidebar views.
// Folder interfaces and libraries never escape the enumeration call.
class QtNativePlaces final : public QAbstractListModel {
  public:
    explicit QtNativePlaces(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void Refresh();
    static QString RootIdentity(const CPanel &panel);
    QModelIndex FindIdentity(const QString &identity) const;

  private:
    struct Place {
        QString id, label, path;
        QtCommandIcon icon;
    };
    QList<Place> places_;
    QString language_;
};
