// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelFolderChange.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source binding/parent-stack bodies. History still uses an explicit app-state
// service; the remaining list-load/presentation stage is recorded in the audit.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "archive/opening/archivecontainerpath.h"
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/AppState.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSDrives.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/RootFolder.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"

static const UInt32 kParentIndex = (UInt32)-1;

void CPanel::OpenAltStreams() {
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    Int32 realIndex = -1;
    if (indices.Size() > 1)
        return;
    if (indices.Size() == 1)
        realIndex = (Int32)indices[0];

    if (_folderAltStreams) {
        CMyComPtr<IFolderFolder> newFolder;
        _folderAltStreams->BindToAltStreams((UInt32)realIndex, &newFolder);
        if (newFolder) {
            CDisableTimerProcessing disableTimerProcessing(*this);
            CDisableNotify disableNotify(*this);
            SetNewFolder(newFolder);
            RefreshListCtrl();
            return;
        }
        return;
    }

#if defined(_WIN32) && !defined(UNDER_CE)
    UString path;
    if (realIndex >= 0)
        path = GetItemFullPath((UInt32)realIndex);
    else {
        path = GetFsPath();
        if (!NWindows::NFile::NName::IsDriveRootPath_SuperAllowed(us2fs(path)))
            if (!path.IsEmpty() && IS_PATH_SEPAR(path.Back()))
                path.DeleteBack();
    }
    path.Add_Colon();
    BindToPathAndRefresh(path);
#endif
}

void CPanel::OpenFolder(unsigned index) {
    if (index == kParentIndex) {
        OpenParentFolder();
        return;
    }
    CMyComPtr<IFolderFolder> newFolder;
    const HRESULT res = _folder->BindToFolder((unsigned)index, &newFolder);
    if (res != 0) {
        MessageBox_Error_HRESULT(res);
        return;
    }
    if (!newFolder)
        return;
    SetNewFolder(newFolder);
    LoadFullPath();
    RefreshListCtrl();
    // 17.02: fixed : now we don't select first item
    _selectionList.EnsureVisible(_selectionList.GetFocusedItem());
}

void CPanel::ReleaseFolder() {
    ++_folderBindingRevision;
    _refreshSelection.reset();
    DeleteListItems();
    _folder.Release();
    _folderCompare.Release();
    _folderGetItemName.Release();
    _folderRawProps.Release();
    _folderAltStreams.Release();
    _folderOperations.Release();
    _thereAreDeletedItems = false;
    // Clear the source load-stage caches when a folder is released.
    _isDirVector.Clear();
    _itemIconIndices.Clear();
    _itemSystemIcons.Clear();
    _folderGetSystemIconIndex.Release();
    _listLoadResult = S_OK;
    _loadedItemCount.reset();
    _selectedStatusVector.Clear();
}

void CPanel::SetNewFolder(IFolderFolder *newFolder) {
    ReleaseFolder();
    _folder = newFolder;
    if (_folder) {
        _folder.QueryInterface(IID_IFolderCompare, &_folderCompare);
        _folder.QueryInterface(IID_IFolderGetItemName, &_folderGetItemName);
        _folder.QueryInterface(IID_IArchiveGetRawProps, &_folderRawProps);
        _folder.QueryInterface(IID_IFolderAltStreams, &_folderAltStreams);
        _folder.QueryInterface(IID_IFolderOperations, &_folderOperations);
    }
}

void CPanel::LoadFullPath() {
    _currentFolderPrefix.Empty();
    FOR_VECTOR(i, _parentFolders) {
        const CFolderLink &folderLink = _parentFolders[i];
        _currentFolderPrefix += folderLink.ParentFolderPath;
        _currentFolderPrefix += folderLink.RelPath;
        _currentFolderPrefix.Add_PathSepar();
    }
    if (_folder)
        _currentFolderPrefix += GetFolderPath(_folder);
}

void CPanel::SetToRootFolder() {
    ReleaseFolder();
    _library.Free();
    CRootFolder *rootFolderSpec = new CRootFolder;
    SetNewFolder(rootFolderSpec);
    rootFolderSpec->Init();
}

UString CPanel::PrepareBindPath(const UString &path) {
    UString s = path;
#ifdef _WIN32
    if (!s.IsEmpty() && s[0] == '\"' && s.Back() == '\"') {
        s.DeleteBack();
        s.Delete(0);
    }
#endif
    return s;
}

HRESULT CPanel::BindToPathAndRefresh(const UString &path) {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    COpenResult openRes;
    const UString s = PrepareBindPath(path);
    HRESULT res = BindToPath(s, UString(), openRes);
    RefreshListCtrl();
    return res;
}

UString CPanel::GetParentDirPrefix() const {
    UString s;
    if (!_currentFolderPrefix.IsEmpty()) {
        wchar_t c = _currentFolderPrefix.Back();
        if (IS_PATH_SEPAR(c) || c == ':') {
            s = _currentFolderPrefix;
            s.DeleteBack();
            if (!s.IsEqualTo("\\\\.") && !s.IsEqualTo("\\\\?")) {
                int pos = s.ReverseFind_PathSepar();
                if (pos >= 0)
                    s.DeleteFrom((unsigned)(pos + 1));
            }
        }
    }
    return s;
}

void CPanel::OpenParentFolder() {
    LoadFullPath(); // Maybe we don't need it ??
    UString parentFolderPrefix;
    UString focusedName;
    if (!_currentFolderPrefix.IsEmpty()) {
        wchar_t c = _currentFolderPrefix.Back();
        if (IS_PATH_SEPAR(c) || c == ':') {
            focusedName = _currentFolderPrefix;
            focusedName.DeleteBack();
            /*
            if (c == ':' && !focusedName.IsEmpty() && IS_PATH_SEPAR(focusedName.Back()))
            {
              focusedName.DeleteBack();
            }
            else
            */
            if (!focusedName.IsEqualTo("\\\\.") && !focusedName.IsEqualTo("\\\\?")) {
                const int pos = focusedName.ReverseFind_PathSepar();
                if (pos >= 0) {
                    parentFolderPrefix = focusedName;
                    parentFolderPrefix.DeleteFrom((unsigned)(pos + 1));
                    focusedName.DeleteFrontal((unsigned)(pos + 1));
                }
            }
        }
    }
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    CMyComPtr<IFolderFolder> newFolder;
    _folder->BindToParentFolder(&newFolder);
    // newFolder.Release(); // for test
    if (newFolder)
        SetNewFolder(newFolder);
    else {
        bool needSetFolder = true;
        if (!_parentFolders.IsEmpty()) {
            {
                const CFolderLink &link = _parentFolders.Back();
                parentFolderPrefix = link.ParentFolderPath;
                focusedName = link.RelPath;
            }
            CloseOneLevel();
            needSetFolder = (!_folder);
        }
        if (needSetFolder) {
            {
                COpenResult openRes;
                BindToPath(parentFolderPrefix, UString(), openRes);
            }
        }
    }
    CSelectedState state;
    state.FocusedName = focusedName;
    state.FocusedName_Defined = true;
    /*
    if (!focusedName.IsEmpty())
      state.SelectedNames.Add(focusedName);
    */
    LoadFullPath();
    // ::SetCurrentDirectory(::_currentFolderPrefix);
    RefreshListCtrl(state);
    // _listView.EnsureVisible(_listView.GetFocusedItem(), false);
}

void CPanel::OpenRootFolder() {
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    _parentFolders.Clear();
    SetToRootFolder();
    RefreshListCtrl();
    // ::SetCurrentDirectory(::_currentFolderPrefix);
    /*
    BeforeChangeFolder();
    _currentFolderPrefix.Empty();
    AfterChangeFolder();
    SetCurrentPathText();
    RefreshListCtrl(UString(), 0, UStringVector());
    _listView.EnsureVisible(_listView.GetFocusedItem(), false);
    */
}

void CPanel::OpenDrivesFolder() {
    CloseOpenFolders();
#ifdef UNDER_CE
    NFsFolder::CFSFolder *folderSpec = new NFsFolder::CFSFolder;
    SetNewFolder(folderSpec);
    folderSpec->InitToRoot();
#else
    CFSDrives *folderSpec = new CFSDrives;
    SetNewFolder(folderSpec);
    folderSpec->Init();
#endif
    RefreshListCtrl();
}

#ifdef _WIN32
static bool DoesNameContainWildcard_SkipRoot(const UString &path) {
    return DoesNameContainWildcard(path.Ptr(NWindows::NFile::NName::GetRootPrefixSize(path)));
}
#endif

HRESULT CPanel::BindToPath(const UString &fullPath, const UString &arcFormat,
                           COpenResult &openRes) {
    using namespace NWindows::NFile;
    using namespace NFind;
    UString path = fullPath;
#ifdef _WIN32
    path.Replace(L'/', WCHAR_PATH_SEPARATOR);
#endif
    openRes.ArchiveIsOpened = false;
    openRes.Encrypted = false;
    CDisableTimerProcessing disableTimerProcessing(*this);
    CDisableNotify disableNotify(*this);
    for (; !_parentFolders.IsEmpty(); CloseOneLevel()) {
        const CFolderLink &link = _parentFolders.Back();
        const UString &virtPath = link.VirtualPath;
        if (!path.IsPrefixedBy(virtPath))
            continue;
        UString relatPath = path.Ptr(virtPath.Len());
        if (!relatPath.IsEmpty()) {
            if (!IS_PATH_SEPAR(relatPath[0]))
                continue;
            else
                relatPath.Delete(0);
        }
        UString relatPath2 = relatPath;
        if (!relatPath2.IsEmpty() && !IS_PATH_SEPAR(relatPath2.Back()))
            relatPath2.Add_PathSepar();
        for (;;) {
            const UString foldPath = GetFolderPath(_folder);
            if (relatPath2 == foldPath)
                break;
            if (relatPath.IsPrefixedBy(foldPath)) {
                path = relatPath.Ptr(foldPath.Len());
                break;
            }
            CMyComPtr<IFolderFolder> newFolder;
            if (_folder->BindToParentFolder(&newFolder) != S_OK)
                throw 20140918;
            if (!newFolder)
                throw 20140918;
            SetNewFolder(newFolder);
        }
        break;
    }
    if (_parentFolders.IsEmpty()) {
        CloseOpenFolders();
        UString sysPath = path;
        const unsigned prefixSize = NName::GetRootPrefixSize(sysPath);
        if (prefixSize == 0 || sysPath[prefixSize] == 0)
            sysPath.Empty();
#if defined(_WIN32) && !defined(UNDER_CE)
        if (!sysPath.IsEmpty() && sysPath.Back() == ':' &&
            (sysPath.Len() != 2 || !NName::IsDrivePath2(sysPath))) {
            UString baseFile = sysPath;
            baseFile.DeleteBack();
            if (NFind::DoesFileOrDirExist(us2fs(baseFile)))
                sysPath.Empty();
        }
#endif
        CFileInfo fileInfo;
        while (!sysPath.IsEmpty()) {
            if (sysPath.Len() <= prefixSize) {
                path.DeleteFrom(prefixSize);
                sysPath.Empty();
                break;
            }
            fileInfo.ClearBase();
            if (IsPathSepar(sysPath.Back())) {
                CDirEntry fi; // POSIX enumerator entry type; Windows aliases CFileInfo.
                CEnumerator enumerator;
                enumerator.SetDirPrefix(us2fs(sysPath));
                bool found = false;
                if (enumerator.Next(fi, found)) {
                    fileInfo.SetAsDir();
                    fileInfo.Size = 0;
                    fileInfo.Name.Empty();
                    break;
                }
                sysPath.DeleteBack();
                continue;
            }
            if (fileInfo.Find(us2fs(sysPath)))
                break;
            int pos = sysPath.ReverseFind_PathSepar();
            if (pos < 0) {
                sysPath.Empty();
                break;
            }
            {
                if ((unsigned)pos != sysPath.Len() - 1)
                    pos++;
                sysPath.DeleteFrom((unsigned)pos);
            }
        }
        // The directory probe retains a separator. A package must be opened
        // by its container name before binding the remaining virtual suffix.
        if (!sysPath.IsEmpty() && IsPathSepar(sysPath.Back()) && fileInfo.IsDir()) {
            const UString directoryName = sysPath.Left(sysPath.Len() - 1);
            if (IsSparseBundleName(directoryName))
                sysPath = directoryName;
        }
        SetToRootFolder();
        CMyComPtr<IFolderFolder> newFolder;
        if (sysPath.IsEmpty())
            _folder->BindToFolder(path, &newFolder);
        else if (fileInfo.IsDir() && !IsSparseBundleName(sysPath)) {
#ifdef _WIN32
            if (DoesNameContainWildcard_SkipRoot(sysPath)) {
                FString dirPrefix, fileName;
                NDir::GetFullPathAndSplit(us2fs(sysPath), dirPrefix, fileName);
                if (DoesNameContainWildcard_SkipRoot(fs2us(dirPrefix)))
                    return E_INVALIDARG;
                sysPath = fs2us(dirPrefix + fileInfo.Name);
            }
#endif
            NName::NormalizeDirPathPrefix(sysPath);
            _folder->BindToFolder(sysPath, &newFolder);
        } else {
            FString dirPrefix, fileName;
            NDir::GetFullPathAndSplit(us2fs(sysPath), dirPrefix, fileName);
            HRESULT res = S_OK;
#ifdef _WIN32
            if (DoesNameContainWildcard_SkipRoot(fs2us(dirPrefix)))
                return E_INVALIDARG;
            if (DoesNameContainWildcard(fs2us(fileName)))
                res = S_FALSE;
            else
#endif
            {
                CTempFileInfo tfi;
                tfi.RelPath = fs2us(fileName);
                tfi.FolderPath = dirPrefix;
                tfi.FilePath = us2fs(sysPath);
                res = OpenAsArc(NULL, tfi, sysPath, arcFormat, openRes);
            }
            if (res == S_FALSE)
                _folder->BindToFolder(fs2us(dirPrefix), &newFolder);
            else {
                RINOK(res)
                openRes.ArchiveIsOpened = true;
                _parentFolders.Back().ParentFolderPath = fs2us(dirPrefix);
                path.DeleteFrontal(sysPath.Len());
                if (!path.IsEmpty() && IS_PATH_SEPAR(path[0]))
                    path.Delete(0);
            }
        }
        if (newFolder) {
            SetNewFolder(newFolder);
            return S_OK;
        }
    }
    {
        for (unsigned curPos = 0; curPos != path.Len();) {
            UString s = path.Ptr(curPos);
            const int slashPos = NName::FindSepar(s);
            unsigned skipLen = s.Len();
            if (slashPos >= 0) {
                s.DeleteFrom((unsigned)slashPos);
                skipLen = (unsigned)slashPos + 1;
            }
            CMyComPtr<IFolderFolder> newFolder;
            _folder->BindToFolder(s, &newFolder);
            if (newFolder)
                curPos += skipLen;
            else if (_folderAltStreams) {
                const int pos = s.Find(L':');
                if (pos >= 0) {
                    UString baseName = s;
                    baseName.DeleteFrom((unsigned)pos);
                    if (_folderAltStreams->BindToAltStreams(baseName, &newFolder) == S_OK &&
                        newFolder)
                        curPos += (unsigned)pos + 1;
                }
            }
            if (!newFolder)
                break;
            SetNewFolder(newFolder);
        }
    }
    return S_OK;
}

UString GetFolderPath(IFolderFolder *folder) {
    {
        NWindows::NCOM::CPropVariant prop;
        if (folder->GetFolderProperty(kpidPath, &prop) == S_OK)
            if (prop.vt == VT_BSTR)
                return (wchar_t *)prop.bstrVal;
    }
    return UString();
}

void CPanel::SetBookmark(CFastFolders &fastFolders, unsigned index,
                         const UString &currentFolderPrefix) {
    fastFolders.SetString(index, currentFolderPrefix);
}

UString CPanel::OpenBookmark(CFastFolders &fastFolders, unsigned index) {
    return fastFolders.GetString(index);
}

QString CPanel::FoldersHistory(CFolderHistory &history, const QString &settingsPath,
                               QWidget *parent) {
    CListViewDialog listViewDialog(parent);
    listViewDialog.SettingsPath = settingsPath;
    listViewDialog.DeleteIsAllowed = true;
    listViewDialog.SelectFirst = true;
    listViewDialog.Title = LangString(IDS_FOLDERS_HISTORY);
    history.GetList(listViewDialog.Strings);
    listViewDialog.OnInit();
    QtBindLangText(&listViewDialog, IDS_FOLDERS_HISTORY);
    if (listViewDialog.exec() != QDialog::Accepted)
        return QString();
    if (listViewDialog.StringsWereChanged) {
        history.RemoveAll();
        for (int i = (int)listViewDialog.Strings.Size() - 1; i >= 0; i--)
            history.AddString(listViewDialog.Strings[(unsigned)i]);
        history.Save(settingsPath);
    }
    return listViewDialog.FocusedItemIndex >= 0
               ? ToQString(listViewDialog.Strings[(unsigned)listViewDialog.FocusedItemIndex])
               : QString();
}
