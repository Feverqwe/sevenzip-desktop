#include "filemanager/window/qtsavedlocation.h"
#include "archive/resources/archiveinputpath.h"
#include <QFileInfo>

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
