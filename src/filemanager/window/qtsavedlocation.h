#pragma once
#include "filemanager/panels/qtaddresssegment.h"
#include <QJsonObject>
#include <QList>
#include <QString>
#include <vector>

class CPanel;

// Native archive-link values only: no providers, temporary files or secrets.
struct QtArchiveLocation {
    struct Directory {
        QString name;
        QString target;
    };
    struct Link {
        QString parentFolder;
        QString entry;
        QList<Directory> directories;
    };
    QString physicalPath;
    QList<Link> nested;
    QString folder;
    QList<Directory> directories;
    static QtArchiveLocation Capture(const CPanel &panel,
                                     const std::vector<QtAddressSegment> &ancestors);
    QJsonObject Save() const;
    static QtArchiveLocation Restore(const QJsonObject &value);
};

QString QtSavedSourcePath(QString path);
