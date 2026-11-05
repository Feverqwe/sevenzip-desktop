// LinkDialog.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "7zip/UI/Common/StdAfx.h"

#include "Windows/ErrorMsg.h"
#include "Windows/FileFind.h"
#include "Windows/FileIO.h"
#include "Windows/FileName.h"
#include "fileops/native/FileDir.h"

#include "localization/LangUtils.h"

#include "7zip/UI/FileManager/CopyDialogRes.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/link/LinkDialog.h"
#include "ui/diagnostics/qtdiagnostictext.h"

#include "7zip/UI/FileManager/resource.h"
#include "fileops/links/qtlinkservices.h"
#include <QFileInfo>
#include <QMessageBox>

extern bool g_SymLink_Supported;

using namespace NWindows;
using namespace NFile;

#ifdef Z7_LANG
static const UInt32 kLangIDs[] = {
    IDB_LINK_LINK,         IDT_LINK_PATH_FROM,     IDT_LINK_PATH_TO,
    IDG_LINK_TYPE,         IDR_LINK_TYPE_HARD,     IDR_LINK_TYPE_SYM_FILE,
    IDR_LINK_TYPE_SYM_DIR, IDR_LINK_TYPE_JUNCTION, IDR_LINK_TYPE_WSL};
#endif

#ifdef _WIN32
static bool GetSymLink(CFSTR path, CReparseAttr &attr, UString &errorMessage) {
    CByteBuffer buf;
    if (!NIO::GetReparseData(path, buf, NULL))
        return false;
    if (!attr.Parse(buf, buf.Size())) {
        SetLastError(attr.ErrorCode);
        return false;
    }
    CByteBuffer data2;
    FillLinkData(data2, attr.GetPath(), !attr.IsMountPoint(), attr.IsSymLink_WSL());
    if (data2.Size() == 0) {
        errorMessage = QtDiagnosticText(
            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot reproduce reparse point"));
        return false;
    }
    if (data2 != buf) {
        errorMessage = QtDiagnosticText(
            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "mismatch for reproduced reparse point"));
        return false;
    }
    return true;
}
#endif

static const unsigned k_LinkType_Buttons[] = {IDR_LINK_TYPE_HARD, IDR_LINK_TYPE_SYM_FILE,
                                              IDR_LINK_TYPE_SYM_DIR, IDR_LINK_TYPE_JUNCTION,
                                              IDR_LINK_TYPE_WSL};

void CLinkDialog::Set_LinkType_Radio(unsigned idb) {
    CheckRadioButton(k_LinkType_Buttons[0],
                     k_LinkType_Buttons[Z7_ARRAY_SIZE(k_LinkType_Buttons) - 1], idb);
}

void CLinkDialog::OnInit() {
#ifdef Z7_LANG
    LangSetWindowText(*this, IDD_LINK);
    LangSetDlgItems(*this, kLangIDs, Z7_ARRAY_SIZE(kLangIDs));
#endif

    _pathFromCombo.Attach(GetItem(IDC_LINK_PATH_FROM));
    _pathToCombo.Attach(GetItem(IDC_LINK_PATH_TO));

    if (!FilePath.IsEmpty()) {
        NFind::CFileInfo fi;
        unsigned linkType = 0;
        if (!fi.Find(us2fs(FilePath)))
            linkType = IDR_LINK_TYPE_SYM_FILE;
        else {
            if (fi.IsOsSymLink()) {
#ifdef _WIN32
                CReparseAttr attr;
                UString error;
                const bool res = GetSymLink(us2fs(FilePath), attr, error);
                if (!res && error.IsEmpty()) {
                    const DWORD lastError = GetLastError();
                    if (lastError)
                        error = NError::MyFormatMessage(lastError);
                }

                UString s = attr.GetPath();
                if (!attr.IsSymLink_WSL())
                    if (!attr.IsOkNamePair()) {
                        s += " : ";
                        s += attr.PrintName;
                    }

                if (!res) {
                    s.Insert(0,
                             QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "ERROR: ")));
                    if (!error.IsEmpty()) {
                        s += " : ";
                        s += error;
                    }
                }

                SetItemText(IDT_LINK_PATH_TO_CUR, s);

                const UString destPath = attr.GetPath();
                _pathFromCombo.SetText(FilePath);
                _pathToCombo.SetText(destPath);

                // if (res)
                {
                    if (attr.IsMountPoint())
                        linkType = IDR_LINK_TYPE_JUNCTION;
                    else if (attr.IsSymLink_WSL())
                        linkType = IDR_LINK_TYPE_WSL;
                    else if (attr.IsSymLink_Win()) {
                        linkType = fi.IsDir() ? IDR_LINK_TYPE_SYM_DIR : IDR_LINK_TYPE_SYM_FILE;
                        // if (attr.IsRelative()) linkType = IDR_LINK_TYPE_SYM_RELATIVE;
                    }

                    if (linkType != 0)
                        Set_LinkType_Radio(linkType);
                }
#else
                UString destPath;
                UString display;
                if (!QtGetPosixLinkTarget(us2fs(FilePath), destPath)) {
                    display = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "ERROR: "));
                    display += NError::MyFormatMessage(GetLastError());
                } else
                    display = destPath;
                SetItemText(IDT_LINK_PATH_TO_CUR, display);
                _pathFromCombo.SetText(FilePath);
                _pathToCombo.SetText(destPath);
                NFind::CFileInfo target;
                linkType = target.Find_FollowLink(us2fs(FilePath)) && target.IsDir()
                               ? IDR_LINK_TYPE_SYM_DIR
                               : IDR_LINK_TYPE_SYM_FILE;
#endif
            } else {
                // no ReparsePoint
                _pathFromCombo.SetText(AnotherPath);
                _pathToCombo.SetText(FilePath);
                if (fi.IsDir())
                    linkType = g_SymLink_Supported ? IDR_LINK_TYPE_SYM_DIR : IDR_LINK_TYPE_JUNCTION;
                else
                    linkType = IDR_LINK_TYPE_HARD;
            }
        }
        if (linkType != 0)
            Set_LinkType_Radio(linkType);
    }

    NormalizeSize();
}

void CLinkDialog::OnButtonClicked(unsigned buttonID) {
    switch (buttonID) {
    case IDB_LINK_PATH_FROM:
        OnButton_SetPath(false);
        return;
    case IDB_LINK_PATH_TO:
        OnButton_SetPath(true);
        return;
    case IDB_LINK_LINK:
        OnButton_Link();
        return;
    }
}

void CLinkDialog::OnButton_SetPath(bool to) {
    UString currentPath;
    CQtLinkCombo &combo = to ? _pathToCombo : _pathFromCombo;
    if (!combo.GetText(currentPath)) {
        ShowError(NError::MyFormatMessage(E_INVALIDARG));
        return;
    }
    // UString title = "Specify a location for output folder";
    const UString title = LangString(IDS_SET_FOLDER);

    UString resultPath;
    if (!MyBrowseForFolder(this, title, currentPath, resultPath))
        return;
    NName::NormalizeDirPathPrefix(resultPath);
    combo.SetCurSel(-1);
    combo.SetText(resultPath);
}

void CLinkDialog::ShowError(const wchar_t *s) {
    QMessageBox::critical(this, "7-Zip", QString::fromWCharArray(s));
}

void CLinkDialog::ShowLastErrorMessage() { ShowError(NError::MyFormatMessage(GetLastError())); }

void CLinkDialog::OnButton_Link() {
    UString from, to;
    if (!_pathFromCombo.GetText(from) || !_pathToCombo.GetText(to)) {
        ShowError(NError::MyFormatMessage(E_INVALIDARG));
        return;
    }

    if (from.IsEmpty())
        return;
    if (!NName::IsAbsolutePath(from))
        from.Insert(0, CurDirPrefix);

    unsigned idb = 0;
    for (unsigned i = 0;; i++) {
        if (i >= Z7_ARRAY_SIZE(k_LinkType_Buttons))
            return;
        idb = k_LinkType_Buttons[i];
        if (IsButtonCheckedBool(idb))
            break;
    }

    NFind::CFileInfo info1, info2;
    const bool finded1 = info1.Find(us2fs(from));
#ifdef _WIN32
    const bool finded2 = info2.Find(us2fs(to));
#else
    // Target classification only: POSIX lstat describes the link inode, while
    // Windows retains its file/directory reparse attribute. Creation keeps raw to.
    UString target = to;
    if (idb != IDR_LINK_TYPE_HARD && !to.IsEmpty() && !NName::IsAbsolutePath(to)) {
        // Preserve symlink/.. traversal in the parent and target paths.
        target = QFileInfo(QString::fromWCharArray(from.Ptr())).path().toStdWString().c_str();
        target.Add_PathSepar();
        target += to;
    }
    const bool finded2 = info2.Find_FollowLink(us2fs(target));
#endif

    const bool isDirLink = (idb == IDR_LINK_TYPE_SYM_DIR || idb == IDR_LINK_TYPE_JUNCTION);

    const bool isWSL = (idb == IDR_LINK_TYPE_WSL);

    if (!isWSL)
        if ((finded1 &&
#ifndef _WIN32
             !info1.IsOsSymLink() &&
#endif
             info1.IsDir() != isDirLink) ||
            (finded2 && info2.IsDir() != isDirLink)) {
            ShowError(
                QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Incorrect link type")));
            return;
        }

    if (idb == IDR_LINK_TYPE_HARD) {
        if (!NDir::MyCreateHardLink(us2fs(from), us2fs(to))) {
            ShowLastErrorMessage();
            return;
        }
    } else {
        if (finded1 && !info1.IsDir() && !info1.IsOsSymLink() && info1.Size != 0) {
            UString s = QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                  "WARNING: reparse point will hide the data of existing file"));
            s.Add_LF();
            s += from;
            ShowError(s);
            return;
        }

#ifdef _WIN32
        CByteBuffer data;
        const bool isSymLink = (idb != IDR_LINK_TYPE_JUNCTION);
        FillLinkData(data, to, isSymLink, isWSL);
        if (data.Size() == 0) {
            ShowError(QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Incorrect link")));
            return;
        }

        CReparseAttr attr;
        if (!attr.Parse(data, data.Size())) {
            ShowError(QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Internal conversion error")));
            return;
        }

        bool res;
        if (to.IsEmpty()) {
            // res = NIO::SetReparseData(us2fs(from), isDirLink, NULL, 0);
            res = NIO::DeleteReparseData(us2fs(from));
        } else
            res = NIO::SetReparseData(us2fs(from), isDirLink, data, (DWORD)data.Size());

        if (!res)
#else
        if (!QtSetPosixLink(from, to))
#endif
        {
            ShowLastErrorMessage();
            return;
        }
    }

    End(QDialog::Accepted);
}
