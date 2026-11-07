#include "archive/callbacks/qtcallbackpassword.h"
#include "archive/callbacks/qtcallbackdialogs.h"
#include "archive/password/archivepassword.h"
#include "crypto/fscrypt/fscryptkeydialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "ui/progress/ProgressDialog2.h"

bool AskCallbackFilesystemKeys(CProgressDialog &parent, UString &keys) {
    return RunCallbackQuestion(parent, [&]() -> HRESULT {
               FscryptKeyDialog dialog(&parent);
               if (dialog.exec() != QDialog::Accepted)
                   return E_ABORT;
               keys = dialog.keyDescription().toStdWString().c_str();
               return S_OK;
           }) == S_OK;
}

bool AskCallbackPassword(CProgressDialog &parent, UString &password, const UString &archive) {
    bool showPassword = false;
    return AskCallbackPassword(parent, password, showPassword, archive);
}

bool AskCallbackPassword(CProgressDialog &parent, UString &password, bool &showPassword,
                         const UString &archive, bool opening) {
    return RunCallbackQuestion(parent, [&]() -> HRESULT {
               CPasswordDialog dialog(&parent);
               dialog.ShowPassword = showPassword;
               dialog.OnInit(QString::fromWCharArray(archive.Ptr(), archive.Len()), opening);
               if (dialog.exec() != QDialog::Accepted)
                   return E_ABORT;
               WipeNativePassword(password);
               password = dialog.Password;
               showPassword = dialog.ShowPassword;
               return S_OK;
           }) == S_OK;
}
