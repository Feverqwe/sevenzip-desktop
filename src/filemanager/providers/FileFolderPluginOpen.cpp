// Adapted from 7-Zip CPP/7zip/UI/FileManager/FileFolderPluginOpen.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "Common/StdAfx.h"
#include "archive/diagnostics/archiveerrormessages.h"

#include "7zip/UI/FileManager/resource.h"

#include "7zip/PropID.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/Thread.h"

#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/opening/OpenArchive.h"
#include "filemanager/ExtractCallback.h"

#include "archive/resources/qtarchivestreamopen.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/OpenCallback.h"
#include "filemanager/PropertyName.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "localization/LangUtils.h"
#include "ui/diagnostics/qtdiagnostictext.h"

using namespace NWindows;

struct CThreadArchiveOpen {
    // This record's COM references release before its GUI callback.
    COpenArchiveCallback OpenCallback;
    UString Path;
    UString ArcFormat;
    CMyComPtr<IInStream> InStream;
    CMyComPtr<IFolderManager> FolderManager;
    CMyComPtr<IProgress> OpenCallbackProgress;
    bool OwnedStreamFile = false;
    bool MaterializeStreamFile = false;
    bool MaterializedStreamFile = false;

    CMyComPtr<IFolderFolder> Folder;
    HRESULT Result;

    void Process() {
        try {
            CProgressCloser closer(OpenCallback.ProgressDialog);
            auto *services =
                dynamic_cast<IQtArchiveStreamOpenServices *>((IFolderManager *)FolderManager);
            if (InStream && OwnedStreamFile && services) {
                Result = services->OpenOwnedStreamFile(InStream, Path, ArcFormat, &Folder,
                                                       OpenCallbackProgress, MaterializeStreamFile,
                                                       &MaterializedStreamFile);
            } else
                Result = FolderManager->OpenFolderFile(InStream, Path, ArcFormat, &Folder,
                                                       OpenCallbackProgress);
        } catch (...) {
            Result = E_FAIL;
        }
    }

    static THREAD_FUNC_DECL MyThreadFunction(void *param) {
        ((CThreadArchiveOpen *)param)->Process();
        return 0;
    }
};

struct CArcLevelInfo {
    UString Error;
    UString Path;
    UString Type;
    UString ErrorType;
    UString ErrorFlags;
};

struct CArcLevelsInfo {
    CObjectVector<CArcLevelInfo> Levels; // LastLevel Is NON-OPEN
};

static void GetFolderLevels(CMyComPtr<IFolderFolder> &folder, CArcLevelsInfo &levels) {
    levels.Levels.Clear();

    CMyComPtr<IGetFolderArcProps> getFolderArcProps;
    folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);

    if (!getFolderArcProps)
        return;
    CMyComPtr<IFolderArcProps> arcProps;
    getFolderArcProps->GetFolderArcProps(&arcProps);
    if (!arcProps)
        return;

    UInt32 numLevels;
    if (arcProps->GetArcNumLevels(&numLevels) != S_OK)
        numLevels = 0;

    for (UInt32 level = 0; level <= numLevels; level++) {
        const PROPID propIDs[] = {kpidError, kpidPath, kpidType, kpidErrorType};

        CArcLevelInfo lev;

        for (Int32 i = 0; i < 4; i++) {
            CMyComBSTR name;
            NCOM::CPropVariant prop;
            if (arcProps->GetArcProp(level, propIDs[i], &prop) != S_OK)
                continue;
            if (prop.vt != VT_EMPTY) {
                UString *s = NULL;
                switch (propIDs[i]) {
                case kpidError:
                    s = &lev.Error;
                    break;
                case kpidPath:
                    s = &lev.Path;
                    break;
                case kpidType:
                    s = &lev.Type;
                    break;
                case kpidErrorType:
                    s = &lev.ErrorType;
                    break;
                }
                *s = (prop.vt == VT_BSTR) ? prop.bstrVal : L"?";
            }
        }

        {
            NCOM::CPropVariant prop;
            if (arcProps->GetArcProp(level, kpidErrorFlags, &prop) == S_OK) {
                UInt32 flags = GetOpenArcErrorFlags(prop);
                if (flags != 0)
                    lev.ErrorFlags = GetOpenArcErrorMessage(flags);
            }
        }

        levels.Levels.Add(lev);
    }
}

static UString GetBracedType(const wchar_t *type) {
    UString s('[');
    s += type;
    s.Add_Char(']');
    return s;
}

static void GetFolderError(CMyComPtr<IFolderFolder> &folder, UString &open_Errors,
                           UString &nonOpen_Errors) {
    CArcLevelsInfo levs;
    GetFolderLevels(folder, levs);
    open_Errors.Empty();
    nonOpen_Errors.Empty();

    FOR_VECTOR(i, levs.Levels) {
        bool isNonOpenLevel = (i == 0);
        const CArcLevelInfo &lev = levs.Levels[levs.Levels.Size() - 1 - i];

        UString m;

        if (!lev.ErrorType.IsEmpty()) {
            m = MyFormatNew(IDS_CANT_OPEN_AS_TYPE, GetBracedType(lev.ErrorType));
            if (!isNonOpenLevel) {
                m.Add_LF();
                m += MyFormatNew(IDS_IS_OPEN_AS_TYPE, GetBracedType(lev.Type));
            }
        }

        if (!lev.Error.IsEmpty()) {
            if (!m.IsEmpty())
                m.Add_LF();
            m += GetBracedType(lev.Type);
            m += " : ";
            m += GetNameOfProperty(kpidError, L"Error");
            m += " : ";
            m += QtArchiveDiagnosticText(lev.Error);
        }

        if (!lev.ErrorFlags.IsEmpty()) {
            if (!m.IsEmpty())
                m.Add_LF();
            m += GetNameOfProperty(kpidErrorFlags, L"Errors");
            m += ": ";
            m += lev.ErrorFlags;
        }

        if (!m.IsEmpty()) {
            if (isNonOpenLevel) {
                UString &s = nonOpen_Errors;
                s += lev.Path;
                s.Add_LF();
                s += m;
            } else {
                UString &s = open_Errors;
                if (!s.IsEmpty())
                    s += "--------------------\n";
                s += lev.Path;
                s.Add_LF();
                s += m;
            }
        }
    }
}

#ifdef _MSC_VER
#pragma warning(error : 4702) // unreachable code
#endif

HRESULT CFfpOpen::OpenFileFolderPlugin(IInStream *inStream, const FString &path,
                                       const UString &arcFormat, const UString &displayPath,
                                       QWidget *parentWindow) {
    if (!FolderManager)
        return E_INVALIDARG;
    const int slashPos = path.ReverseFind_PathSepar();
    FString dirPrefix;
    FString fileName;
    if (slashPos >= 0) {
        dirPrefix.SetFrom(path, (unsigned)(slashPos + 1));
        fileName = path.Ptr((unsigned)(slashPos + 1));
    } else
        fileName = path;

    ErrorMessage.Empty();

    NWindows::NDLL::CLibrary library;

    CThreadArchiveOpen t;

    t.FolderManager = FolderManager;
    std::unique_ptr<IQtProgressTransport> transport;
    if (auto *services = dynamic_cast<IQtProgressServices *>((IFolderManager *)FolderManager))
        transport = services->CreateProgressTransport(t.OpenCallback.ProgressDialog);
    t.OpenCallback.PasswordIsDefined = Encrypted;
    t.OpenCallback.Password = Password;
    t.OpenCallback.ParentWindow = parentWindow;
    t.OpenCallback.SettingsPath = SettingsPath;
    t.OpenCallback.ProgressDialog.DisableUserQuestions |= DisableUserQuestions;

    /* COpenCallbackImp object will exist after Open stage for multivolume archives */
    COpenCallbackImp *openCallbackSpec = new COpenCallbackImp;
    t.OpenCallbackProgress = openCallbackSpec;
    // we set pointer without reference counter:
    openCallbackSpec->Callback = &t.OpenCallback;

    if (inStream)
        openCallbackSpec->SetSubArchiveName(fs2us(fileName));
    else {
        RINOK(openCallbackSpec->Init2(dirPrefix, fileName))
    }

    t.InStream = inStream;
    t.OwnedStreamFile = OwnedStreamFile;
    t.MaterializeStreamFile = MaterializeStreamFile;
    t.Path = fs2us(path);
    t.OpenCallback.PasswordArchivePath = displayPath.IsEmpty() ? t.Path : displayPath;
    t.ArcFormat = arcFormat;

    const UString progressTitle = LangString(IDS_OPENNING);
    {
        CProgressDialog &pd = t.OpenCallback.ProgressDialog;
        pd.MainWindow = parentWindow;
        pd.MainTitle = "7-Zip"; // LangString(IDS_APP_TITLE);
        pd.MainAddTitle = progressTitle + L' ';
        pd.WaitMode = true;
    }

    {
        NWindows::CThread thread;
        const WRes wres = thread.Create(CThreadArchiveOpen::MyThreadFunction, &t);
        if (wres != 0)
            return HRESULT_FROM_WIN32(wres);
        t.OpenCallback.StartProgressDialog(progressTitle, thread);
    }

    /*
      if archive is multivolume:
      COpenCallbackImp object will exist after Open stage.
      COpenCallbackImp object will be deleted when last reference
      from each volume object (CInFileStreamVol) will be closed (when archive will be closed).
    */
    t.OpenCallbackProgress.Release();
    MaterializedStreamFile = t.MaterializedStreamFile;
    if (!t.OpenCallback.DeviceOpenError.isEmpty())
        ErrorMessage = t.OpenCallback.DeviceOpenError.toStdWString().c_str();

    if (t.Result != S_FALSE && t.Result != S_OK)
        return t.Result;

    if (t.Folder) {
        UString open_Errors, nonOpen_Errors;
        GetFolderError(t.Folder, open_Errors, nonOpen_Errors);
        if (!nonOpen_Errors.IsEmpty()) {
            ErrorMessage = nonOpen_Errors;
            // if (t.Result != S_OK) return t.Result;
            /* if there are good open leves, and non0open level,
               we could force error as critical error and return error here
               but it's better to allow to open such rachives */
            // return S_FALSE;
        }
    }

    // if (openCallbackSpec->PasswordWasAsked)
    {
        Encrypted = t.OpenCallback.PasswordIsDefined;
        Password = t.OpenCallback.Password;
    }

    if (t.Result == S_OK) {
        Library.Attach(library.Detach());
        // Folder.Attach(t.Folder.Detach());
        Folder = t.Folder;
    }

    return t.Result;
}
