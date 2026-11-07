#include "Common/StringConvert.h"
#include "filemanager/window/qtexternalfiletask_p.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QDir>

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
