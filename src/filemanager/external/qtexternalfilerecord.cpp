#include "filemanager/external/qtexternalfilerecord.h"
#include <QFileInfo>
#include <QJsonArray>

QJsonObject QtExternalFileRecord::Save() const {
    QJsonObject value{{"type", "external-edit"}, {"version", 1},     {"file", fileName},
                      {"entry", entry},          {"prefix", prefix}, {"initial", initial.Save()}};
    if (published) {
        value.insert("published", published->Save());
        value.insert("publicationError", publicationError);
    }
    if (!retainedInputs.isEmpty())
        value.insert("retainedInputs", QJsonArray::fromStringList(retainedInputs));
    return value;
}

std::optional<QtExternalFileRecord> QtExternalFileRecord::Restore(const QJsonObject &value) {
    const QString file = value.value("file").toString();
    const auto initial = QtNativeFileStamp::Restore(value.value("initial").toObject());
    if (value.value("type").toString() != "external-edit" || value.value("version").toInt() != 1 ||
        !initial || file.isEmpty() || file == "." || file == ".." || file.contains('/') ||
        file.contains('\\') || file.contains(QChar::Null) || QFileInfo(file).isAbsolute() ||
        !value.value("entry").isString() || !value.value("prefix").isString())
        return {};
    QtExternalFileRecord record{
        file, value.value("entry").toString(), value.value("prefix").toString(), *initial, {},
        false};
    if (value.contains("published")) {
        record.published = QtNativeFileStamp::Restore(value.value("published").toObject());
        if (!record.published)
            return {};
        record.publicationError = value.value("publicationError").toBool();
    }
    if (value.contains("retainedInputs")) {
        if (!value.value("retainedInputs").isArray())
            return {};
        const auto paths = value.value("retainedInputs").toArray();
        if (paths.size() > 4096)
            return {};
        for (const auto &path : paths) {
            const auto text = path.toString();
            if (!path.isString() || text.contains(QChar::Null) || !QFileInfo(text).isAbsolute())
                return {};
            record.retainedInputs.append(text);
        }
    }
    return record;
}
