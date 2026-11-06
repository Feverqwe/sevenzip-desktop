#include "Common/StringConvert.h"
#include "filemanager/window/qtexternalfiletask_p.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QApplication>
#include <QDir>

QtExternalFileTask::QtExternalFileTask(const QtOwnedTempEntry &owner,
                                       const QtExternalFileRecord &record,
                                       const ArchiveBackend &backend, const QString &settings)
    : state_(std::make_unique<State>()) {
    state_->record = record;
    state_->retained = record.retainedInputs;
    state_->recovered = true;
    state_->persisted = true;
    state_->launched = true;
    state_->observation = QtExternalObservation::Unknown;
    state_->directoryIdentity = owner.identity;
    state_->prefix = record.prefix;
    state_->file.FolderPath = us2fs(ToUString(owner.path));
    state_->file.FilePath = us2fs(ToUString(QDir(owner.path).filePath(record.fileName)));
    state_->file.RelPath = ToUString(record.entry);
    state_->file.FileInfo.Size = record.initial.size;
    state_->file.FileInfo.MTime = record.initial.modified;
    state_->published = record.published.has_value();
    if (!state_->published)
        state_->target = QtExternalArchiveTarget::Restore(record.target, backend, settings);
    else {
        NWindows::NFile::NFind::CFileInfo current;
        const bool changed = current.Find(state_->file.FilePath) &&
                             (current.Size != record.published->size ||
                              Compare_FiTime(&current.MTime, &record.published->modified) != 0);
        state_->message = record.publicationError
                              ? QT_TR_NOOP("The archive was saved, but completing the operation "
                                           "failed. The copy was kept.")
                          : changed ? QT_TR_NOOP("The archive was updated, but the temporary file "
                                                 "changed again. Its copy was kept.")
                                    : QT_TR_NOOP("The archive was updated.");
    }
    if (state_->target)
        state_->prefix = state_->target->description();
    Initialize();
}

bool QtExternalFileTask::Persist() {
    if (!state_->record)
        return false;
    const auto context = state_->record->Save();
    if (state_->file.OwnedTempLease)
        return state_->file.OwnedTempLease->SetContext(context);
    const QString directory =
        QDir::cleanPath(QDir::fromNativeSeparators(ToQString(fs2us(state_->file.FolderPath))));
    QtOwnedTempRegistry registry;
    for (const auto &entry : registry.Entries())
        if (entry.path == directory && entry.identity == state_->directoryIdentity)
            return registry.SetContext(entry, context);
    return false;
}
