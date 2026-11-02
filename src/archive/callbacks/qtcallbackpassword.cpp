#include "archive/callbacks/qtcallbackpassword.h"
#include "archive/callbacks/qtcallbackdialogs.h"
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

bool AskCallbackPassword(CProgressDialog &parent, UString &password) {
    bool showPassword = false;
    return AskCallbackPassword(parent, password, showPassword);
}

bool AskCallbackPassword(CProgressDialog &parent, UString &password, bool &showPassword) {
    return RunCallbackQuestion(parent, [&]() -> HRESULT {
               CPasswordDialog dialog(&parent);
               dialog.ShowPassword = showPassword;
               dialog.OnInit();
               if (dialog.exec() != QDialog::Accepted)
                   return E_ABORT;
               password = dialog.Password;
               showPassword = dialog.ShowPassword;
               return S_OK;
           }) == S_OK;
}
