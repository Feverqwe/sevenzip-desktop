#include "archive/backend/archivebackend.h"
#include "archive/opening/OpenArchive.h"
#include "archive/operations/ExtractingFilePath.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlanguageservices.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace {
HRESULT NativeExtractionEntries(const CArc &arc, QList<ArchiveEntry> &entries) {
    UInt32 count = 0;
    RINOK(arc.Archive->GetNumberOfItems(&count))
    for (UInt32 i = 0; i < count; ++i) {
        ArchiveEntry entry;
        UString path;
        RINOK(arc.GetItem_Path(i, path))
        entry.path = QString::fromWCharArray(path.Ptr());
        RINOK(Archive_IsItem_Dir(arc.Archive, i, entry.directory))
        bool alt = false;
        RINOK(Archive_IsItem_AltStream(arc.Archive, i, alt))
        if (alt)
            entry.properties.insert(QStringLiteral("Alternate Stream"), "+");
        entries.append(entry);
    }
    return S_OK;
}

bool SafeExtractionPath(const QString &path) {
    QString normalized = QDir::fromNativeSeparators(path);
    normalized.replace('\\', '/');
    if (normalized.isEmpty() || normalized.startsWith('/') ||
        QRegularExpression(R"(^[A-Za-z]:)").match(normalized).hasMatch())
        return false;
    for (const QString &part : normalized.split('/')) {
        const QString base = part.section('.', 0, 0).toUpper();
        if (part.isEmpty() || part == ".." || part == "." || part.endsWith('.') ||
            part.endsWith(' ') ||
#ifdef Q_OS_WIN
            part.contains(':') ||
#endif
            QRegularExpression(R"([\x00-\x1f])").match(part).hasMatch() || base == "CON" ||
            base == "PRN" || base == "AUX" || base == "NUL" ||
            QRegularExpression(R"(^(COM[1-9]|LPT[1-9])$)").match(base).hasMatch())
            return false;
    }
    return true;
}

bool ValidateExtractionNodes(const QString &output, bool directory, QString *error) {
    QString candidate = output;
    bool leaf = true;
    while (true) {
        const QFileInfo existing(candidate);
#ifdef Q_OS_MACOS
        const bool systemAlias = candidate == "/var" || candidate == "/tmp" || candidate == "/etc";
#else
        const bool systemAlias = false;
#endif
        if ((existing.isSymLink() && !systemAlias) ||
            (existing.exists() && (leaf && !directory ? !existing.isFile() : !existing.isDir()))) {
            *error =
                QCoreApplication::translate("archivebackend_validation",
                                            "Destination path is occupied by a file or link: %1")
                    .arg(candidate);
            return false;
        }
        const QString parent = existing.absolutePath();
        if (parent == candidate)
            break;
        candidate = parent;
        leaf = false;
    }
    return true;
}

struct ExtractionPathContext {
    const QFileInfo &target;
    const ExtractionOptions &options;
    const QString &currentFolder;
    const QString &boundFolder;
    bool nativeSource;
};

std::expected<QString, QString> ExtractionRelativePath(const ExtractionPathContext &context,
                                                       const ArchiveEntry &entry,
                                                       const QString &entryPath) {
    const auto &[target, options, currentFolder, boundFolder, nativeSource] = context;
    QString boundedPath = entryPath;
    if (options.pathMode == ExtractionOptions::PathMode::AbsolutePaths &&
        QDir::isAbsolutePath(entryPath)) {
        // Reject traversal before cleanPath can erase it. The source path
        // algorithm keeps absolute prefixes only in kAbsPaths mode.
        if (entryPath.split('/').contains("..")) {
            return std::unexpected(
                QCoreApplication::translate("archivebackend_validation",
                                            "Archive contains an unsafe path: %1")
                    .arg(entry.path));
        }
        QString error;
        if (!ArchiveBackend::validateExtractionOutput(target.filePath(), entryPath, entry.directory,
                                                      &error))
            return std::unexpected(error);
        boundedPath = QDir(target.absoluteFilePath()).relativeFilePath(entryPath);
    }
    const bool streamFolder = options.pathMode == ExtractionOptions::PathMode::CurrentPaths &&
                              currentFolder.endsWith(':');
    const bool stream = streamFolder && !entry.directory &&
                        entry.properties.value(QStringLiteral("Alternate Stream")) == "+";
    const bool sourceRelative = options.pathMode == ExtractionOptions::PathMode::CurrentPaths &&
                                !boundFolder.isEmpty() && boundFolder == currentFolder;
    const QString base = streamFolder ? currentFolder.chopped(1) : QString();
    const QString streamName = entryPath.mid(currentFolder.size());
    // Original ArchiveExtractCallback::CorrectPathParts removes the base
    // and colon when extracting from an ADS folder. Validate that exact
    // output name. Ordinary POSIX filenames may contain colons; ADS
    // names still cannot contain a second stream separator.
    const bool safeStream = stream && entryPath.startsWith(currentFolder) &&
                            (sourceRelative || SafeExtractionPath(base)) &&
                            SafeExtractionPath(streamName) && !streamName.contains('/') &&
                            !streamName.contains('\\') && !streamName.contains(':');
    QString relative = boundedPath;
    if (options.pathMode == ExtractionOptions::PathMode::CurrentPaths && !currentFolder.isEmpty()) {
        const QString prefix = currentFolder + (safeStream ? QString() : QString('/'));
        if (!relative.startsWith(prefix)) {
            return std::unexpected(
                QCoreApplication::translate("archivebackend_validation",
                                            "Item is outside the current archive folder: %1")
                    .arg(entry.path));
        }
        relative.remove(0, prefix.size());
    } else if (options.pathMode == ExtractionOptions::PathMode::NoPaths)
        relative = relative.section('/', -1);
    // A bound source folder can remove this exact prefix even when the
    // prefix itself contains parent components. Unbound inputs stay strict.
    QStringList checkedParts = (sourceRelative ? relative : boundedPath).split('/');
    checkedParts.removeAll(QStringLiteral("."));
    const QString checkedPath = checkedParts.join('/');
    const int colon = checkedPath.indexOf(':');
    // The listing declares streams explicitly; unlike links, their bounded
    // output names do not require a native-index extraction workflow.
    const bool nativeStream = !streamFolder && !entry.directory && !entry.link &&
                              entry.properties.value(QStringLiteral("Alternate Stream")) == "+" &&
                              colon > 0 && SafeExtractionPath(checkedPath.left(colon)) &&
                              SafeExtractionPath(checkedPath.mid(colon + 1)) &&
                              !checkedPath.mid(colon + 1).contains('/') &&
                              !checkedPath.mid(colon + 1).contains('\\') &&
                              !checkedPath.mid(colon + 1).contains(':');
    const bool safeRelative =
        (entry.directory && checkedParts.isEmpty()) || SafeExtractionPath(checkedPath);
    if ((!safeStream && !nativeStream && !safeRelative) || (entry.link && !nativeSource) ||
        (streamFolder && !safeStream)) {
        return std::unexpected(
            QCoreApplication::translate("archivebackend_validation",
                                        "Archive contains an unsafe path or link: %1")
                .arg(entry.path));
    }
    if (!safeStream && !nativeStream) {
        UStringVector parts;
        SplitPathToParts(UString(relative.toStdWString().c_str()), parts);
        Correct_FsPath(false, false, parts, entry.directory);
        relative = QString::fromWCharArray(MakePathFromParts(parts).Ptr());
        relative.replace('\\', '/');
    }
    return relative;
}

} // namespace

bool ArchiveBackend::safePath(const QString &path) { return SafeExtractionPath(path); }

bool ArchiveBackend::validateExtractionOutput(const QString &destination, const QString &path,
                                              bool directory, QString *error) {
    const QString root = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    const QString output = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(root).relativeFilePath(output);
    if ((relative == "." && !directory) || relative == ".." || relative.startsWith("../") ||
        QDir::isAbsolutePath(relative) || (relative != "." && !safePath(relative))) {
        *error =
            QCoreApplication::translate("archivebackend_validation",
                                        "Extraction path is outside the destination folder: %1")
                .arg(path);
        return false;
    }
    return ValidateExtractionNodes(output, directory, error);
}

bool ArchiveBackend::validateExtractionStreamOutput(const QString &destination, const QString &path,
                                                    QString *error) {
    const QString output = QDir::fromNativeSeparators(path);
    const int colon = output.lastIndexOf(':');
    if (colon > output.lastIndexOf('/')) {
        const QString name = output.mid(colon + 1);
        if (!safePath(name) || name.contains('/') || name.contains('\\')) {
            *error = QCoreApplication::translate("archivebackend_validation",
                                                 "Unsafe alternate stream name");
            return false;
        }
#ifdef Q_OS_WIN
        const QString base = output.left(colon);
        return validateExtractionOutput(destination, base, QFileInfo(base).isDir(), error);
#else
        // POSIX writes a standalone file. Validate the known stream's two
        // components without mistaking h:stream for a Windows drive prefix,
        // then check the actual output and all its filesystem ancestors.
        const QString root = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
        const QString absolute = QDir::cleanPath(QFileInfo(output).absoluteFilePath());
        const QString relative = QDir(root).relativeFilePath(absolute);
        const int separator = relative.lastIndexOf(':');
        const QString base = relative.left(separator);
        if (separator < 0 || separator <= relative.lastIndexOf('/') ||
            (!base.isEmpty() && !safePath(base))) {
            *error =
                QCoreApplication::translate("archivebackend_validation",
                                            "Extraction path is outside the destination folder: %1")
                    .arg(path);
            return false;
        }
        return ValidateExtractionNodes(absolute, false, error);
#endif
    }
    // Extracting from a source ADS folder removes the base and colon.
    return validateExtractionOutput(destination, output, false, error);
}

std::expected<QString, QString> ArchiveBackend::prepareExtractionDestination(
    const QString &destination, const QStringList &selected, const ExtractionOptions &options,
    bool nativeSource, const QString &sourceFolder, const CArc *nativeMetadata) const {
    QString error;
    const auto validate = [&](const QString &path) {
        return validateExtraction(path, selected, options, &error, nativeSource, sourceFolder,
                                  nativeMetadata);
    };
    if (!validate(destination))
        return std::unexpected(error);
    const QString output = QFileInfo(destination).absoluteFilePath();
    if (!QDir().mkpath(output))
        return std::unexpected(
            QCoreApplication::translate("archivebackend", "Could not create destination folder: %1")
                .arg(destination));
    if (!validate(output))
        return std::unexpected(error);
    return output;
}

bool ArchiveBackend::validateExtraction(const QString &destination, const QStringList &selected,
                                        const ExtractionOptions &options, QString *error,
                                        bool nativeSource, const QString &sourceFolder,
                                        const CArc *nativeMetadata) const {
    QList<ArchiveEntry> nativeEntries;
    if (nativeMetadata) {
        const HRESULT result = NativeExtractionEntries(*nativeMetadata, nativeEntries);
        if (result != S_OK) {
            *error = QCoreApplication::translate(
                "mainwindow_archivecommands", "Source extraction is unavailable for this archive.");
            return false;
        }
    }
    return validateExtractionEntries(nativeMetadata ? nativeEntries : entries_, destination,
                                     selected, options, error, nativeSource, sourceFolder);
}

bool ArchiveBackend::validateExtractionEntries(const QList<ArchiveEntry> &entries,
                                               const QString &destination,
                                               const QStringList &selected,
                                               const ExtractionOptions &options, QString *error,
                                               bool nativeSource, const QString &sourceFolder) {
    if (destination.trimmed().isEmpty()) {
        *error = QCoreApplication::translate("archivebackend_validation",
                                             "Specify a destination folder.");
        return false;
    }
    QFileInfo target(destination);
    QString ancestor = QDir::cleanPath(target.absoluteFilePath());
    while (true) {
        const QFileInfo part(ancestor);
        // macOS exposes temporary directories through /var or /tmp, which are system aliases
        // for /private/var and /private/tmp. Archive previews use QTemporaryDir beneath them.
#ifdef Q_OS_MACOS
        const bool systemAlias = ancestor == QStringLiteral("/var") ||
                                 ancestor == QStringLiteral("/tmp") ||
                                 ancestor == QStringLiteral("/etc");
#else
        const bool systemAlias = false;
#endif
        if (part.isSymLink() && !systemAlias) {
            *error = QCoreApplication::translate("archivebackend_validation",
                                                 "Destination path contains a link: %1")
                         .arg(ancestor);
            return false;
        }
        if (part.exists() && !part.isDir()) {
            *error = QCoreApplication::translate("archivebackend_validation",
                                                 "Destination path is occupied by a file: %1")
                         .arg(ancestor);
            return false;
        }
        if (ancestor == QDir::rootPath())
            break;
        const QString parent = QFileInfo(ancestor).absolutePath();
        if (parent == ancestor)
            break;
        ancestor = parent;
    }

    QSet<QString> names;
    QSet<QString> files;
    QSet<QString> selectedPaths;
    for (QString path : selected) {
        path.replace('\\', '/');
        selectedPaths.insert(path);
    }
    QString currentFolder = options.currentFolder;
    currentFolder.replace('\\', '/');
    QString boundFolder = sourceFolder;
    boundFolder.replace('\\', '/');
    const ExtractionPathContext paths{target, options, currentFolder, boundFolder, nativeSource};
    for (const ArchiveEntry &entry : entries) {
        QString entryPath = entry.path;
        entryPath.replace('\\', '/');
        if (!selectedPaths.isEmpty() && !selectedPaths.contains(entryPath))
            continue;
        const auto projected = ExtractionRelativePath(paths, entry, entryPath);
        if (!projected) {
            *error = projected.error();
            return false;
        }
        const QString &relative = *projected;
        const QString key = relative.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (names.contains(key)) {
            *error = QCoreApplication::translate("archivebackend_validation",
                                                 "Archive contains conflicting names: %1")
                         .arg(entry.path);
            return false;
        }
        names.insert(key);
        if (!entry.directory || entry.link)
            files.insert(key);
        QString candidate = target.absoluteFilePath();
        const QStringList parts = relative.split('/');
        for (int index = 0; index < parts.size(); ++index) {
            const QString &part = parts.at(index);
            // Each component already belongs to the bounded relative output.
            // A stream such as C:stream must not become drive-relative again.
            candidate = QDir::cleanPath(candidate + '/' + part);
            const QFileInfo existing(candidate);
            const bool finalFile = index == parts.size() - 1 && !entry.directory;
            if (existing.isSymLink() ||
                (existing.exists() && (finalFile ? !existing.isFile() : !existing.isDir()))) {
                *error = QCoreApplication::translate(
                             "archivebackend_validation",
                             "Destination path is occupied by a file or link: %1")
                             .arg(candidate);
                return false;
            }
        }
        if (QFileInfo(candidate).exists() && !entry.directory &&
            options.overwriteMode == ExtractionOptions::OverwriteMode::RejectExisting) {
            *error = QCoreApplication::translate("archivebackend_validation",
                                                 "Already exists in the destination folder: %1")
                         .arg(entry.path);
            return false;
        }
    }
    for (const QString &name : names) {
        QString parent = name;
        while (parent.contains('/')) {
            parent = parent.section('/', 0, -2);
            if (files.contains(parent)) {
                *error = QCoreApplication::translate("archivebackend_validation",
                                                     "Archive contains a file/folder conflict: %1")
                             .arg(name);
                return false;
            }
        }
    }
    return true;
}
