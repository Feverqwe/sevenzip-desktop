// ExtractCallback.h
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_EXTRACT_CALLBACK_H
#define ZIP7_INC_EXTRACT_CALLBACK_H

#include "Common/MyCom.h"
#include "Common/StringConvert.h"

#ifndef Z7_SFX
#include "archive/agent/IFolderArchive.h"
#endif

#include "archive/callbacks/ArchiveExtractCallback.h"
#include "archive/callbacks/ArchiveOpenCallback.h"

#ifndef Z7_NO_CRYPTO
#include "7zip/IPassword.h"
#endif

#ifndef Z7_SFX
#include "7zip/UI/FileManager/IFolder.h"
#endif

#include "ui/progress/ProgressDialog2.h"
#include <QString>

#ifndef Z7_SFX
class CVirtFileSystem;
#endif

class CExtractCallbackImp Z7_final
    : public IFolderArchiveExtractCallback,
      /* IExtractCallbackUI:
           before v23.00 : it         included IFolderArchiveExtractCallback
           since  v23.00 : it doesn't include  IFolderArchiveExtractCallback
      */
      public IExtractCallbackUI, // NON-COM interface since 23.00
      public IOpenCallbackUI,    // NON-COM interface
      public IFolderArchiveExtractCallback2,
#ifndef Z7_SFX
      public IFolderOperationsExtractCallback,
      public IFolderExtractToStreamCallback,
      public ICompressProgressInfo,
      public IArchiveRequestMemoryUseCallback,
#endif
#ifndef Z7_NO_CRYPTO
      public ICryptoGetTextPassword,
#endif
      public QtArchiveExtractStatusReceiver,
      public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderArchiveExtractCallback)
    Z7_COM_QI_ENTRY(IFolderArchiveExtractCallback2)
#ifndef Z7_SFX
    Z7_COM_QI_ENTRY(IFolderOperationsExtractCallback)
    Z7_COM_QI_ENTRY(IFolderExtractToStreamCallback)
    Z7_COM_QI_ENTRY(ICompressProgressInfo)
    Z7_COM_QI_ENTRY(IArchiveRequestMemoryUseCallback)
#endif
#ifndef Z7_NO_CRYPTO
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
#endif
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE

    Z7_IFACE_IMP(IExtractCallbackUI)
    Z7_IFACE_IMP(IOpenCallbackUI)
    Z7_IFACE_COM7_IMP(IProgress)
    Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback)
    Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback2)
#ifndef Z7_SFX
    Z7_IFACE_COM7_IMP(IFolderOperationsExtractCallback)
    Z7_IFACE_COM7_IMP(IFolderExtractToStreamCallback)
    Z7_IFACE_COM7_IMP(ICompressProgressInfo)
    Z7_IFACE_COM7_IMP(IArchiveRequestMemoryUseCallback)
#endif
#ifndef Z7_NO_CRYPTO
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
#endif

    bool _needWriteArchivePath = false;
    bool _isFolder;
    bool _totalFiles_Defined;
    bool _totalBytes_Defined;

  public:
    QString SettingsPath;
    bool DisableUserQuestions = false;
    bool MultiArcMode;
    bool ProcessAltStreams;
    bool StreamMode; // set to true, if you want the callee to call GetStream7()
    bool ThereAreMessageErrors;
    bool Src_Is_IO_FS_Folder;

#ifndef Z7_NO_CRYPTO
    bool PasswordIsDefined;
    bool PasswordWasAsked;
#endif

  private:
#ifndef Z7_SFX
    bool _needUpdateStat;
    bool _newVirtFileWasAdded;
    bool _isAltStream;
    // bool _extractMode;
    // bool _testMode;
    bool _hashStream_WasUsed;
    bool _curSize_Defined;
    bool NeedAddFile;

    bool _remember;
    bool _skipArc;
#endif

  public:
    bool YesToAll;
    bool TestMode;

    UInt32 NumArchiveErrors;
    NExtract::NOverwriteMode::EEnum OverwriteMode;

  private:
    UString _currentArchivePath;
    UString _currentFilePath;
    UString _filePath; // virtual path than will be sent via IFolderExtractToStreamCallback

#ifndef Z7_SFX
    UInt64 _curSize;
    CMyComPtr2<ISequentialOutStream, COutStreamWithHash> _hashStream;
    IHashCalc *_hashCalc; // it's for stat in Test operation
#endif

  public:
    CProgressDialog *ProgressDialog;

#ifndef Z7_SFX
    CVirtFileSystem *VirtFileSystemSpec;
    CMyComPtr<ISequentialOutStream> VirtFileSystem;
    UInt64 NumFolders;
    UInt64 NumFiles;
#endif

#ifndef Z7_NO_CRYPTO
    UString Password;
#endif

    UString _lang_Extracting;
    UString _lang_Testing;
    UString _lang_Skipping;
    UString _lang_Reading;
    UString _lang_Empty;

    CExtractCallbackImp()
        : _totalFiles_Defined(false), _totalBytes_Defined(false), MultiArcMode(false),
          ProcessAltStreams(true), StreamMode(false), ThereAreMessageErrors(false),
          Src_Is_IO_FS_Folder(false)
#ifndef Z7_NO_CRYPTO
          ,
          PasswordIsDefined(false), PasswordWasAsked(false)
#endif
#ifndef Z7_SFX
          ,
          _remember(false), _skipArc(false)
#endif
          ,
          YesToAll(false), TestMode(false), OverwriteMode(NExtract::NOverwriteMode::kAsk)
#ifndef Z7_SFX
          ,
          _hashCalc(NULL)
#endif
    {
    }

    ~CExtractCallbackImp();
    void Init();

    HRESULT SetCurrentFilePath2(const wchar_t *filePath);
    void AddError_Message(LPCWSTR message);
    void AddError_Message_ShowArcPath(LPCWSTR message);
    HRESULT MessageError(const char *message, const FString &path);
    void Add_ArchiveName_Error();

#ifndef Z7_SFX
    void SetHashCalc(IHashCalc *hashCalc) { _hashCalc = hashCalc; }

    void SetHashMethods(IHashCalc *hash) {
        if (!hash)
            return;
        _hashStream.Create_if_Empty();
        _hashStream->_hash = hash;
    }
#endif

    bool IsOK() const { return NumArchiveErrors == 0 && !ThereAreMessageErrors; }
};

#endif
