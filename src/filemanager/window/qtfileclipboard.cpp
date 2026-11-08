#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QClipboard>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSet>
#include <QtEndian>

namespace {
constexpr auto gnomeFilesFormat = "x-special/gnome-copied-files";

QStringList LocalFilePaths(const QList<QUrl> &urls) {
    QStringList paths;
    for (const auto &url : urls) {
        if (!url.isValid() || !url.isLocalFile())
            return {};
        const QString path = url.toLocalFile();
        if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null))
            return {};
        paths.append(path);
    }
    paths.removeDuplicates();
    return paths;
}

QStringList GnomeFilePaths(const QByteArray &data) {
    auto lines = data.split('\n');
    if (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast();
    if (lines.size() < 2 || (lines.front() != "cut" && lines.front() != "copy"))
        return {};
    QList<QUrl> urls;
    for (qsizetype i = 1; i < lines.size(); ++i)
        urls.append(QUrl::fromEncoded(lines[i], QUrl::StrictMode));
    return LocalFilePaths(urls);
}

QStringList ClipboardFilePaths(const QMimeData *mime) {
    if (!mime)
        return {};
    const auto urls = mime->urls();
    return urls.isEmpty() && !mime->hasUrls() ? GnomeFilePaths(mime->data(gnomeFilesFormat))
                                              : LocalFilePaths(urls);
}

bool ClipboardMovesFiles(const QMimeData &mime, const QStringList &paths) {
    bool move = mime.data("application/x-kde-cutselection") == "1";
    const auto gnome = mime.data(gnomeFilesFormat);
    if (!move && !paths.isEmpty() && gnome.startsWith("cut\n")) {
        const auto gnomePaths = GnomeFilePaths(gnome);
        move = QSet<QString>(paths.cbegin(), paths.cend()) ==
               QSet<QString>(gnomePaths.cbegin(), gnomePaths.cend());
    }
#ifdef Q_OS_WIN
    const QByteArray preferred =
        mime.data("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
    if (preferred.size() == 4)
        move |= qFromLittleEndian<quint32>(preferred.constData()) == 2;
#endif
    return move;
}

void PublishFileClipboard(const QList<QUrl> &urls, bool move) {
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    QByteArray gnome = move ? QByteArray("cut") : QByteArray("copy");
    for (const auto &url : urls)
        gnome += '\n' + url.toEncoded();
    mime->setData(gnomeFilesFormat, gnome);
    mime->setData("application/x-kde-cutselection", move ? "1" : "0");
#ifdef Q_OS_WIN
    mime->setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"",
                  QByteArray(move ? "\x02\x00\x00\x00" : "\x01\x00\x00\x00", 4));
#endif
    QApplication::clipboard()->setMimeData(mime);
}
} // namespace

QtFileClipboard::QtFileClipboard(MainWindow &window) : QObject(&window), window_(window) {
    cut = new QAction(tr("Cut"), this);
    copy = new QAction(tr("Copy"), this);
    paste = new QAction(tr("Paste"), this);
    QtBindLocalText(cut, "QtFileClipboard", QT_TRANSLATE_NOOP("QtFileClipboard", "Cut"));
    QtBindLocalText(copy, "QtFileClipboard", QT_TRANSLATE_NOOP("QtFileClipboard", "Copy"));
    QtBindLocalText(paste, "QtFileClipboard", QT_TRANSLATE_NOOP("QtFileClipboard", "Paste"));
    cut->setObjectName("cutFilesAction");
    copy->setObjectName("copyFilesAction");
    paste->setObjectName("pasteFilesAction");
    connect(cut, &QAction::triggered, this, [this] { Copy(true); });
    connect(copy, &QAction::triggered, this, [this] { Copy(false); });
    connect(paste, &QAction::triggered, this, &QtFileClipboard::Paste);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        ++revision_;
        cutDirty_ = true;
        window_.refreshActions();
    });
}

QtFileClipboard::Availability QtFileClipboard::AvailabilityForPanel(unsigned panel) const {
    const auto &ui = window_.panelUi_[panel];
    if (!ui.table)
        return {};
    auto &source = window_.app_.Panels[panel];
    const bool ready = !window_.app_.QtOperations().closed() &&
                       !window_.app_.QtOperations().active() && !window_.sourceArchiveProgress_ &&
                       !ui.locationUnavailable && !ui.scanningDisk && !ui.scanningArchive;
    CRecordVector<UInt32> indices;
    if (ready)
        source.Get_ItemIndices_Operated(indices);
    Availability available;
    available.copy = ready && !indices.IsEmpty() && source.DoesItSupportOperations();
    const QFileInfo destination(ui.directory);
    const bool writableFolder = ready && source.GetFolder() && source.IsFSFolder() &&
                                !source.IsThereReadOnlyFolder() && destination.isDir() &&
                                destination.isWritable();
    available.cut = available.copy && writableFolder;
    available.paste =
        writableFolder && !ClipboardFilePaths(QApplication::clipboard()->mimeData()).isEmpty();
    return available;
}

void QtFileClipboard::Refresh() {
    if (cutDirty_) {
        cutDirty_ = false;
        cutPaths_.clear();
        const auto *mime = QApplication::clipboard()->mimeData();
        const auto paths = ClipboardFilePaths(mime);
        if (mime && ClipboardMovesFiles(*mime, paths))
            for (const auto &path : paths)
                cutPaths_.insert(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    }
    // Hidden tabs retain their model; every window observes clipboard changes.
    // New/restored models receive the same snapshot at their next publication.
    for (auto *table : window_.findChildren<FileTable *>())
        table->panelModel()->setCutPaths(cutPaths_);
    const auto available = AvailabilityForPanel(window_.projectedPanel_);
    copy->setEnabled(available.copy);
    cut->setEnabled(available.cut);
    paste->setEnabled(available.paste);
}

void QtFileClipboard::Copy(bool move) {
    Refresh();
    if (!(move ? cut : copy)->isEnabled())
        return;
    auto &source = window_.app_.GetFocusedPanel();
    QtAppOperationState::CallScope call(window_.app_.QtOperations());
    CRecordVector<UInt32> indices;
    source.Get_ItemIndices_Operated(indices);
    QList<QUrl> urls;
    if (source.IsFSFolder()) {
        UStringVector paths;
        source.GetFilePaths(indices, paths);
        FOR_VECTOR(i, paths)
        urls.append(QUrl::fromLocalFile(ToQString(paths[i])));
    } else {
        // Materialize with the same native copy owner as drag/export. Never
        // expose URLs until the engine reports a complete successful copy.
        const auto revision = revision_;
        NWindows::NFile::NDir::CTempDir temporary;
        if (!temporary.Create(FTEXT("7zClipboard")))
            return;
        const QString directory = ToQString(fs2us(temporary.GetPath()));
        CCopyToOptions options;
        options.folder = ToUString(QDir::fromNativeSeparators(directory) + '/');
        UStringVector messages;
        QtArchiveExtractReport report;
        report.Purpose = QtArchiveExtractPurpose::Copy;
        HRESULT result;
        {
            QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
            result = source.CopyTo(options, indices, &messages, &report);
        }
        if (result != S_OK || !messages.IsEmpty() || revision_ != revision)
            return;
        if (source.IsArcFolder() && (!report.Status || !report.Status->Complete)) {
            window_.showStatusMessage(tr(
                "Complete extraction could not be confirmed. The clipboard was kept unchanged."));
            return;
        }
        for (const auto &file : QDir(directory).entryInfoList(
                 QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            urls.append(QUrl::fromLocalFile(file.absoluteFilePath()));
        if (urls.isEmpty() || !temporary.OwnedLease() || !temporary.OwnedLease()->ProtectExternal())
            return;
        // Clipboard consumers may still copy after replacing the clipboard.
        // Retain the existing protected-temp registry's cleanup contract.
        temporary.DisableDeleting();
    }
    if (urls.isEmpty())
        return;
    PublishFileClipboard(urls, move);
}

void QtFileClipboard::Paste() {
    Refresh();
    if (!paste->isEnabled())
        return;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    const QStringList paths = ClipboardFilePaths(mime);
    if (paths.isEmpty())
        return;
    const bool move = ClipboardMovesFiles(*mime, paths);
    // Existing transfer owns collisions, path validation, cancellation,
    // metadata and progress; clipboard only supplies source URLs/destination.
    const QPointer<const QMimeData> original(mime);
    const QPointer<QtFileClipboard> owner(this);
    const auto revision = revision_;
    const QString destination = window_.projectedUi().directory;
    window_.transferFiles(paths, destination, move, false,
                          [owner, original, paths, revision](const QStringList &movedSources) {
                              if (!owner)
                                  return;
                              if (movedSources.isEmpty() || owner->revision_ != revision ||
                                  !original || QApplication::clipboard()->mimeData() != original ||
                                  ClipboardFilePaths(original) != paths ||
                                  !ClipboardMovesFiles(*original, paths))
                                  return;
                              const QSet<QString> moved(movedSources.cbegin(), movedSources.cend());
                              QList<QUrl> remaining;
                              for (const auto &path : paths)
                                  if (!moved.contains(path))
                                      remaining.append(QUrl::fromLocalFile(path));
                              if (remaining.isEmpty())
                                  QApplication::clipboard()->clear();
                              else
                                  PublishFileClipboard(remaining, true);
                          });
}
