#pragma once
#include "fileops/native/qtnativefilestamp.h"
#include <QString>
#include <QStringList>

// Nonsecret session values inside the existing owned-temp record.
struct QtExternalFileRecord {
    QString fileName;
    QString entry;
    QString prefix;
    QtNativeFileStamp initial;
    std::optional<QtNativeFileStamp> published;
    bool publicationError = false;
    QStringList retainedInputs{};
    QJsonObject Save() const;
    static std::optional<QtExternalFileRecord> Restore(const QJsonObject &value);
};
