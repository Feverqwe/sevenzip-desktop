// Adapted from 7-Zip CPP/7zip/UI/FileManager/OpenCallback.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_OPEN_CALLBACK_H
#define ZIP7_INC_OPEN_CALLBACK_H

#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "crypto/fscrypt/fscryptkeyrequest.h"

#ifdef Z7_SFX
#include "7zip/UI/FileManager/ProgressDialog.h"
#else
#include "ui/progress/ProgressDialog2.h"
#endif

/* we can use IArchiveOpenCallback or IOpenCallbackUI here */

class COpenArchiveCallback Z7_final :
    /*
    public IArchiveOpenCallback,
    public IProgress,
    public ICryptoGetTextPassword,
    public CMyUnknownImp
    */
    public IOpenCallbackUI,
    public IFscryptKeyUI,
    public IArchivePasswordRetryUI {
    // NWindows::NSynchronization::CCriticalSection _criticalSection;
  public:
    bool PasswordIsDefined;
    bool PasswordWasAsked;
    UString Password;
    UString_Wipe FilesystemKeys;
    bool FilesystemKeysDefined = false;
    HRESULT Open_FscryptKeys(BSTR *keys) override;
    HRESULT ResetArchivePassword() override {
        Password.Empty();
        PasswordIsDefined = false;
        return Open_CheckBreak();
    }
    QString DeviceOpenError;
    QPointer<QWidget> ParentWindow;
    QString SettingsPath;
    CProgressDialog ProgressDialog;

    /*
    Z7_COM_UNKNOWN_IMP_3(
      IArchiveOpenVolumeCallback,
      IProgress
      ICryptoGetTextPassword
      )

    Z7_IFACE_COM7_IMP(IProgress)
    Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
    // ICryptoGetTextPassword
    Z7_COM7F_IMP(CryptoGetTextPassword(BSTR *password))
    */

    Z7_IFACE_IMP(IOpenCallbackUI)

    COpenArchiveCallback() : ParentWindow(NULL) {
        // _subArchiveMode = false;
        PasswordIsDefined = false;
        PasswordWasAsked = false;
    }
    /*
    void Init()
    {
      PasswordIsDefined = false;
      _subArchiveMode = false;
    }
    */

    qintptr StartProgressDialog(const UString &title, NWindows::CThread &thread) {
        return ProgressDialog.Create(title, thread, ParentWindow);
    }
};

#endif
