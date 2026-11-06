#include "dialogs/licenses/qtlicensedocuments.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>

QtLicenseDocuments::QtLicenseDocuments(QString directory)
    : directory_(QFileInfo(directory).absoluteFilePath()) {}

QString QtLicenseDocuments::InstalledDirectory() {
    const QDir executable(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    return executable.absoluteFilePath("../Resources/Notices");
#else
    return executable.absoluteFilePath("../share/licenses/sevenzip-desktop");
#endif
}

QString QtLicenseDocuments::resolve(const QString &relative, QString &error) const {
    const auto parts = relative.split('/');
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\') ||
        parts.contains("..") || parts.contains(".") || parts.contains(QString())) {
        error = QCoreApplication::translate("LicenseDocuments",
                                            "Invalid document path in the license index.");
        return {};
    }
    const QString root = QFileInfo(directory_).canonicalFilePath();
    const QString path = QFileInfo(QDir(directory_).filePath(relative)).canonicalFilePath();
    if (root.isEmpty() || path.isEmpty()) {
        error =
            QCoreApplication::translate("LicenseDocuments", "The license document is unavailable.");
        return {};
    }
    const QString inside = QDir(root).relativeFilePath(path);
    if (QDir::isAbsolutePath(inside) || inside == ".." || inside.startsWith("../")) {
        error = QCoreApplication::translate("LicenseDocuments",
                                            "Invalid document path in the license index.");
        return {};
    }
    return path;
}

bool QtLicenseDocuments::load(QString &error) {
    documents_.clear();
    error.clear();
    const QString path = resolve("index.json", error);
    if (path.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument index = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject object = index.object();
    if (parse.error != QJsonParseError::NoError || !index.isObject() ||
        object.value("schema").toInt() != 1 || !object.value("documents").isArray()) {
        error =
            QCoreApplication::translate("LicenseDocuments", "The license index could not be read.");
        return false;
    }
    for (const QJsonValue &entry : object.value("documents").toArray()) {
        const QJsonObject document = entry.toObject();
        const QString title = document.value("title").toString();
        documents_.append({title.isEmpty()
                               ? QCoreApplication::translate("LicenseDocuments", "Unnamed document")
                               : title,
                           document.value("path").toString()});
    }
    return true;
}

std::optional<QString> QtLicenseDocuments::read(int row, QString &error) const {
    error.clear();
    if (row < 0 || row >= documents_.size()) {
        error = QCoreApplication::translate("LicenseDocuments", "No document is selected.");
        return std::nullopt;
    }
    const QString path = resolve(documents_[row].path, error);
    if (path.isEmpty())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return std::nullopt;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    if (decoder.hasError()) {
        error = QCoreApplication::translate("LicenseDocuments",
                                            "The license document is not valid UTF-8 text.");
        return std::nullopt;
    }
    return text;
}
