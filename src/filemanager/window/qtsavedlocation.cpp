#include "filemanager/window/qtsavedlocation.h"
#include "Windows/FileFind.h"
#include "archive/resources/archiveinputpath.h"
#include "filemanager/panels/Panel.h"
#include "settings/ViewSettings.h"
#include <QFileInfo>
#include <QJsonArray>

namespace {
QJsonArray SaveDirectories(const QList<QtArchiveLocation::Directory> &directories) {
    QJsonArray result;
    for (const auto &directory : directories)
        result.append(QJsonObject{{"name", directory.name}, {"target", directory.target}});
    return result;
}

QList<QtArchiveLocation::Directory> RestoreDirectories(const QJsonArray &value) {
    QList<QtArchiveLocation::Directory> result;
    for (const auto &item : value) {
        const auto directory = item.toObject();
        result.append({directory.value("name").toString(), directory.value("target").toString()});
    }
    return result;
}
} // namespace

QtArchiveLocation QtArchiveLocation::Capture(const CPanel &panel,
                                             const std::vector<QtAddressSegment> &ancestors) {
    QtArchiveLocation location;
    const auto &parents = panel.GetParentFolders();
    if (!panel.IsArcFolder() || parents.IsEmpty())
        return location;
    location.physicalPath = ToQString(parents.Front().VirtualPath);
    for (unsigned index = 1; index < parents.Size(); ++index)
        location.nested.append(
            {ToQString(parents[index].ParentFolderPath), ToQString(parents[index].RelPath), {}});
    location.folder = ToQString(GetFolderPath(panel.GetFolder()));
    int archive = -1;
    QList<Directory> directories;
    for (const auto &segment : ancestors) {
        if (segment.kind == QtAddressSegmentKind::Archive) {
            if (archive >= 0 && archive < location.nested.size())
                location.nested[archive].directories = directories;
            ++archive;
            directories.clear();
        } else if (archive >= 0 && segment.kind == QtAddressSegmentKind::Folder)
            directories.append({ToQString(segment.label), ToQString(segment.target)});
    }
    if (archive != int(parents.Size()) - 1)
        return {};
    location.directories = std::move(directories);
    return location;
}

QJsonObject QtArchiveLocation::Save() const {
    if (physicalPath.isEmpty())
        return {};
    QJsonArray links;
    for (const auto &link : nested)
        links.append(QJsonObject{{"parent", link.parentFolder},
                                 {"entry", link.entry},
                                 {"directories", SaveDirectories(link.directories)}});
    return {{"physical", physicalPath},
            {"nested", links},
            {"folder", folder},
            {"directories", SaveDirectories(directories)}};
}

QString QtArchiveLocation::MoveToParent() {
    NWindows::NFile::NFind::CFileInfo source;
    if ((directories.isEmpty() && nested.isEmpty()) ||
        !source.Find_FollowLink(us2fs(ToUString(physicalPath)))) {
        const QString parent = QFileInfo(physicalPath).absolutePath();
        *this = {};
        return parent;
    }
    if (!directories.isEmpty()) {
        directories.removeLast();
        UString prefix = ToUString(folder);
        if (!prefix.IsEmpty() && IS_PATH_SEPAR(prefix.Back()))
            prefix.DeleteBack();
        folder = ToQString(prefix.Left(unsigned(prefix.ReverseFind_PathSepar() + 1)));
    } else {
        auto link = nested.takeLast();
        folder = std::move(link.parentFolder);
        directories = std::move(link.directories);
    }
    UString path = ToUString(physicalPath);
    path.Add_PathSepar();
    for (const auto &link : nested) {
        path += ToUString(link.parentFolder);
        path += ToUString(link.entry);
        path.Add_PathSepar();
    }
    path += ToUString(folder);
    return ToQString(path);
}

QtArchiveLocation QtArchiveLocation::Restore(const QJsonObject &value) {
    QtArchiveLocation location;
    location.physicalPath = value.value("physical").toString();
    for (const auto &item : value.value("nested").toArray()) {
        const auto link = item.toObject();
        const QString entry = link.value("entry").toString();
        if (entry.isEmpty())
            return {};
        location.nested.append({link.value("parent").toString(), entry,
                                RestoreDirectories(link.value("directories").toArray())});
    }
    location.folder = value.value("folder").toString();
    location.directories = RestoreDirectories(value.value("directories").toArray());
    return location;
}

QString QtSavedSourcePath(QString path) {
    // Earlier Qt settings used a display separator after the real archive.
    // Convert only that boundary; spaces inside source path components survive.
    const QString separator = QStringLiteral(" / ");
    for (int pos = path.indexOf(separator); pos >= 0;
         pos = path.indexOf(separator, pos + separator.size()))
        if (IsArchiveFileOrPackage(QFileInfo(path.left(pos)))) {
            path.replace(pos, separator.size(), QStringLiteral("/"));
            break;
        }
    return path;
}
