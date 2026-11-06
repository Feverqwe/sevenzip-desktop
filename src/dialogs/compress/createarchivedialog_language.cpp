// Qt presentation bindings for CompressDialog.cpp/CompressDialog.rc (0766b733).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/qtlangcontrols.h"
#include <QEvent>
#include <QSignalBlocker>

void CreateArchiveDialog::BindLanguage() {
    QtBindResourceText(this, IDD_COMPRESS, "Add to Archive");
    const auto row = [this](QWidget *field, UInt32 id, const QString &caption) {
        if (!field)
            return;
        QtBindResourceText(field, id, caption, true);
        for (auto *form : findChildren<QFormLayout *>())
            if (QWidget *label = form->labelForField(field))
                QtBindResourceText(label, id, caption);
    };
    for (auto *widget :
         {static_cast<QWidget *>(archivePath_), findChild<QWidget *>("archivePathRow"),
          static_cast<QWidget *>(findChild<QLabel *>("archiveLabel"))})
        QtBindLocalText(widget, "ArchiveRedesign",
                        QT_TRANSLATE_NOOP("ArchiveRedesign", "Archive name"),
                        widget != findChild<QLabel *>("archiveLabel"));
    QtBindResourceText(findChild<QPushButton *>("archiveBrowse"), IDS_COMPRESS_SET_ARCHIVE_BROWSE,
                       "Specify archive name", true);
    row(format_, IDT_COMPRESS_FORMAT, "Archive &format:");
    row(updateMode_, IDT_COMPRESS_UPDATE_MODE, "&Update mode:");
    row(pathMode_, IDT_COMPRESS_PATH_MODE, "Path mode:");
    row(level_, IDT_COMPRESS_LEVEL, "Compression &level:");
    row(method_, IDT_COMPRESS_METHOD, "Compression &method:");
    row(dictionary_, IDT_COMPRESS_DICTIONARY, "&Dictionary size:");
    row(wordSize_, IDT_COMPRESS_ORDER, "&Word size:");
    row(solidBlock_, IDT_COMPRESS_SOLID, "&Solid Block size:");
    row(threads_->parentWidget(), IDT_COMPRESS_THREADS, "Number of CPU &threads:");
    QtBindResourceText(threads_, IDT_COMPRESS_THREADS, "Number of CPU &threads:", true);
    QtBindLocalText(memUse_, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Compression memory limit:"), true);
    QtBindLocalText(memUse_->parentWidget(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Compression memory limit:"), true);
    for (auto *form : findChildren<QFormLayout *>())
        if (auto *label = form->labelForField(memUse_->parentWidget()))
            QtBindLocalText(label, "ArchiveRedesign",
                            QT_TRANSLATE_NOOP("ArchiveRedesign", "Compression memory limit:"));
    row(parameters_, IDT_COMPRESS_PARAMETERS, "Parameters:");
    row(volume_, IDT_SPLIT_TO_VOLUMES, "Split to &volumes, bytes:");
    for (const auto &entry :
         {qMakePair(password_, QT_TRANSLATE_NOOP("ArchiveRedesign", "Password")),
          qMakePair(passwordConfirmation_,
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Repeat password"))}) {
        QtBindLocalText(entry.first, "ArchiveRedesign", entry.second, true);
        QtBindLocalText(encryptionForm_->labelForField(entry.first), "ArchiveRedesign",
                        entry.second);
    }
    row(encryption_, IDT_COMPRESS_ENCRYPTION_METHOD, "&Encryption method:");
    if (auto *label = findChild<QLabel *>("archiveEncryptionMethodLabel"))
        QtBindResourceText(label, IDT_COMPRESS_ENCRYPTION_METHOD, "&Encryption method:");
    QtBindResourceText(sharedFiles_, IDX_COMPRESS_SHARED, "Compress shared files");
    QtBindResourceText(sfx_, IDX_COMPRESS_SFX, "Create SFX archive");
    QtBindResourceText(deleteAfterCompressing_, IDX_COMPRESS_DEL, "Delete files after compression");
    if (password_) {
        QtBindResourceText(showPassword_, IDX_PASSWORD_SHOW, "Show Password");
        QtBindResourceText(encryptHeaders_, IDX_COMPRESS_ENCRYPT_FILE_NAMES, "Encrypt file &names");
    }
    ReloadLanguageItems();
}

QString CreateArchiveDialog::LevelText(unsigned level) {
    // Source SetLevel2 uses these language IDs beside the unchanged numeric level.
    const UInt32 levels[] = {IDS_METHOD_STORE,
                             IDS_METHOD_FASTEST,
                             0,
                             IDS_METHOD_FAST,
                             0,
                             IDS_METHOD_NORMAL,
                             0,
                             IDS_METHOD_MAXIMUM,
                             0,
                             IDS_METHOD_ULTRA};
    QString text = QString::number(level);
    if (level < Z7_ARRAY_SIZE(levels) && levels[level])
        text += " - " + QtLangText(levels[level]);
    return text;
}

void CreateArchiveDialog::ReloadLanguageItems() {
    const QSignalBlocker levelBlock(level_), pathBlock(pathMode_);
    for (int i = 0; i < level_->count(); ++i)
        level_->setItemText(i, LevelText(level_->itemData(i).toUInt()));
    const UInt32 paths[] = {IDS_PATH_MODE_RELAT, IDS_EXTRACT_PATHS_FULL, IDS_EXTRACT_PATHS_ABS};
    for (int i = 0; i < pathMode_->count(); ++i)
        pathMode_->setItemText(i, QtLangText(paths[i]).remove('&'));
    {
        const QSignalBlocker block(solidBlock_);
        for (int i = 1; i < solidBlock_->count(); ++i) {
            const UInt32 log = solidBlock_->itemData(i).toUInt();
            if (log == 0 || log == 64)
                solidBlock_->setItemText(
                    i, QtLangText(log == 0 ? IDS_COMPRESS_NON_SOLID : IDS_COMPRESS_SOLID));
        }
    }
    {
        const QSignalBlocker block(memUse_);
        int selected;
        const auto items = NCompressDialog::SetMemUseCombo(Get_FormatOptions().MemUse, &selected);
        if (!items.isEmpty() && memUse_->count())
            memUse_->setItemText(0, items.first().User);
    }
    if (updateMode_) {
        const QSignalBlocker updateBlock(updateMode_);
        const UInt32 ids[] = {IDS_COMPRESS_UPDATE_MODE_ADD, IDS_COMPRESS_UPDATE_MODE_UPDATE,
                              IDS_COMPRESS_UPDATE_MODE_FRESH, IDS_COMPRESS_UPDATE_MODE_SYNC};
        for (int i = 0; i < updateMode_->count(); ++i)
            updateMode_->setItemText(i, QtLangText(ids[i]).remove('&'));
    }
}

void CreateArchiveDialog::changeEvent(QEvent *event) {
    if ((event->type() == QEvent::LanguageChange || event->type() == QEvent::FontChange) &&
        buttons_) {
        if (event->type() == QEvent::LanguageChange) {
            ReloadLanguageItems();
            SetMemoryUsage();
            updateRedesignSummary();
        }
        stabilizeColumnWidths();
    }
    QDialog::changeEvent(event);
}
