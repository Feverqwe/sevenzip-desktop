// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItemOpen.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Item launch policy, warnings and changed-file update presentation.
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QWidget>
#include <memory>
#include <utility>

using namespace NWindows;
using namespace NFile;
using namespace NDir;
static const UInt32 kParentIndex = (UInt32)-1;

void CPanel::MessageBox_LastError() const {
    MessageBox_Error_HRESULT(GetLastError_noZero_HRESULT());
}

QWidget *CPanel::GetParent() const {
    if (_parentWindow)
        return _parentWindow;
    return _listView ? _listView->window() : _mainWindow.data();
}

void CPanel::MessageBox_Error_UnsupportOperation() const {
    MessageBox_Error(LangString(IDS_OPERATION_IS_NOT_SUPPORTED));
}

static const char *const kExeExtensions = " exe bat ps1 com lnk"
                                          " ";

static const char *const kStartExtensions =
#ifdef UNDER_CE
    " cab"
#endif
    " exe bat ps1 com lnk"
    " chm"
    " msi doc dot xls ppt pps wps wpt wks xlr wdb vsd pub"

    " docx docm dotx dotm xlsx xlsm xltx xltm xlsb xps"
    " xlam pptx pptm potx potm ppam ppsx ppsm vsdx xsn"
    " mpp"
    " msg"
    " dwf"

    " flv swf"

    " epub"
    " odt ods"
    " wb3"
    " pdf"
    " ps"
    " txt"
    " xml xsd xsl xslt hxk hxc htm html xhtml xht mht mhtml htw asp aspx css cgi jsp shtml"
    " h hpp hxx c cpp cxx m mm go swift"
    " awk sed hta js json php php3 php4 php5 phptml pl pm py pyo rb tcl ts vbs"
    " asm"
    " mak clw csproj vcproj sln dsp dsw"
    " ";

// bool FindExt(const char *p, const UString &name, AString &s);
bool FindExt(const char *p, const UString &name, CStringFinder &finder);

bool CPanel::DoItemAlwaysStart(const UString &name) {
    CStringFinder finder;
    return FindExt(kStartExtensions, name, finder);
}

void CPanel::OpenFolderExternal(unsigned index) {
    UString prefix = GetFsPath();
    UString path = prefix;
    if (index == kParentIndex) {
        if (prefix.IsEmpty())
            return;
        const wchar_t c = prefix.Back();
        if (!IS_PATH_SEPAR(c) && c != ':')
            return;
        prefix.DeleteBack();
        int pos = prefix.ReverseFind_PathSepar();
        if (pos < 0)
            return;
        prefix.DeleteFrom((unsigned)(pos + 1));
        path = prefix;
    } else {
        path += GetItemRelPath(index);
        path.Add_PathSepar();
    }
    // This disk/folder caller closes any returned native handle without waiting.
    if (_externalFileServices)
        _externalFileServices->StartApplication(prefix, path, GetParent());
    else
        MessageBox_Error_UnsupportOperation();
}

void CPanel::OpenItem(unsigned index, bool tryInternal, bool tryExternal, const wchar_t *type) {
    std::optional<CDisableTimerProcessing> disableTimerProcessing(std::in_place, *this);
    const UString name = GetItemRelPath2(index);
    if (tryExternal)
        if (IsVirus_Message(name))
            return;
    if (!_parentFolders.IsEmpty()) {
        // The nested opener retains this guard until its own refresh handoff.
        disableTimerProcessing.reset();
        OpenItemInArchive(index, tryInternal, tryExternal, type);
        return;
    }
    std::optional<CDisableNotify> disableNotify(std::in_place, *this);
    UString prefix = GetFsPath();
    UString fullPath = prefix + name;
    if (IsFSFolder() && !QFileInfo::exists(ToQString(fullPath))) {
        MessageBox_Error(
            ToUString(QCoreApplication::translate("mainwindow_archiveopening", "File not found: ") +
                      ToQString(fullPath)));
        return;
    }
    if (tryInternal)
        if (!tryExternal || !DoItemAlwaysStart(name)) {
            // OpenAsArc_Index guards binding itself; Refresh must retain the
            // caller's enabled state when its Qt rows finish asynchronously.
            disableNotify.reset();
            disableTimerProcessing.reset();
            HRESULT res = OpenAsArc_Index(index, type);
            if (res != S_OK)
                disableTimerProcessing.emplace(*this);
            _selectionList.Invalidate();
            if (res == S_OK || res == E_ABORT)
                return;
            if (res != S_FALSE) {
                // OpenAsArc_Msg already presented this opening failure.
                return;
            }
        }
    if (tryExternal) {
        if (_externalFileServices)
            _externalFileServices->StartApplication(prefix, fullPath, GetParent());
        else
            MessageBox_Error_UnsupportOperation();
    }
}

void CPanel::EditItem(unsigned index, bool) { OpenItem(index, false, true); }

bool CPanel::IsVirus_Message(const UString &name) {
    UString name2;

    const wchar_t cRLO = (wchar_t)0x202E;
    bool isVirus = false;
    bool isSpaceError = false;
    {
        unsigned numSpaces = 0;
        for (unsigned i = 0; i < name.Len(); i++) {
            const wchar_t c = name[i];
            switch (c) {
            case ' ':
            case '\t':
            case 0x00A0: // Non-Breaking Space
            case 0x2002: // En Space
            case 0x2003: // Em Space
            case 0x2004: // Three-Per-Em Space
            case 0x2005: // Four-Per-Em Space
            case 0x2006: // Six-Per-Em Space
            case 0x2007: // Figure Space
            case 0x2008: // Punctuation Space
            case 0x2009: // Thin Space
            case 0x200A: // Hair Space
            case 0x200B: // Zero Width Space
            case 0x202F: // Narrow Non-Breaking Space
            case 0x205F: // Medium Mathematical Space
            case 0x3000: // Ideographic Space
                numSpaces++;
                break;
            default:
                numSpaces = 0;
                break;
            }
            if (numSpaces <= 1)
                name2 += c;
            if (numSpaces >= 5) {
                isVirus = true;
                isSpaceError = true;
            }
        }
    }
    if (name2.Find(cRLO) >= 0) {
        const UString badString(cRLO);
        name2.Replace(badString, L"[RLO]");
        isVirus = true;
    }

#ifdef _WIN32
    {
        unsigned i;
        for (i = name2.Len(); i != 0;) {
            wchar_t c = name2[i - 1];
            if (c != '.' && c != ' ')
                break;
            i--;
            name2.ReplaceOneCharAtPos(i, '_');
        }
        if (i != name2.Len()) {
            CStringFinder finder;
            UString name3 = name2;
            name3.DeleteFrom(i);
            if (FindExt(kExeExtensions, name3, finder))
                isVirus = true;
        }
    }
#endif

    if (!isVirus)
        return false;

    UString s = LangString(IDS_VIRUS);

    if (!isSpaceError) {
        const int pos1 = s.Find(L'(');
        if (pos1 >= 0) {
            const int pos2 = s.Find(L')', (unsigned)pos1 + 1);
            if (pos2 >= 0) {
                s.Delete((unsigned)pos1, (unsigned)pos2 + 1 - (unsigned)pos1);
                if (pos1 > 0 && s[pos1 - 1] == ' ' && s[pos1] == '.')
                    s.Delete(pos1 - 1);
            }
        }
    }

    UString name3 = name;
    name3.Replace(L'\n', L'_');
    name2.Replace(L'\n', L'_');

    s.Add_LF();
    s += name2;
    s.Add_LF();
    s += name3;

    MessageBox_Error(s);
    return true;
}

// Native OnOpenItemChanged message body. A typed request replaces LPARAM and
// the window handle; the external service retains the temp file during the call.
bool CPanel::OnOpenItemChanged(const CTempFileInfo &tpi, const UString &fullPathFolderPrefix,
                               bool usePassword, const UString &password,
                               std::optional<CSelectedState> *refreshSelection) {
    if (refreshSelection)
        refreshSelection->reset();
    if (fullPathFolderPrefix != _currentFolderPrefix || !_folder)
        return false;
    UInt32 fileIndex = tpi.FileIndex;
    UInt32 numItems = 0;
    if (_folder->GetNumberOfItems(&numItems) != S_OK)
        return false;
    // Source limitation: duplicate names can still make a relocated index
    // ambiguous after another archive update.
    if (fileIndex >= numItems || GetItemRelPath(fileIndex) != tpi.RelPath) {
        UInt32 i;
        for (i = 0; i < numItems; i++)
            if (GetItemRelPath(i) == tpi.RelPath)
                break;
        if (i == numItems)
            return false;
        fileIndex = i;
    }
    CSelectedState state;
    SaveSelectedState(state);
    CDisableNotify disableNotify(*this);
    HRESULT result = OnOpenItemChanged(fileIndex, fs2us(tpi.FilePath), usePassword, password);
    // Publish the reached source refresh phase after the caller's operation
    // guards unwind, including when the native writer failed.
    if (refreshSelection)
        *refreshSelection = state;
    else
        RefreshListCtrl(state);
    return result == S_OK;
}

class CThreadCopyFrom : public CProgressThreadVirt {
    HRESULT ProcessVirt() override;

  public:
    UString FullPath;
    UInt32 ItemIndex;
    CMyComPtr<IFolderOperations> FolderOperations;
    CMyComPtr2<IProgress, CUpdateCallback100Imp> UpdateCallback;
};

HRESULT CThreadCopyFrom::ProcessVirt() {
    return FolderOperations->CopyFromFile(ItemIndex, FullPath, UpdateCallback);
}

HRESULT CPanel::OnOpenItemChanged(UInt32 index, const wchar_t *fullFilePath, bool usePassword,
                                  const UString &password) {
    return UpdateExternalFile(index, fullFilePath, usePassword, password).Result;
}

QtArchiveUpdateResult CPanel::UpdateExternalFile(UInt32 index, const wchar_t *fullFilePath,
                                                 bool usePassword, const UString &password) {
    if (!_folderOperations) {
        MessageBox_Error_UnsupportOperation();
        return {E_FAIL, false};
    }
    CThreadCopyFrom t;
    t.UpdateCallback.SetFromCls(new CUpdateCallback100Imp);
    t.UpdateCallback->ProgressDialog = &t;
    t.ItemIndex = index;
    t.FullPath = fullFilePath;
    t.FolderOperations = _folderOperations;
    std::unique_ptr<IQtProgressTransport> transport;
    if (auto *services =
            dynamic_cast<IQtProgressServices *>((IFolderOperations *)_folderOperations))
        transport = services->CreateProgressTransport(t);
    t.UpdateCallback->Init();
    if (!_parentFolders.IsEmpty())
        t.UpdateCallback->PasswordArchivePath = _parentFolders.Back().VirtualPath;
    t.UpdateCallback->PasswordIsDefined = usePassword;
    t.UpdateCallback->Password = password;
    const HRESULT created = t.Create(GetItemName(index), GetParent());
    return {created == S_OK ? t.Result : created, t.UpdateCallback->Published};
}
