#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace LicenseDocumentsFixture {
inline bool Write(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline bool Index(const QString &root, const QStringList &paths) {
    QJsonArray documents;
    for (const QString &path : paths)
        documents.append(QJsonObject{{"title", path}, {"path", path}});
    return Write(QDir(root).filePath("index.json"),
                 QJsonDocument(QJsonObject{{"schema", 1}, {"documents", documents}}).toJson());
}
} // namespace LicenseDocumentsFixture
