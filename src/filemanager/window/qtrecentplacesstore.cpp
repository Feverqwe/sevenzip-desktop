#include "filemanager/window/qtrecentplacesstore.h"
#include "Common/StringConvert.h"
#include "fileops/patterns/Wildcard.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSettings>

QtRecentPlacesStore *QtRecentPlacesStore::ForSettings(const QString &settings) {
    for (auto *object : qApp->children())
        if (auto *store = dynamic_cast<QtRecentPlacesStore *>(object);
            store && store->settings_ == settings)
            return store;
    return new QtRecentPlacesStore(settings, qApp);
}

QtRecentPlacesStore::QtRecentPlacesStore(const QString &settings, QObject *parent)
    : QObject(parent), settings_(settings) {
    Reload();
}

int QtRecentPlacesStore::Count() const { return int(paths_.Size()); }

QtRecentPlace QtRecentPlacesStore::Place(int row) const {
    if (row < 0 || row >= Count())
        return {};
    const QString path = ToQString(paths_[unsigned(row)]);
    auto place = Target(paths_[unsigned(row)]);
    place.path = path;
    return place;
}

int QtRecentPlacesStore::IndexOf(const QString &path) const {
    const UString native = ToUString(path);
    FOR_VECTOR(index, paths_)
    if (CompareFileNames(paths_[index], native) == 0)
        return int(index);
    return -1;
}

QtRecentPlace QtRecentPlacesStore::Target(const UString &path) const {
    for (auto it = targets_.cbegin(); it != targets_.cend(); ++it)
        if (CompareFileNames(path, ToUString(it.key())) == 0)
            return it.value();
    return {};
}

void QtRecentPlacesStore::Reload() {
    history_.Read(settings_);
    history_.GetList(paths_);
    targets_.clear();
    const auto saved = QJsonDocument::fromJson(QSettings(settings_, QSettings::IniFormat)
                                                   .value("workspace/recentTargets")
                                                   .toByteArray())
                           .object();
    QJsonObject retained;
    FOR_VECTOR(index, paths_) {
        const QString path = ToQString(paths_[index]);
        const auto value = saved.value(path);
        if (!value.isObject())
            continue;
        const auto item = value.toObject();
        targets_.insert(path, {path, item.value("nativePlace").toString(),
                               QtArchiveLocation::Restore(item.value("archive").toObject())});
        retained.insert(path, item);
    }
    if (retained != saved)
        QSettings(settings_, QSettings::IniFormat)
            .setValue("workspace/recentTargets",
                      QJsonDocument(retained).toJson(QJsonDocument::Compact));
    emit historyChanged();
}

void QtRecentPlacesStore::Visit(const QtRecentPlace &place) {
    Reload();
    const UString path = ToUString(place.path);
    history_.AddString(path);
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (CompareFileNames(ToUString(it.key()), path) == 0)
            it = targets_.erase(it);
        else
            ++it;
    }
    targets_.insert(place.path, place);
    Publish();
}

void QtRecentPlacesStore::Publish() {
    history_.GetList(paths_);
    QJsonObject saved;
    QHash<QString, QtRecentPlace> retained;
    FOR_VECTOR(index, paths_) {
        const QString path = ToQString(paths_[index]);
        auto target = Target(paths_[index]);
        target.path = path;
        retained.insert(path, target);
        if (!target.nativeIdentity.isEmpty() || !target.archive.physicalPath.isEmpty())
            saved.insert(path, QJsonObject{{"nativePlace", target.nativeIdentity},
                                           {"archive", target.archive.Save()}});
    }
    history_.Save(settings_);
    QSettings(settings_, QSettings::IniFormat)
        .setValue("workspace/recentTargets", QJsonDocument(saved).toJson(QJsonDocument::Compact));
    targets_ = std::move(retained);
    emit historyChanged();
}
