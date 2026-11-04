// UpdateCallbackGUI2.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "archive/callbacks/qtcallbackpassword.h"
#include "archive/password/archivepassword.h"
#include "localization/LangUtils.h"

#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "7zip/UI/GUI/resource3.h"

#include "gui/UpdateCallbackGUI2.h"
#include <QCoreApplication>
#include <QLocale>

using namespace NWindows;

static const UINT k_UpdNotifyLangs[] = {
    IDS_PROGRESS_ADD,    IDS_PROGRESS_UPDATE,   IDS_PROGRESS_ANALYZE, IDS_PROGRESS_REPLICATE,
    IDS_PROGRESS_REPACK, IDS_PROGRESS_SKIPPING, IDS_PROGRESS_DELETE,  IDS_PROGRESS_HEADER};

void CUpdateCallbackGUI2::Init() {
    NumFiles = 0;

    LangString(IDS_PROGRESS_REMOVE, _lang_Removing);
    LangString(IDS_MOVING, _lang_Moving);
    _lang_Ops.Clear();
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_UpdNotifyLangs); i++)
        _lang_Ops.Add(LangString(k_UpdNotifyLangs[i]));
}

HRESULT CUpdateCallbackGUI2::SetOperation_Base(UInt32 notifyOp, const wchar_t *name, bool isDir) {
    const UString *s = NULL;
    if (notifyOp < _lang_Ops.Size())
        s = &(_lang_Ops[(unsigned)notifyOp]);
    else
        s = &_emptyString;

    return ProgressDialog->Sync.Set_Status2(*s, name, isDir);
}

HRESULT CUpdateCallbackGUI2::ShowAskPasswordDialog() {
    NativePassword password;
    if (!AskCallbackPassword(*ProgressDialog, password))
        return E_ABORT;
    Password = password;
    PasswordIsDefined = true;
    return S_OK;
}

HRESULT CUpdateCallbackGUI2::MoveArc_UpdateStatus() {
    UString s;
    s.Add_UInt64(_arcMoving_percents);
    s.Add_Char('%');

    const bool totalDefined = (_arcMoving_total != 0 && _arcMoving_total != (UInt64)(Int64)-1);
    if (totalDefined || _arcMoving_current != 0) {
        s += " : ";
        s += UString(QCoreApplication::translate("MemoryUnits", "%1 MiB")
                         .arg(QLocale().toString(qulonglong(_arcMoving_current >> 20)))
                         .toStdWString()
                         .c_str());
    }
    if (totalDefined) {
        s += " / ";
        s += UString(
            QCoreApplication::translate("MemoryUnits", "%1 MiB")
                .arg(QLocale().toString(qulonglong((_arcMoving_total + ((1 << 20) - 1)) >> 20)))
                .toStdWString()
                .c_str());
    }

    s += " : ";
    s += _lang_Moving;
    s += " : ";
    // s.Add_Char('\"');
    s += _arcMoving_name1;
    // s.Add_Char('\"');
    return ProgressDialog->Sync.Set_Status2(s, _arcMoving_name2,
                                            false); // isDir
}

HRESULT CUpdateCallbackGUI2::MoveArc_Start_Base(const wchar_t *srcTempPath,
                                                const wchar_t *destFinalPath, UInt64 totalSize,
                                                Int32 updateMode) {
    _arcMoving_percents = 0;
    _arcMoving_total = totalSize;
    _arcMoving_current = 0;
    _arcMoving_updateMode = updateMode;
    _arcMoving_name1 = srcTempPath;
    _arcMoving_name2 = destFinalPath;
    return MoveArc_UpdateStatus();
}

HRESULT CUpdateCallbackGUI2::MoveArc_Progress_Base(UInt64 totalSize, UInt64 currentSize) {
    _arcMoving_total = totalSize;
    _arcMoving_current = currentSize;
    UInt64 percents = 0;
    if (totalSize != 0) {
        if (totalSize < ((UInt64)1 << 57))
            percents = currentSize * 100 / totalSize;
        else
            percents = currentSize / (totalSize / 100);
    }
    if (percents == _arcMoving_percents)
        return ProgressDialog->Sync.CheckStop();
    // Sleep(300); // for debug
    _arcMoving_percents = percents;
    return MoveArc_UpdateStatus();
}

HRESULT CUpdateCallbackGUI2::MoveArc_Finish_Base() {
    return ProgressDialog->Sync.Set_Status2(L"", L"", false);
}
