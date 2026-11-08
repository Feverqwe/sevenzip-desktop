#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/AppState.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/window/qtsavedlocation.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {
QString FavoriteKey(QString path) {
    path = QDir::fromNativeSeparators(path);
    while (path.endsWith('/') && path.size() > 1 && path != "//" &&
           !(path.size() == 3 && path[1] == ':'))
        path.chop(1);
    return path;
}
} // namespace

QtFavoriteStore::QtFavoriteStore(const QString &settingsPath, QObject *parent)
    : QAbstractListModel(parent), settingsPath_(settingsPath) {
    QSettings settings(settingsPath_, QSettings::IniFormat);
    if (settings.contains("workspace/favorites")) {
        const auto entries =
            QJsonDocument::fromJson(settings.value("workspace/favorites").toByteArray()).array();
        for (const auto &value : entries) {
            const auto entry = value.toObject();
            const QString id = entry.value("id").toString();
            const QString path = entry.value("path").toString();
            const QString nativeIdentity = entry.value("nativePlace").toString();
            if (!id.isEmpty() && (!path.isEmpty() || nativeIdentity == "locations"))
                favorites_.append({id, entry.value("label").toString(), path,
                                   entry.value("defaultLabel").toString(), nativeIdentity,
                                   QtArchiveLocation::Restore(entry.value("archive").toObject()),
                                   entry.value("diskFolder").toBool()});
        }
    } else {
        CFastFolders legacy;
        legacy.Read(settingsPath_);
        FOR_VECTOR(i, legacy.Strings) {
            const QString path = QtSavedSourcePath(ToQString(legacy.Strings[i]));
            if (!path.isEmpty())
                Add(path);
        }
        if (favorites_.isEmpty()) {
            for (const auto &[label, path] : QList<QPair<QString, QString>>{
                     {QStringLiteral(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Home")),
                      QDir::homePath()},
                     {QStringLiteral(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Documents")),
                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)},
                     {QStringLiteral(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Downloads")),
                      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)}}) {
                const int before = favorites_.size();
                Add(path, label);
                if (favorites_.size() > before)
                    favorites_.last().defaultLabel = label;
            }
        }
        Save();
    }
}

int QtFavoriteStore::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : favorites_.size();
}

QtFavorite QtFavoriteStore::Favorite(int row) const {
    auto entry = row >= 0 && row < favorites_.size() ? favorites_[row] : QtFavorite{};
    if (!entry.defaultLabel.isEmpty())
        entry.label = QCoreApplication::translate("QtWindowWorkspace",
                                                  entry.defaultLabel.toUtf8().constData());
    return entry;
}

QModelIndex QtFavoriteStore::IndexForId(const QString &id) const {
    for (int row = 0; row < favorites_.size(); ++row)
        if (favorites_[row].id == id)
            return index(row);
    return {};
}

void QtFavoriteStore::RefreshLabels() {
    if (!favorites_.isEmpty())
        emit dataChanged(index(0), index(favorites_.size() - 1),
                         {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
}

QVariant QtFavoriteStore::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= favorites_.size())
        return {};
    const auto &entry = favorites_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return Favorite(index.row()).label;
    case Qt::ToolTipRole:
        return Favorite(index.row()).label + '\n' + entry.path;
    case Qt::UserRole:
        return entry.id;
    case Qt::UserRole + 1:
        return entry.path;
    default:
        return {};
    }
}

Qt::ItemFlags QtFavoriteStore::flags(const QModelIndex &index) const {
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
                                 Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                           : Qt::ItemIsDropEnabled;
}

bool QtFavoriteStore::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() >= favorites_.size() ||
        value.toString().trimmed().isEmpty())
        return false;
    favorites_[index.row()].label = value.toString().trimmed();
    favorites_[index.row()].defaultLabel.clear();
    Save();
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
    return true;
}

void QtFavoriteStore::Add(const QString &path, const QString &label, const QString &nativeIdentity,
                          const QtArchiveLocation &archive, bool diskFolder) {
    if (path.isEmpty() && nativeIdentity != "locations")
        return;
    for (const auto &entry : favorites_)
        if (nativeIdentity.isEmpty() ? FavoriteKey(entry.path) == FavoriteKey(path)
                                     : entry.nativeIdentity == nativeIdentity)
            return;
    QString name = label.isEmpty() ? QFileInfo(path).fileName() : label;
    if (name.isEmpty())
        name = path;
    beginInsertRows({}, favorites_.size(), favorites_.size());
    favorites_.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                       name,
                       path,
                       {},
                       nativeIdentity,
                       archive,
                       diskFolder});
    endInsertRows();
    Save();
}

bool QtFavoriteStore::moveRows(const QModelIndex &source, int row, int count,
                               const QModelIndex &destination, int before) {
    if (source.isValid() || destination.isValid() || count != 1 || row < 0 ||
        row >= favorites_.size() || before < 0 || before > favorites_.size() || before == row ||
        before == row + 1)
        return false;
    beginMoveRows({}, row, row, {}, before);
    favorites_.move(row, before > row ? before - 1 : before);
    endMoveRows();
    Save();
    return true;
}

bool QtFavoriteStore::removeRows(int row, int count, const QModelIndex &parent) {
    if (parent.isValid() || count < 1 || row < 0 || row + count > favorites_.size())
        return false;
    beginRemoveRows({}, row, row + count - 1);
    favorites_.remove(row, count);
    endRemoveRows();
    Save();
    return true;
}

QStringList QtFavoriteStore::mimeTypes() const {
    return {QStringLiteral("application/x-sevenzip-favorite"), QStringLiteral("text/uri-list")};
}
QMimeData *QtFavoriteStore::mimeData(const QModelIndexList &indices) const {
    auto *mime = new QMimeData;
    if (!indices.isEmpty()) {
        const auto entry = Favorite(indices.first().row());
        mime->setData("application/x-sevenzip-favorite", entry.id.toUtf8());
    }
    return mime;
}
Qt::DropActions QtFavoriteStore::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction;
}
bool QtFavoriteStore::dropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int,
                                   const QModelIndex &parent) {
    if (action == Qt::IgnoreAction)
        return true;
    if (const auto *native = dynamic_cast<const QtPanelDragMime *>(mime);
        native && native->Data().IsTempFiles)
        return false;
#ifdef Q_OS_WIN
    if (auto *converter = QtNativePanelDragConverter::ForApplication())
        if (auto native = converter->Read(*mime)) {
            UInt32 flags = 0;
            if (!native->ReadTransferFlags(flags) || (flags & k_SourceFlags_TempFiles))
                return false;
        }
#endif
    const int before = row >= 0 ? row : parent.isValid() ? parent.row() : favorites_.size();
    const QString id = QString::fromUtf8(mime->data("application/x-sevenzip-favorite"));
    for (int i = 0; i < favorites_.size(); ++i)
        if (!id.isEmpty() && favorites_[i].id == id)
            return moveRows({}, i, 1, {}, before);
    bool added = false;
    for (const auto &url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
            Add(url.toLocalFile(), {}, {}, {}, true);
            added = true;
        }
    }
    return added;
}

void QtFavoriteStore::Save() {
    QJsonArray entries;
    for (const auto &entry : favorites_)
        entries.append(QJsonObject{{"id", entry.id},
                                   {"label", entry.label},
                                   {"path", entry.path},
                                   {"defaultLabel", entry.defaultLabel},
                                   {"nativePlace", entry.nativeIdentity},
                                   {"archive", entry.archive.Save()},
                                   {"diskFolder", entry.diskFolder}});
    QSettings settings(settingsPath_, QSettings::IniFormat);
    settings.setValue("workspace/favorites", QJsonDocument(entries).toJson(QJsonDocument::Compact));
}
