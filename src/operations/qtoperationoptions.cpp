#include "operations/qtoperationoptions.h"
#include "7zip/UI/Common/ArchiveCommandLine.h"
#include "Windows/FileName.h"
#include "operations/qtoperationprotocol.h"
#include "platform/memory/qtmemorylockservices.h"
#include "settings/RegistryUtils.h"

void ConfigureOperationOptions(const QtOperationJob &job, CArcCmdLineOptions &options) {
    // Parse the source command defaults, without putting selection in argv or
    // recreating Parse1/Parse2's initialization/platform policy in Qt glue.
    CArcCmdLineParser parser;
    UStringVector defaults;
    const bool extract = job.Operation == QtOperationJob::Command::Extract;
    const bool compress = job.Operation == QtOperationJob::Command::Compress;
    const bool hash = job.Operation == QtOperationJob::Command::Hash;
    defaults.Add(hash ? L"h" : compress ? L"a" : extract ? L"x" : L"t");
    defaults.Add(L"-an");
    if (job.HashMode)
        defaults.Add(L"-thash");
    if (hash && !job.HashMethod.isEmpty())
        defaults.Add(UString(L"-scrc") + UString(job.HashMethod.toStdWString().c_str()));
    if (compress) {
        defaults.Add(job.AddExtension ? L"-saa" : L"-sae");
        if (!job.ArchiveType.isEmpty())
            defaults.Add(UString(L"-t") + UString(job.ArchiveType.toStdWString().c_str()));
        if (job.EMail)
            defaults.Add(L"-seml.");
    }
    UString memorySwitch;
    AddLagePagesSwitch(memorySwitch, FmSettingsPath());
    memorySwitch.Trim();
    if (!memorySwitch.IsEmpty())
        defaults.Add(memorySwitch);
    if (job.YesToAll)
        defaults.Add(L"-y");
    parser.Parse1(defaults, options);
    parser.Parse2(options);
    if (hash) {
        options.Censor.CensorPaths.Clear();
        options.Censor.Pairs.Clear();
        if (job.HashBasePrefix.isEmpty()) {
            for (const auto &path : job.Paths)
                options.Censor.AddPreItem_NoWildcard(UString(path.toStdWString().c_str()));
            options.Censor.AddPathsToCensor(NWildcard::k_RelatPath);
        }
        return;
    }
    if (compress) {
        options.ArchiveName = job.ArchivePath.toStdWString().c_str();
        options.ShowDialog = job.ShowDialog;
        options.Censor.CensorPaths.Clear();
        options.Censor.Pairs.Clear();
        for (const auto &path : job.Paths)
            options.Censor.AddPreItem_NoWildcard(UString(path.toStdWString().c_str()));
        // UpdateGUI needs the literal pre-items for ShowDialog. Common/Update
        // performs the source path conversion/enumeration in its worker.
        return;
    }
    if (extract) {
        options.ShowDialog = job.ShowDialog;
        options.ExtractOptions.OutputDir = us2fs(job.Destination.toStdWString().c_str());
#ifdef _WIN32
        NWindows::NFile::NName::NormalizeDirSeparators(options.ExtractOptions.OutputDir);
#endif
        NWindows::NFile::NName::NormalizeDirPathPrefix(options.ExtractOptions.OutputDir);
        if (job.ElimDup)
            options.ExtractOptions.ElimDup.Val = options.ExtractOptions.ElimDup.Def = true;
        if (job.WriteZone != quint32(-1))
            options.ExtractOptions.ZoneMode = NExtract::NZoneIdMode::EEnum(job.WriteZone);
    }
    // CompressCall2's literal archive selection. Main2 retains enumeration,
    // errors, callbacks, dialogs and the original test/exit policy.
    options.arcCensor.CensorPaths.Clear();
    options.arcCensor.Pairs.Clear();
    for (const auto &path : job.Paths)
        options.arcCensor.AddPreItem_NoWildcard(UString(path.toStdWString().c_str()));
    options.arcCensor.AddPathsToCensor(NWildcard::k_RelatPath);
    options.arcCensor.ExtendExclude();
}
