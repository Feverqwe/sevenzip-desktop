// ExtractGUI.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "7zip/UI/Common/StdAfx.h"

#include "Common/IntToString.h"
#include "Common/StringConvert.h"

#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/Thread.h"
#include "fileops/native/FileDir.h"

#include "7zip/UI/FileManager/OverwriteDialogRes.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"

#include "7zip/UI/Common/PropIDUtils.h"
#include "archive/callbacks/ArchiveExtractCallback.h"

#include "7zip/UI/GUI/ExtractRes.h"

#include "dialogs/extract/ExtractDialog.h"
#include "gui/ExtractGUI.h"
#include "gui/HashGUI.h"
#include <QDir>

#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "archive/callbacks/qtguimessages.h"
#include "gui/resource2.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

static const char *const kIncorrectOutDir =
    QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Incorrect output directory path");

#ifndef Z7_SFX

static void AddValuePair(UString &s, UINT resourceID, UInt64 value, bool addColon = true) {
    AddLangString(s, resourceID);
    if (addColon)
        s.Add_Colon();
    s.Add_Space();
    s.Add_UInt64(value);
    s.Add_LF();
}

static void AddSizePair(UString &s, UINT resourceID, UInt64 value) {
    AddLangString(s, resourceID);
    s += ": ";
    AddSizeValue(s, value);
    s.Add_LF();
}

#endif

class CThreadExtracting : public CProgressThreadVirt {
    HRESULT ProcessVirt() Z7_override;

  public:
    /*
    #ifdef Z7_EXTERNAL_CODECS
    const CExternalCodecs *externalCodecs;
    #endif
    */

    CCodecs *codecs;
    const CObjectVector<COpenType> *FormatIndices;
    const CIntVector *ExcludedFormatIndices;

    UStringVector *ArchivePaths;
    UStringVector *ArchivePathsFull;
    const NWildcard::CCensorNode *WildcardCensor;
    const CExtractOptions *Options;

#ifndef Z7_SFX
    CHashBundle *HashBundle;
    virtual void ProcessWasFinished_GuiVirt() Z7_override;
#endif

    CMyComPtr2<IFolderArchiveExtractCallback, CExtractCallbackImp> ExtractCallback;
    UString Title;

    CPropNameValPairs Pairs;
    IQtArchiveOpenService *QtOpenService;
};

#ifndef Z7_SFX
void CThreadExtracting::ProcessWasFinished_GuiVirt() {
    if (HashBundle && !Pairs.IsEmpty())
        ShowHashResults(Pairs, this);
}
#endif

HRESULT CThreadExtracting::ProcessVirt() {
    CDecompressStat Stat;

#ifndef Z7_SFX
/*
if (HashBundle)
  HashBundle->Init();
*/
#endif

    HRESULT res = Extract(
        /*
        #ifdef Z7_EXTERNAL_CODECS
        externalCodecs,
        #endif
        */
        codecs, *FormatIndices, *ExcludedFormatIndices, *ArchivePaths, *ArchivePathsFull,
        *WildcardCensor, *Options, ExtractCallback.ClsPtr(), ExtractCallback.ClsPtr(),
        ExtractCallback,
#ifndef Z7_SFX
        HashBundle,
#endif
        FinalMessage.ErrorMessage.Message, Stat, QtOpenService);

    FinalMessage.HadErrors = ExtractCallback->ThereAreMessageErrors;
#ifndef Z7_SFX
    FinalMessage.Partial =
        ExtractCallback->SomeItemsSkipped ||
        (ExtractCallback->ExtractReport.Status && !ExtractCallback->ExtractReport.Status->Complete);
    if (res == S_OK && ExtractCallback->IsOK()) {
        if (HashBundle) {
            AddValuePair(Pairs, IDS_ARCHIVES_COLON, Stat.NumArchives);
            AddSizeValuePair(Pairs, IDS_PROP_PACKED_SIZE, Stat.PackSize);
            AddHashBundleRes(Pairs, *HashBundle);
        } else if (Options->TestMode) {
            UString s;

            AddValuePair(s, IDS_ARCHIVES_COLON, Stat.NumArchives, false);
            AddSizePair(s, IDS_PROP_PACKED_SIZE, Stat.PackSize);

            if (Stat.NumFolders != 0)
                AddValuePair(s, IDS_PROP_FOLDERS, Stat.NumFolders);
            AddValuePair(s, IDS_PROP_FILES, Stat.NumFiles);
            AddSizePair(s, IDS_PROP_SIZE, Stat.UnpackSize);
            if (Stat.NumAltStreams != 0) {
                s.Add_LF();
                AddValuePair(s, IDS_PROP_NUM_ALT_STREAMS, Stat.NumAltStreams);
                AddSizePair(s, IDS_PROP_ALT_STREAMS_SIZE, Stat.AltStreams_UnpackSize);
            }
            s.Add_LF();
            AddLangString(s, IDS_MESSAGE_NO_ERRORS);
            FinalMessage.OkMessage.Title = Title;
            FinalMessage.OkMessage.Message = s;
        }
    }
#endif

    return res;
}

HRESULT ExtractGUI(
    // DECL_EXTERNAL_CODECS_LOC_VARS
    CCodecs *codecs, const CObjectVector<COpenType> &formatIndices,
    const CIntVector &excludedFormatIndices, UStringVector &archivePaths,
    UStringVector &archivePathsFull, const NWildcard::CCensorNode &wildcardCensor,
    CExtractOptions &options,
#ifndef Z7_SFX
    CHashBundle *hb,
#endif
    bool showDialog, bool &messageWasDisplayed, CExtractCallbackImp *extractCallback,
    IQtArchiveOpenService *qtOpenService, IQtProgressServices *progressServices,
    QWidget *hwndParent, const std::function<void()> &outputReady) {
    messageWasDisplayed = false;

    CThreadExtracting extracter;
    /*
    #ifdef Z7_EXTERNAL_CODECS
    extracter.externalCodecs = _externalCodecs;
    #endif
    */
    extracter.codecs = codecs;
    extracter.QtOpenService = qtOpenService;
    std::unique_ptr<IQtProgressTransport> transport =
        progressServices ? progressServices->CreateProgressTransport(extracter) : nullptr;
    extracter.FormatIndices = &formatIndices;
    extracter.ExcludedFormatIndices = &excludedFormatIndices;
    QStringList sourceLocations;
    FOR_VECTOR(i, archivePathsFull)
    sourceLocations.append(ToQString(archivePathsFull[i]));

    if (!options.TestMode) {
        FString outputDir = options.OutputDir;
#ifndef UNDER_CE
        if (outputDir.IsEmpty())
            GetCurrentDir(outputDir);
#endif
        if (showDialog) {
            FString outputDirFull;
            if (!MyGetFullPathName(outputDir, outputDirFull)) {
                ShowErrorMessageQt(hwndParent, QtDiagnosticText(kIncorrectOutDir));
                messageWasDisplayed = true;
                return E_FAIL;
            }
            NName::NormalizeDirPathPrefix(outputDirFull);
            CExtractDialog dialog(ToQString(fs2us(outputDirFull)), {}, hwndParent);
            if (archivePathsFull.Size() == 1)
                dialog.ArcPath = archivePathsFull[0];
            dialog.SetArchiveSources(sourceLocations, wildcardCensor.AreAllAllowed() &&
                                                          !options.ExcludeDirItems &&
                                                          !options.ExcludeFileItems);

            dialog.OverwriteMode_Force = options.OverwriteMode_Force;
            dialog.PathMode_Force = options.PathMode_Force;
            dialog.SetInitialOptions(options.PathMode, options.OverwriteMode,
                                     extractCallback->PasswordIsDefined
                                         ? ToQString(extractCallback->Password)
                                         : QString());

#ifndef Z7_SFX
            dialog.SetMetadataOptions(options.ElimDup, options.NtOptions.NtSecurity);
#endif

            if (dialog.exec() != QDialog::Accepted)
                return E_ABORT;

            outputDir = us2fs(ToUString(QDir::toNativeSeparators(dialog.DirPath)));

            options.OverwriteMode = dialog.OverwriteMode;
            options.PathMode = dialog.PathMode;
            options.ElimDup = dialog.ElimDup;

#ifndef Z7_SFX
            extractCallback->Password = dialog.Password;
            extractCallback->PasswordIsDefined = !dialog.Password.isEmpty();
            options.NtOptions.NtSecurity = dialog.NtSecurity;
#endif
        }
        if (!MyGetFullPathName(outputDir, options.OutputDir)) {
            ShowErrorMessageQt(hwndParent, QtDiagnosticText(kIncorrectOutDir));
            messageWasDisplayed = true;
            return E_FAIL;
        }
        NName::NormalizeDirPathPrefix(options.OutputDir);

        /*
        if (!CreateComplexDirectory(options.OutputDir))
        {
          UString s = GetUnicodeString(NError::MyFormatMessage(GetLastError()));
          UString s2 = MyFormatNew(IDS_CANNOT_CREATE_FOLDER,
          #ifdef Z7_LANG
          0x02000603,
          #endif
          options.OutputDir);
          s2.Add_LF();
          s2 += s;
          MyMessageBox(s2);
          return E_FAIL;
        }
        */
    }

    UString title = LangString(options.TestMode ? IDS_PROGRESS_TESTING : IDS_PROGRESS_EXTRACTING);

    extracter.Title = title;
    extractCallback->ProgressDialog = &extracter;
    extracter.ExtractCallback.SetFromCls(extractCallback);
    extracter.ExtractCallback->Init();
    extracter.DeferResultReview =
        extractCallback->ExtractReport.Purpose != QtArchiveExtractPurpose::Copy;

    extracter.CompressingMode = false;

    extracter.ArchivePaths = &archivePaths;
    extracter.ArchivePathsFull = &archivePathsFull;
    extracter.SetLocations(sourceLocations.join('\n'),
                           options.TestMode ? QString()
                                            : QString::fromWCharArray(fs2us(options.OutputDir)));
    extracter.WildcardCensor = &wildcardCensor;
    extracter.Options = &options;
#ifndef Z7_SFX
    extracter.HashBundle = hb;
#endif

    extracter.IconID = IDI_ICON;

    if (!options.TestMode && outputReady)
        outputReady();
    RINOK(extracter.Create(title, hwndParent))
    extractCallback->ExtractReport.HadErrors = !extractCallback->IsOK();
    if (extractCallback->ExtractReport.Status && extractCallback->SomeItemsSkipped)
        extractCallback->ExtractReport.Status->Complete = false;
    FOR_VECTOR(i, extracter.Sync.Messages)
    extractCallback->ExtractReport.Diagnostics.append(ToQString(extracter.Sync.Messages[i]));
    if (!extracter.Sync.FinalMessage.ErrorMessage.Message.IsEmpty())
        extractCallback->ExtractReport.Diagnostics.append(
            ToQString(extracter.Sync.FinalMessage.ErrorMessage.Message));
    messageWasDisplayed = extracter.ThreadFinishedOK && extracter.MessagesDisplayed;
    return extracter.Result;
}
