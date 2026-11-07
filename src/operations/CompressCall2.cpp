// Adapted from 7-Zip CPP/7zip/UI/Common/CompressCall2.cpp.
// Baseline 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17, Archive grouping and checksum
// entries. Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/MyException.h"
#include "archive/operations/EnumDirItems.h"
#include "dialogs/compress/qtcompresscallservices.h"
#include "gui/UpdateCallbackGUI.h"
#include "operations/CompressCall.h"

// Original in-process archive grouping and checksum workflows. The catalog,
// GUI/open transport and error presentation are injected instead of globals.
static void ThrowException_if_Error(HRESULT res) {
    if (res != S_OK)
        throw CSystemException(res);
}
#define MY_TRY_BEGIN                                                                               \
    HRESULT result;                                                                                \
    try {
#define MY_TRY_FINISH                                                                              \
    }                                                                                              \
    catch (CSystemException & e) {                                                                 \
        result = e.ErrorCode;                                                                      \
    }                                                                                              \
    catch (UString & s) {                                                                          \
        services.ShowErrorMessage(s);                                                              \
        result = E_FAIL;                                                                           \
    }                                                                                              \
    catch (...) {                                                                                  \
        result = E_FAIL;                                                                           \
    }                                                                                              \
    if (result != S_OK && result != E_ABORT)                                                       \
        services.ErrorMessageHRESULT(result);
HRESULT IQtCompressCallServices::CompressSelection(const UString &folder, const UString &name,
                                                   const UString &type, bool addExtension,
                                                   const UStringVector &paths, bool email,
                                                   bool showDialog, bool waitFinish) {
    return ::CompressFiles(folder, name, type, addExtension, paths, email, showDialog, waitFinish,
                           *this);
}

HRESULT CompressFiles(const UString &arcPathPrefix, const UString &arcName, const UString &arcType,
                      bool addExtension, const UStringVector &names, bool email, bool showDialog,
                      bool /* waitFinish */, IQtCompressCallServices &services) {
    MY_TRY_BEGIN

    CCodecs *codecs = &services.Codecs();

    CUpdateCallbackGUI callback;

    callback.Init();

    CUpdateOptions uo;
    uo.EMailMode = email;
    uo.SetActionCommand_Add();

    uo.ArcNameMode = (addExtension ? k_ArcNameMode_Add : k_ArcNameMode_Exact);

    CObjectVector<COpenType> formatIndices;
    if (!ParseOpenTypes(*codecs, arcType, formatIndices)) {
        services.ShowErrorMessageRes(IDS_UNSUPPORTED_ARCHIVE_TYPE);
        return E_FAIL;
    }
    const UString arcPath = arcPathPrefix + arcName;
    if (!uo.InitFormatIndex(codecs, formatIndices, arcPath) || !uo.SetArcPath(codecs, arcPath)) {
        services.ShowErrorMessageRes(IDS_UPDATE_NOT_SUPPORTED);
        return E_FAIL;
    }

    NWildcard::CCensor censor;
    FOR_VECTOR(i, names) { censor.AddPreItem_NoWildcard(names[i]); }

    bool messageWasDisplayed = false;

    result = services.UpdateGUI(*codecs, formatIndices, arcPath, censor, uo, showDialog,
                                messageWasDisplayed, callback);

    if (result != S_OK) {
        if (result != E_ABORT && messageWasDisplayed)
            return E_FAIL;
        throw CSystemException(result);
    }
    if (callback.FailedFiles.Size() > 0) {
        if (!messageWasDisplayed)
            throw CSystemException(E_FAIL);
        return E_FAIL;
    }

    MY_TRY_FINISH
    return S_OK;
}

static HRESULT ExtractGroupCommand(const UStringVector &arcPaths, bool showDialog,
                                   CExtractOptions &eo, IQtCompressCallServices &services,
                                   const char *kType = NULL) {
    MY_TRY_BEGIN

    CCodecs *codecs = &services.Codecs();

    CMyComPtr2_Create<IFolderArchiveExtractCallback, CExtractCallbackImp> ecs;

    ecs->Init();

    // eo.CalcCrc = options.CalcCrc;

    UStringVector arcPathsSorted;
    UStringVector arcFullPathsSorted;
    {
        NWildcard::CCensor arcCensor;
        FOR_VECTOR(i, arcPaths) { arcCensor.AddPreItem_NoWildcard(arcPaths[i]); }
        arcCensor.AddPathsToCensor(NWildcard::k_RelatPath);
        CDirItemsStat st;
        EnumerateDirItemsAndSort(arcCensor, NWildcard::k_RelatPath, UString(), arcPathsSorted,
                                 arcFullPathsSorted, st,
                                 NULL // &scan: change it!!!!
        );
    }

    CObjectVector<COpenType> formatIndices;
    if (kType) {
        if (!ParseOpenTypes(*codecs, UString(kType), formatIndices)) {
            throw CSystemException(E_INVALIDARG);
            // ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE);
            // return E_INVALIDARG;
        }
    }

    NWildcard::CCensor censor;
    {
        censor.AddPreItem_Wildcard();
    }

    censor.AddPathsToCensor(NWildcard::k_RelatPath);

    bool messageWasDisplayed = false;

    ecs->MultiArcMode = (arcPathsSorted.Size() > 1);

    result = services.ExtractGUI(*codecs, formatIndices, arcPathsSorted, arcFullPathsSorted,
                                 censor.Pairs.Front().Head, eo, showDialog, messageWasDisplayed,
                                 *ecs.ClsPtr());

    if (result != S_OK) {
        if (result != E_ABORT && messageWasDisplayed)
            return E_FAIL;
        throw CSystemException(result);
    }
    return ecs->IsOK() ? S_OK : E_FAIL;

    MY_TRY_FINISH
    return result;
}

HRESULT ExtractArchives(const UStringVector &arcPaths, const UString &outFolder, bool showDialog,
                        bool elimDup, UInt32 writeZone, IQtCompressCallServices &services) {
    CExtractOptions eo;
    eo.OutputDir = us2fs(outFolder);
    eo.TestMode = false;
    eo.ElimDup.Val = elimDup;
    eo.ElimDup.Def = elimDup;
    if (writeZone != (UInt32)(Int32)-1)
        eo.ZoneMode = (NExtract::NZoneIdMode::EEnum)writeZone;
    return ExtractGroupCommand(arcPaths, showDialog, eo, services);
}

HRESULT TestArchives(const UStringVector &arcPaths, bool hashMode,
                     IQtCompressCallServices &services) {
    CExtractOptions eo;
    eo.TestMode = true;
    return ExtractGroupCommand(arcPaths,
                               true, // showDialog
                               eo, services, hashMode ? "hash" : NULL);
}

HRESULT CalcChecksum(const UStringVector &paths, const UString &methodName,
                     const UString &arcPathPrefix, const UString &arcFileName,
                     IQtCompressCallServices &services) {
    MY_TRY_BEGIN

    if (!arcFileName.IsEmpty()) {
        services.CompressFiles(arcPathPrefix, arcFileName, UString("hash"),
                               false, // addExtension,
                               paths,
                               false, // email,
                               false, // showDialog,
                               false  // waitFinish
        );
        return S_OK;
    }

    CCodecs *codecs = &services.Codecs();
    CExternalCodecs _externalCodecs;
    ThrowException_if_Error(services.LoadHashProviders(_externalCodecs));

    NWildcard::CCensor censor;
    FOR_VECTOR(i, paths) { censor.AddPreItem_NoWildcard(paths[i]); }

    censor.AddPathsToCensor(NWildcard::k_RelatPath);
    bool messageWasDisplayed = false;

    CHashOptions options;
    options.Methods.Add(methodName);

    /*
    if (!arcFileName.IsEmpty())
      options.HashFilePath = arcPathPrefix + arcFileName;
    */

    result = services.HashCalcGUI(&_externalCodecs, censor, options, messageWasDisplayed);
    if (result != S_OK) {
        if (result != E_ABORT && messageWasDisplayed)
            return E_FAIL;
        throw CSystemException(result);
    }

    MY_TRY_FINISH
    return result;
}
