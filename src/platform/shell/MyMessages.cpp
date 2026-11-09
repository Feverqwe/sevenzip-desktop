// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// MyMessages.cpp

#include "archive/callbacks/qtguimessages.h"
#include "localization/LangUtils.h"
#include <QMessageBox>

bool g_DisableUserQuestions = false;

void ShowErrorMessageQt(QWidget *parent, const wchar_t *message) {
    if (!g_DisableUserQuestions) {
        QMessageBox box(QMessageBox::Critical, QStringLiteral("7-Zip"),
                        QString::fromWCharArray(message), QMessageBox::Ok, parent);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }
}

void ShowErrorMessageHwndResQt(QWidget *parent, UInt32 resID) {
    UString s = LangString(resID);
    if (s.IsEmpty())
        s.Add_UInt32(resID);
    ShowErrorMessageQt(parent, s);
}

#ifdef _WIN32
#include "Common/MyWindows.h"

#include "7zip/UI/Explorer/MyMessages.h"

#include "Windows/ErrorMsg.h"
#include "localization/ResourceString.h"

using namespace NWindows;

void ShowErrorMessage(HWND window, LPCWSTR message) {
    if (!g_DisableUserQuestions)
        ::MessageBoxW(window, message, L"7-Zip", MB_OK | MB_ICONSTOP);
}

void ShowErrorMessageHwndRes(HWND window, UInt32 resID) {
    UString s = LangString(resID);
    if (s.IsEmpty())
        s.Add_UInt32(resID);
    ShowErrorMessage(window, s);
}

void ShowErrorMessageRes(UInt32 resID) { ShowErrorMessageHwndRes(NULL, resID); }

static void ShowErrorMessageDWORD(HWND window, DWORD errorCode) {
    ShowErrorMessage(window, NError::MyFormatMessage(errorCode));
}

void ShowLastErrorMessage(HWND window) { ShowErrorMessageDWORD(window, ::GetLastError()); }
#endif
