#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QClipboard>
#include <QScopedValueRollback>
#include <QtEndian>

namespace {
bool ClipboardMovesFiles(const QMimeData &mime) {
    bool move = mime.data("application/x-kde-cutselection") == "1" ||
                mime.data("x-special/gnome-copied-files").startsWith("cut\n");
#ifdef Q_OS_WIN
    const QByteArray preferred =
        mime.data("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
    if (preferred.size() == 4)
        move |= qFromLittleEndian<quint32>(preferred.constData()) == 2;
#endif
    return move;
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
                       !ui.locationUnavailable;
    CRecordVector<UInt32> indices;
    if (ready)
        source.Get_ItemIndices_Operated(indices);
    Availability available;
    available.copy = ready && !indices.IsEmpty() && source.DoesItSupportOperations();
    available.cut = available.copy && source.IsFSFolder() && !source.IsThereReadOnlyFolder();
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    available.paste = ready && source.GetFolder() && source.IsFSFolder() &&
                      !source.IsThereReadOnlyFolder() && mime && mime->hasUrls();
    return available;
}

void QtFileClipboard::Refresh() {
    if (cutDirty_) {
        cutDirty_ = false;
        cutPaths_.clear();
        const auto *mime = QApplication::clipboard()->mimeData();
        if (mime && ClipboardMovesFiles(*mime))
            for (const auto &url : mime->urls()) {
                if (!url.isLocalFile()) {
                    cutPaths_.clear();
                    break;
                }
                cutPaths_.insert(QDir::cleanPath(QDir::fromNativeSeparators(url.toLocalFile())));
            }
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
        if (result != S_OK || !messages.IsEmpty())
            return;
        if (source.IsArcFolder() && (!report.Status || !report.Status->Complete)) {
            window_.statusBar()->showMessage(tr(
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
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    QByteArray gnome = move ? QByteArray("cut") : QByteArray("copy");
    for (const auto &url : urls)
        gnome += '\n' + url.toEncoded();
    mime->setData("x-special/gnome-copied-files", gnome);
    mime->setData("application/x-kde-cutselection", move ? "1" : "0");
#ifdef Q_OS_WIN
    mime->setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"",
                  QByteArray(move ? "\x02\x00\x00\x00" : "\x01\x00\x00\x00", 4));
#endif
    QApplication::clipboard()->setMimeData(mime);
}

void QtFileClipboard::Paste() {
    Refresh();
    if (!paste->isEnabled())
        return;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    QStringList paths;
    for (const auto &url : mime->urls()) {
        if (!url.isLocalFile())
            return;
        paths.append(url.toLocalFile());
    }
    const bool move = ClipboardMovesFiles(*mime);
    if (paths.isEmpty())
        return;
    // Existing transfer owns collisions, path validation, cancellation,
    // metadata and progress; clipboard only supplies source URLs/destination.
    const QPointer<const QMimeData> original(mime);
    window_.transferFiles(
        paths, window_.projectedUi().directory, move, false, [original, move](bool success) {
            if (move && success && original && QApplication::clipboard()->mimeData() == original)
                QApplication::clipboard()->clear();
        });
    window_.app_.Panels[window_.projectedPanel_].MarkPortableFolderChanged();
}
