#include "fileops/volumes/qtvolumetransaction.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QUuid>

QtVolumeTransaction::~QtVolumeTransaction() {
    for (const auto &output : outputs_)
        QFile::remove(output.ownedPath);
}

bool QtVolumeTransaction::create(NWindows::NFile::NIO::COutFile &file, const FString &target) {
    const QString path = ToQString(fs2us(target));
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        failedPath_ = path;
        return false;
    }
    const QString stage = path + ".sevenzip-" + QUuid::createUuid().toString(QUuid::Id128);
    const FString nativeStage = us2fs(ToUString(stage));
    // Register both names before creating a file that rollback must own.
    outputs_.append(Output{path, stage});
    auto removeOutput = qScopeGuard([&] { outputs_.removeLast(); });
    if (!file.Create_NEW(nativeStage)) {
        failedPath_ = path;
        return false;
    }
    removeOutput.dismiss();
    return true;
}

bool QtVolumeTransaction::publish() {
    for (auto &output : outputs_) {
        const QString &target = output.target;
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink() ||
            !QFile::rename(output.ownedPath, target)) {
            failedPath_ = target;
            return false;
        }
        output.ownedPath = target;
    }
    outputs_.clear();
    return true;
}
