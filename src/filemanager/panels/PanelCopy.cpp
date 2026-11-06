/// PanelCopy.cpp

// Adapted from 7-Zip PanelCopy.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "Common/StringConvert.h"
#include "archive/password/archivepassword.h"
#include "ui/progress/qtprogresstransport.h"
#include <memory>

#include "archive/agent/Agent.h"
#include "archive/operations/ZipRegistry.h"

#include "gui/HashGUI.h"

#include "archive/resources/virtualextractfilesystem.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "localization/LangUtils.h"

#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/ExtractRes.h"

class CPanelCopyThread : public CProgressThreadVirt {
    bool ResultsPending;

    HRESULT ProcessVirt() Z7_override;
    virtual void ProcessWasFinished_GuiVirt() Z7_override;

  public:
    const CCopyToOptions *options;
    const UStringVector *CopyFrom_Paths;
    CMyComPtr<IFolderOperations> FolderOperations;
    CRecordVector<UInt32> Indices;
    CMyComPtr2<IFolderOperationsExtractCallback, CExtractCallbackImp> ExtractCallback;

    CHashBundle Hash;
    // UString FirstFilePath;

    // HRESULT Result2;

    void ShowFinalResults(QWidget *hwnd);

    CPanelCopyThread()
        : ResultsPending(false), CopyFrom_Paths(NULL)
    // , Result2(E_FAIL)
    {}
};

void CPanelCopyThread::ShowFinalResults(QWidget *hwnd) {
    if (!ResultsPending)
        return;
    ResultsPending = false;
    ShowHashResults(Hash, hwnd);
}

void CPanelCopyThread::ProcessWasFinished_GuiVirt() { ShowFinalResults(this); }

HRESULT CPanelCopyThread::ProcessVirt() {
    /*
    CMyComPtr<IFolderSetReplaceAltStreamCharsMode> iReplace;
    FolderOperations.QueryInterface(IID_IFolderSetReplaceAltStreamCharsMode, &iReplace);
    if (iReplace)
    {
      RINOK(iReplace->SetReplaceAltStreamCharsMode(ReplaceAltStreamChars ? 1 : 0));
    }
    */

    HRESULT result2;

    if (FolderOperations) {
        {
            CMyComPtr<IFolderSetZoneIdMode> setZoneMode;
            FolderOperations.QueryInterface(IID_IFolderSetZoneIdMode, &setZoneMode);
            if (setZoneMode) {
                RINOK(setZoneMode->SetZoneIdMode(options->ZoneIdMode))
            }
        }
        {
            CMyComPtr<IFolderSetZoneIdFile> setZoneFile;
            FolderOperations.QueryInterface(IID_IFolderSetZoneIdFile, &setZoneFile);
            if (setZoneFile) {
                RINOK(setZoneFile->SetZoneIdFile(options->ZoneBuf, (UInt32)options->ZoneBuf.Size()))
            }
        }
    }

    if (CopyFrom_Paths) {
        result2 = NFsFolder::CopyFileSystemItems(*CopyFrom_Paths, us2fs(options->folder),
                                                 options->moveMode, ExtractCallback);
    } else if (options->testMode || options->extractMode) {
        CMyComPtr<IArchiveFolder> archiveFolder;
        FolderOperations.QueryInterface(IID_IArchiveFolder, &archiveFolder);
        if (!archiveFolder)
            return E_NOTIMPL;
        CMyComPtr<IFolderArchiveExtractCallback> extractCallback2;
        RINOK(ExtractCallback.Interface()->QueryInterface(IID_IFolderArchiveExtractCallback,
                                                          (void **)&extractCallback2))
        NExtract::NPathMode::EEnum pathMode = options->pathMode;
        result2 = archiveFolder->Extract(
            Indices.ConstData(), Indices.Size(), BoolToInt(options->includeAltStreams),
            BoolToInt(options->replaceAltStreamChars), pathMode, options->overwriteMode,
            options->folder, BoolToInt(options->testMode), extractCallback2);
    } else
        result2 = FolderOperations->CopyTo(BoolToInt(options->moveMode), Indices.ConstData(),
                                           Indices.Size(), BoolToInt(options->includeAltStreams),
                                           BoolToInt(options->replaceAltStreamChars),
                                           options->folder, ExtractCallback);

    if (result2 == S_OK && !ExtractCallback->ThereAreMessageErrors) {
        if (!options->hashMethods.IsEmpty())
            ResultsPending = true;
        else if (options->testMode) {
            CProgressMessageBoxPair &pair =
                GetMessagePair(false); // GetMessagePair(ExtractCallback->Hash.NumErrors != 0);
            AddHashBundleRes(pair.Message, Hash);
        }
    }

    return result2;
}

/*
#ifdef Z7_EXTERNAL_CODECS

static void ThrowException_if_Error(HRESULT res)
{
  if (res != S_OK)
    throw CSystemException(res);
}

#endif
*/

void CPanel::Get_ZoneId_Stream_from_ParentFolders(CByteBuffer &buf) {
    // we suppose that ZoneId of top parent has priority over ZoneId from childs.
    FOR_VECTOR(i, _parentFolders) {
        // _parentFolders[0] = is top level archive
        // _parentFolders[1 ... ].isVirtual == true is possible
        //           if extracted size meets size conditions derived from g_RAM_Size.
        const CFolderLink &fl = _parentFolders[i];
        if (fl.IsVirtual) {
            if (fl.ZoneBuf.Size() != 0) {
                buf = fl.ZoneBuf;
                return;
            }
        } else if (!fl.FilePath.IsEmpty()) {
            ReadZoneFile_Of_BaseFile(fl.FilePath, buf);
            if (buf.Size() != 0)
                return;
        }
    }
}

HRESULT CPanel::CopyTo(CCopyToOptions &options, const CRecordVector<UInt32> &indices,
                       UStringVector *messages, bool &usePassword, UString &password,
                       const UStringVector *filePaths, QtArchiveExtractReport *report) {
    if (report)
        report->Status.reset();
    if (options.NeedRegistryZone && !options.testMode) {
        CContextMenuInfo ci;
        ci.Load(_settingsPath);
        if (ci.WriteZone != (UInt32)(Int32)-1)
            options.ZoneIdMode = (NExtract::NZoneIdMode::EEnum)(int)(Int32)ci.WriteZone;
    }

    if (options.ZoneBuf.Size() == 0 && options.ZoneIdMode != NExtract::NZoneIdMode::kNone)
        Get_ZoneId_Stream_from_ParentFolders(options.ZoneBuf);

    if (IsHashFolder()) {
        if (!options.testMode)
            return E_NOTIMPL;
    }

    if (!filePaths)
        if (!_folderOperations) {
            const UString errorMessage = LangString(IDS_OPERATION_IS_NOT_SUPPORTED);
            if (options.showErrorMessages)
                MessageBox_Error(errorMessage);
            else if (messages)
                messages->Add(errorMessage);
            return E_FAIL;
        }

    HRESULT res = S_OK;

    {
#ifdef Z7_EXTERNAL_CODECS
        // This port has no upstream FM global codec singleton. Keep the source
        // local provider alive longer than extracter.Hash; runtime attachment
        // is owned by the P2-01 backend adapter.
        CExternalCodecs g_ExternalCodecs;
#endif
        /* extracter.Hash uses g_ExternalCodecs
           extracter must be declared after g_ExternalCodecs for correct destructor order !!! */

        CPanelCopyThread extracter;
        extracter.DisableUserQuestions |= options.DisableUserQuestions;

        extracter.ExtractCallback.SetFromCls(new CExtractCallbackImp);
        extracter.ExtractCallback->Src_Is_IO_FS_Folder = IsFSFolder() || IsAltStreamsFolder();
        // options.src_Is_IO_FS_Folder;
        extracter.options = &options;
        extracter.ExtractCallback->ProgressDialog = &extracter;
        extracter.ExtractCallback->SettingsPath = _settingsPath;
        std::unique_ptr<IQtProgressTransport> transport;
        if (auto *services =
                dynamic_cast<IQtProgressServices *>((IFolderOperations *)_folderOperations))
            transport = services->CreateProgressTransport(extracter);
        extracter.CompressingMode = false;

        extracter.ExtractCallback->StreamMode = options.streamMode;

        if (indices.Size() == 1) {
            extracter.Hash.FirstFileName = GetItemRelPath(indices[0]);
            extracter.Hash.MainName = extracter.Hash.FirstFileName;
        }

        if (options.VirtFileSystemSpec)
            extracter.ExtractCallback->VirtFileSystem.SetFromCls(options.VirtFileSystemSpec);
        extracter.ExtractCallback->ProcessAltStreams = options.includeAltStreams;

        if (!options.hashMethods.IsEmpty()) {
            /* this code is used when we call CRC calculation for files in side archive
               But new code uses global codecs so we don't need to call LoadGlobalCodecs again */

            /*
            #ifdef Z7_EXTERNAL_CODECS
            ThrowException_if_Error(LoadGlobalCodecs());
            #endif
            */

            RINOK(extracter.Hash.SetMethods(options.ExternalCodecs, options.hashMethods))
            extracter.ExtractCallback->SetHashMethods(&extracter.Hash);
        } else if (options.testMode) {
            extracter.ExtractCallback->SetHashCalc(&extracter.Hash);
        }

        // extracter.Hash.Init();

        UString title;
        {
            UInt32 titleID = IDS_COPYING;
            if (options.moveMode)
                titleID = IDS_MOVING;
            else if (options.extractMode)
                titleID = IDS_PROGRESS_EXTRACTING;
            else if (!options.hashMethods.IsEmpty() && options.streamMode) {
                titleID = IDS_CHECKSUM_CALCULATING;
                if (options.hashMethods.Size() == 1) {
                    const UString &s = options.hashMethods[0];
                    if (!s.IsEqualTo("*"))
                        title = s;
                }
            } else if (options.testMode)
                titleID = IDS_PROGRESS_TESTING;

            if (title.IsEmpty())
                title = LangString(titleID);
        }

        const UString progressWindowTitle("7-Zip"); // LangString(IDS_APP_TITLE);

        extracter.MainWindow = GetParent();
        extracter.MainTitle = progressWindowTitle;
        extracter.MainAddTitle = title + L' ';

        extracter.ExtractCallback->OverwriteMode = options.overwriteMode;
        extracter.ExtractCallback->Init();
        if (report)
            extracter.ExtractCallback->ExtractReport.Purpose = report->Purpose;

        extracter.CopyFrom_Paths = filePaths;
        if (!filePaths) {
            extracter.Indices = indices;
            extracter.FolderOperations = _folderOperations;
        }

        extracter.ExtractCallback->PasswordIsDefined = usePassword;
        extracter.ExtractCallback->Password = password;

        RINOK(extracter.Create(title, GetParent()))

        if (messages)
            *messages = extracter.Sync.Messages;

        // res = extracter.Result2;
        res = extracter.Result;

        if (report) {
            report->Status = extracter.ExtractCallback->ExtractReport.Status;
            if (report->Status && !extracter.ExtractCallback->IsOK())
                report->Status->Complete = false;
        }

        if (res == S_OK && extracter.ExtractCallback->IsOK()) {
            usePassword = extracter.ExtractCallback->PasswordIsDefined;
            WipeNativePassword(password);
            password = extracter.ExtractCallback->Password;
        }

        extracter.ShowFinalResults(GetParent());
    }

    RefreshTitleAlways();
    return res;
}

HRESULT CPanel::CopyFromNoAsk(bool moveMode, const UStringVector &filePaths,
                              std::optional<CSelectedState> *refreshSelection) {
    if (refreshSelection)
        refreshSelection->reset();
    CDisableTimerProcessing disableTimerProcessing(*this);
    CSelectedState srcSelState;
    SaveSelectedState(srcSelState);
    CDisableNotify disableNotify(*this);
    const HRESULT result = CopyFrom(moveMode, L"", filePaths, true, NULL);
    if (result != S_OK) {
        disableNotify.Restore();
        SetFocusToList();
        if (result != E_ABORT)
            MessageBox_Error_HRESULT(result);
        return result;
    }
    // Qt publishes this reached source phase after the outer synchronous
    // operation guards unwind, before retaining asynchronous scan guards.
    if (refreshSelection)
        *refreshSelection = srcSelState;
    else
        RefreshListCtrl(srcSelState);
    disableNotify.Restore();
    SetFocusToList();
    return S_OK;
}

class CThreadUpdate final : public CProgressThreadVirt {
    HRESULT ProcessVirt() override {
        if (ActionSet) {
            CMyComPtr<IArchiveFolderInternal> archiveFolder;
            RINOK(FolderOperations.QueryInterface(IID_IArchiveFolderInternal, &archiveFolder))
            if (PathMode)
                return archiveFolder->UpdateFromPaths(FileNamePointers.ConstData(),
                                                      FileNamePointers.Size(), *ActionSet,
                                                      *PathMode, UpdateCallback);
            return archiveFolder->UpdateFrom(FolderPrefix, FileNamePointers.ConstData(),
                                             FileNamePointers.Size(), *ActionSet, UpdateCallback);
        }
        return FolderOperations->CopyFrom(MoveMode, FolderPrefix, FileNamePointers.ConstData(),
                                          FileNamePointers.Size(), UpdateCallback);
    }

  public:
    CMyComPtr<IFolderOperations> FolderOperations;
    UString FolderPrefix;
    UStringVector FileNames;
    CRecordVector<const wchar_t *> FileNamePointers;
    CMyComPtr2<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> UpdateCallback;
    bool MoveMode = false;
    const NUpdateArchive::CActionSet *ActionSet = nullptr;
    std::optional<NWildcard::ECensorPathMode> PathMode;
};

HRESULT CPanel::CopyFrom(bool moveMode, const UString &folderPrefix, const UStringVector &filePaths,
                         bool showErrorMessages, UStringVector *messages,
                         const NUpdateArchive::CActionSet *actionSet,
                         std::optional<NWildcard::ECensorPathMode> pathMode) {
    if (IsHashFolder() && moveMode)
        return E_NOTIMPL;

    HRESULT res;
    if (!_folderOperations)
        res = E_NOINTERFACE;
    else {
        CThreadUpdate updater;
        updater.MoveMode = moveMode;
        updater.ActionSet = actionSet;
        updater.PathMode = pathMode;
        updater.UpdateCallback.SetFromCls(new CUpdateCallback100Imp);
        updater.UpdateCallback->Init();
        updater.UpdateCallback->ProgressDialog = &updater;
        updater.MainWindow = GetParent();
        updater.MainTitle = L"7-Zip";
        const UString title = LangString(IDS_COPYING);
        updater.MainAddTitle = title + L' ';
        if (!_parentFolders.IsEmpty()) {
            const CFolderLink &fl = _parentFolders.Back();
            updater.UpdateCallback->PasswordIsDefined = fl.UsePassword;
            updater.UpdateCallback->Password = fl.Password.native();
        }
        updater.FolderOperations = _folderOperations;
        updater.FolderPrefix = folderPrefix;
        updater.FileNames.ClearAndReserve(filePaths.Size());
        FOR_VECTOR(i, filePaths) updater.FileNames.AddInReserved(filePaths[i]);
        updater.FileNamePointers.ClearAndReserve(updater.FileNames.Size());
        FOR_VECTOR(i, updater.FileNames)
        updater.FileNamePointers.AddInReserved(updater.FileNames[i]);
        std::unique_ptr<IQtProgressTransport> transport;
        if (auto *services =
                dynamic_cast<IQtProgressServices *>((IFolderOperations *)_folderOperations))
            transport = services->CreateProgressTransport(updater);
        RINOK(updater.Create(title, GetParent()))
        if (messages)
            *messages = updater.Sync.Messages;
        res = updater.Result;
    }
    if (res == E_NOINTERFACE) {
        const UString errorMessage = LangString(IDS_OPERATION_IS_NOT_SUPPORTED);
        if (showErrorMessages)
            MessageBox_Error(errorMessage);
        else if (messages)
            messages->Add(errorMessage);
        return E_ABORT;
    }
    RefreshTitleAlways();
    return res;
}
