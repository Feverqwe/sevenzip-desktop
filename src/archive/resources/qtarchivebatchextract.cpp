#include "archive/resources/qtarchivebatchextract.h"
#include "7zip/UI/Common/SortUtils.h"
#include "Common/StringConvert.h"
#include "archive/resources/qtarchivebatchopen.h"
#include "archive/resources/qtextractpathpolicy.h"
#include "gui/ExtractGUI.h"
#include <QDir>
#include <QFileInfo>

QtArchiveBatchResult RunQtArchiveBatchExtract(ArchiveBackend &backend, const QStringList &archives,
                                              const QString &destination, const QString &password,
                                              const ExtractionOptions &options, QWidget *parent) {
    QtArchiveBatchResult result;
    if (archives.isEmpty() || destination.isEmpty())
        return result;
    CCodecs codecs;
    CObjectVector<COpenType> types;
    CIntVector excludedFormats;
    UStringVector archivePaths, archivePathsFull;
    for (const QString &path : archives) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        archivePaths.Add(UString(absolute.toStdWString().c_str()));
    }
    // Extract's volume lookup indexes the processing list and its size/skip
    // arrays. Build both native path lists from the same filename ordering.
    CUIntVector order;
    SortFileNames(archivePaths, order);
    FOR_VECTOR(i, order) { archivePathsFull.Add(archivePaths[order[i]]); }
    archivePaths = archivePathsFull;
    NWildcard::CCensorNode censor;
    censor.Add_Wildcard();
    CExtractOptions sourceOptions;
    CQtExtractPathPolicy pathPolicy(destination);
    sourceOptions.NtOptions.QtPathPolicy = &pathPolicy;
    sourceOptions.OutputDir =
        us2fs(UString(QDir::toNativeSeparators(destination).toStdWString().c_str()));
    sourceOptions.OutDirMode = NExtractOutDirMode::k_Direct;
    sourceOptions.PathMode = options.pathMode == ExtractionOptions::PathMode::NoPaths
                                 ? NExtract::NPathMode::kNoPaths
                             : options.pathMode == ExtractionOptions::PathMode::AbsolutePaths
                                 ? NExtract::NPathMode::kAbsPaths
                                 : NExtract::NPathMode::kFullPaths;
    sourceOptions.ElimDup = options.ElimDup;
    sourceOptions.NtOptions.NtSecurity = options.NtSecurity;
    switch (options.overwriteMode) {
    case ExtractionOptions::OverwriteMode::Ask:
        sourceOptions.OverwriteMode = NExtract::NOverwriteMode::kAsk;
        break;
    case ExtractionOptions::OverwriteMode::OverwriteExisting:
        sourceOptions.OverwriteMode = NExtract::NOverwriteMode::kOverwrite;
        break;
    case ExtractionOptions::OverwriteMode::SkipExisting:
        sourceOptions.OverwriteMode = NExtract::NOverwriteMode::kSkip;
        break;
    case ExtractionOptions::OverwriteMode::RenameExtracted:
        sourceOptions.OverwriteMode = NExtract::NOverwriteMode::kRename;
        break;
    case ExtractionOptions::OverwriteMode::RenameExisting:
        sourceOptions.OverwriteMode = NExtract::NOverwriteMode::kRenameExisting;
        break;
    case ExtractionOptions::OverwriteMode::RejectExisting:
        return result;
    }
    CMyComPtr2_Create<IFolderArchiveExtractCallback, CExtractCallbackImp> callback;
    callback->MultiArcMode = archives.size() > 1;
    callback->OverwriteMode = sourceOptions.OverwriteMode;
    callback->SettingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    callback->Password = UString(password.toStdWString().c_str());
    callback->PasswordIsDefined = !password.isEmpty();
    QtArchiveBatchOpen open(backend);
    open.SetExtractionPolicy(destination, options);
    result.Result = ExtractGUI(&codecs, types, excludedFormats, archivePaths, archivePathsFull,
                               censor, sourceOptions, nullptr, false, result.MessageWasDisplayed,
                               callback.ClsPtr(), &open, &open, parent);
    result.CallbackOK = callback->IsOK();
    return result;
}
