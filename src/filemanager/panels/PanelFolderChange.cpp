// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelFolderChange.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source binding/parent-stack bodies. History still uses an explicit app-state
// service; the remaining list-load/presentation stage is recorded in the audit.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/AppState.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
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

static int GetRealIconIndex_for_DirPath(IQtPanelAddressCombo &header, CFSTR path, DWORD attrib) {
    attrib |= FILE_ATTRIBUTE_DIRECTORY; // optional
    int index = -1;
    if ((index = header.GetPathIconIndex(fs2us(path), attrib)) >= 0)
        if (index >= 0)
            return index;
    return g_Ext_to_Icon_Map.GetIconIndex_DIR(attrib);
}

extern UString RootFolder_GetName_Computer(int &iconIndex);
extern UString RootFolder_GetName_Network(int &iconIndex);
extern UString RootFolder_GetName_Documents(int &iconIndex);

static int Find_FileExtension_DotPos_in_path(const wchar_t *path) {
    int dotPos = -1;
    unsigned i;
    for (i = 0;; i++) {
        const wchar_t c = path[i];
        if (c == 0)
            return dotPos;
        if (c == '.')
            dotPos = (int)i;
        else if (IS_PATH_SEPAR(c) || c == ':')
            dotPos = -1;
    }
}

void CPanel::LoadFullPathAndShow(IQtPanelAddressCombo &header, const UString &displayPath) {
    using namespace NWindows::NFile;
    using namespace NFind;
    LoadFullPath();
    header.SetText(displayPath);

#ifndef UNDER_CE

    int iconIndex_Current = -1;

    UString path = _currentFolderPrefix;
    // path = "\\\\.\\PhysicalDrive1\\"; // for debug
    // path = "\\\\.\\y:\\"; // for debug
    if (!path.IsEmpty()) {
        const unsigned rootPrefixSize = NName::GetRootPrefixSize(path);
        if (rootPrefixSize == 0 && path[0] != '\\') {
            int iconIndex = -1;
            UString name_Computer = RootFolder_GetName_Computer(iconIndex);
            iconIndex = header.GetNamespaceIconIndex(IDS_COMPUTER);
            name_Computer.Add_PathSepar();
            if (path == name_Computer || path.IsEqualTo("\\\\?\\"))
                iconIndex_Current = iconIndex;
            else {
                UString name = RootFolder_GetName_Network(iconIndex);
                iconIndex = header.GetNamespaceIconIndex(IDS_NETWORK);
                name.Add_PathSepar();
                if (path == name)
                    iconIndex_Current = iconIndex;
            }
        }

        if (iconIndex_Current < 0) {
            if (rootPrefixSize == 0 || rootPrefixSize == path.Len()) {
                DWORD attrib = FILE_ATTRIBUTE_DIRECTORY;
                CFileInfo info;
                if (info.Find(us2fs(path)))
                    attrib = info.GetWinAttrib();
#ifdef _WIN32
                NName::If_IsSuperPath_RemoveSuperPrefix(path);
#endif
                iconIndex_Current = GetRealIconIndex_for_DirPath(header, us2fs(path), attrib);
            }
#ifdef _WIN32
            else if (rootPrefixSize == NName::kDevicePathPrefixSize &&
                     NName::IsDevicePath(us2fs(path.Left(path.Len() - 1)))) {
                if (path.IsPrefixedBy_Ascii_NoCase("\\\\.\\"))
                    path.DeleteFrontal(4);
                if (path.Len() > 3) // is not "c:\\"
                {
                    // PhysicalDrive
                    if (IS_PATH_SEPAR(path.Back()))
                        path.DeleteBack();
                }
                iconIndex_Current = header.GetPathIconIndex(path, FILE_ATTRIBUTE_ARCHIVE);
            }
#endif
            else {
                if (IS_PATH_SEPAR(path.Back()))
                    path.DeleteBack();
                bool need_Fs_Check = true;
                bool is_File = false;
                if (!_parentFolders.IsEmpty()) {
                    const CFolderLink &link = _parentFolders.Back();
                    if (link.VirtualPath == path) {
                        is_File = true;
                        if (_parentFolders.Size() != 1)
                            need_Fs_Check = false;
                    } else
                        need_Fs_Check = false;
                }
                if (need_Fs_Check) {
                    CFileInfo info;
                    const bool finded = info.Find(us2fs(path));
                    DWORD attrib = FILE_ATTRIBUTE_DIRECTORY;
                    if (finded)
                        attrib = info.GetWinAttrib();
                    iconIndex_Current = header.GetPathIconIndex(path, attrib);
                }
                if (iconIndex_Current <= 0 && is_File) {
                    int dotPos = Find_FileExtension_DotPos_in_path(path);
                    if (dotPos < 0)
                        dotPos = (int)path.Len();
                    iconIndex_Current =
                        g_Ext_to_Icon_Map.GetIconIndex(FILE_ATTRIBUTE_ARCHIVE, path.Ptr(dotPos));
                }
            }
        }
    }

    if (iconIndex_Current < 0)
        iconIndex_Current = g_Ext_to_Icon_Map.GetIconIndex_DIR();
    // if (iconIndex_Current < 0) iconIndex_Current = 0;
    // iconIndex_Current = -1; // for debug
    header.SetCurrentIcon(iconIndex_Current);

#endif

    RefreshTitle();
}

void CPanel::CloseOneLevel() {
    ReleaseFolder();
    _library.Free();
    {
        CFolderLink &link = _parentFolders.Back();
        if (link.ParentFolder)
            SetNewFolder(link.ParentFolder);
        _library.Attach(link.Library.Detach());
    }
    if (_parentFolders.Size() > 1)
        OpenParentArchiveFolder();
    _parentFolders.DeleteBack();
    if (_parentFolders.IsEmpty())
        _flatMode = _flatModeForDisk;
}

void CPanel::CloseOpenFolders() {
    while (!_parentFolders.IsEmpty())
        CloseOneLevel();
    _flatMode = _flatModeForDisk;
    ReleaseFolder();
    _library.Free();
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
        SetToRootFolder();
        CMyComPtr<IFolderFolder> newFolder;
        if (sysPath.IsEmpty())
            _folder->BindToFolder(path, &newFolder);
        else if (fileInfo.IsDir()) {
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

// Original AddComboBoxItem/OnComboBoxCommand; Qt supplies control, icon,
// drive/mount and application delivery services. Source path construction and
// archive-link boundaries remain in this module.
void CPanel::OnNotifyComboBoxEnter(const UString &s, IQtPanelAddressCombo &header) {
    if (header.BindToPathAndRefresh(s, false) == S_OK)
        header.SetFocusToList();
}

bool CPanel::OnNotifyComboBoxEndEdit(QtAddressEditReason reason, IQtPanelAddressCombo &header) {
    if (reason == QtAddressEditReason::Escape) {
        header.SetText(_currentFolderPrefix);
        header.SetFocusToList();
        return true;
    }
    if (reason == QtAddressEditReason::Return) {
        OnNotifyComboBoxEnter(header.GetText(), header);
        return true;
    }
    return false;
}

void CPanel::AddComboBoxItem(IQtPanelAddressCombo &header, const UString &name, int iconIndex,
                             unsigned indent, bool addToList, UInt32 namespaceID) {
    header.InsertItem(name, iconIndex, indent, namespaceID);
    if (addToList) {
        UString s = name;
        s.Add_PathSepar();
        ComboBoxPaths.Add(s);
    }
}

bool CPanel::OnComboBoxCommand(QtAddressCommand code, IQtPanelAddressCombo &header) {
    switch (code) {
    case QtAddressCommand::DropDown: {
        ComboBoxPaths.Clear();
        header.ResetContent();

        UString sumPath;
        UStringVector pathParts;
        unsigned indent = 0;
        {
            UString path = _currentFolderPrefix;
            // path = "\\\\.\\y:\\"; // for debug
            UString prefix0;
            if (path.IsPrefixedBy_Ascii_NoCase("\\\\")) {
                const int separ = FindCharPosInString(path.Ptr(2), '\\');
                if (separ > 0 && (separ > 1 || path[2] != '.')) // "\\\\.\\" will be processed later
                {
                    const UString s = path.Left(2 + separ);
                    prefix0 = s;
                    prefix0.Add_PathSepar();
                    AddComboBoxItem(
                        header, s, header.GetIconIndex(prefix0, FILE_ATTRIBUTE_DIRECTORY), indent++,
                        false); // addToList
                    ComboBoxPaths.Add(prefix0);
                }
            }

            unsigned rootPrefixSize = NWindows::NFile::NName::GetRootPrefixSize(path);

            sumPath = path;

            if (rootPrefixSize <= prefix0.Len()) {
                rootPrefixSize = prefix0.Len();
                sumPath.DeleteFrom(rootPrefixSize);
            } else {
                // rootPrefixSize > prefix0.Len()
                sumPath.DeleteFrom(rootPrefixSize);

                NWindows::NFile::NFind::CFileInfo info;
                DWORD attrib = FILE_ATTRIBUTE_DIRECTORY;
                if (info.Find(us2fs(sumPath)) && info.IsDir())
                    attrib = info.GetWinAttrib();
                UString s = sumPath.Ptr(prefix0.Len());
                if (!s.IsEmpty()) {
                    const wchar_t c = s.Back();
                    if (IS_PATH_SEPAR(c))
                        s.DeleteBack();
                }
                UString path_for_icon = sumPath;
#ifdef _WIN32
                NWindows::NFile::NName::If_IsSuperPath_RemoveSuperPrefix(path_for_icon);
#endif

                AddComboBoxItem(header, s, header.GetIconIndex(path_for_icon, attrib), indent++,
                                false); // addToList
                ComboBoxPaths.Add(sumPath);
            }

            path.DeleteFrontal(rootPrefixSize);
            SplitPathToParts(path, pathParts);
        }

        // it's expected that pathParts.Back() is empty, because _currentFolderPrefix has
        // PathSeparator.
        unsigned next_Arc_index = 0;
        int iconIndex_Computer;
        const UString name_Computer = RootFolder_GetName_Computer(iconIndex_Computer);

        // const bool is_devicePrefix = (sumPath.IsEqualTo("\\\\.\\"));

        if (pathParts.Size() > 1)
            if (!sumPath.IsEmpty() || pathParts.Size() != 2 || pathParts[0] != name_Computer)
                for (unsigned i = 0; i + 1 < pathParts.Size(); i++) {
                    UString name = pathParts[i];
                    sumPath += name;

                    bool isRootDir_inLink = false;
                    if (next_Arc_index < _parentFolders.Size()) {
                        const CFolderLink &link = _parentFolders[next_Arc_index];
                        if (link.VirtualPath == sumPath) {
                            isRootDir_inLink = true;
                            next_Arc_index++;
                        }
                    }

                    int iconIndex = -1;
                    DWORD attrib =
                        isRootDir_inLink ? FILE_ATTRIBUTE_ARCHIVE : FILE_ATTRIBUTE_DIRECTORY;
                    if (next_Arc_index == 0 || (next_Arc_index == 1 && isRootDir_inLink)) {
#ifdef _WIN32
                        if (i == 0 && NWindows::NFile::NName::IsDevicePath(us2fs(sumPath))) {
                            UString path = name;
                            path.Add_PathSepar();
                            attrib = FILE_ATTRIBUTE_ARCHIVE;
                            // FILE_ATTRIBUTE_DIRECTORY;
                        } else
#endif
                        {
                            NWindows::NFile::NFind::CFileInfo info;
                            if (info.Find(us2fs(sumPath)))
                                attrib = info.GetWinAttrib();
                        }
                        iconIndex = header.GetIconIndex(sumPath, attrib);
                    }

                    if (iconIndex < 0)
                        iconIndex = header.GetIconIndex(name, attrib);
                    // iconIndex = -1; // for debug
                    if (iconIndex < 0 && isRootDir_inLink)
                        iconIndex = 0; // default file

                    sumPath.Add_PathSepar();

                    ComboBoxPaths.Add(sumPath);
                    if (name.IsEmpty())
                        name.Add_PathSepar();
                    AddComboBoxItem(header, name, iconIndex, indent++,
                                    false); // addToList
                }

#ifndef UNDER_CE

#ifdef _WIN32
        {
            int iconIndex;
            const UString name = RootFolder_GetName_Documents(iconIndex);
            // iconIndex = -1; // for debug
            AddComboBoxItem(header, name, iconIndex, 0, true, IDS_DOCUMENTS);
        }
#endif
        AddComboBoxItem(header, name_Computer, iconIndex_Computer, 0, true, IDS_COMPUTER);
#ifndef _WIN32
        {
            CRootFolder root;
            root.Init();
            NWindows::NCOM::CPropVariant name;
            static_cast<IFolderFolder &>(root).GetProperty(1, kpidName, &name);
            if (name.vt == VT_BSTR)
                AddComboBoxItem(header, UString(name.bstrVal), -1, 0, true, UInt32(-1));
        }
#endif
        {
            FStringVector driveStrings;
            header.GetLogicalDriveStrings(driveStrings);
            FOR_VECTOR(i, driveStrings) {
                FString s = driveStrings[i];
                ComboBoxPaths.Add(fs2us(s));
                int iconIndex2 = header.GetIconIndex(fs2us(s), FILE_ATTRIBUTE_DIRECTORY);
                if (!s.IsEmpty()) {
                    const FChar c = s.Back();
                    if (IS_PATH_SEPAR(c))
                        s.DeleteBack();
                }
                // iconIndex2 = -1; // for debug
                AddComboBoxItem(header, fs2us(s), iconIndex2, 1, false);
            }
        }
        {
            int iconIndex;
            const UString name = RootFolder_GetName_Network(iconIndex);
            AddComboBoxItem(header, name, iconIndex, 0, true, IDS_NETWORK);
        }

#endif

        return false;
    }

    case QtAddressCommand::SelectionAccepted: {
        int index = header.GetCurSel();
        if (index >= 0 && (unsigned)index < ComboBoxPaths.Size()) {
            const UString path = ComboBoxPaths[index];
            header.SetCurSel(-1);
            // header.SetText(pass); // it's fix for selecting by mouse.
            if (header.BindToPathAndRefresh(path, true) == S_OK) {
                header.SetFocusToList();
                return true;
            }
        }
        return false;
    }
    }
    return false;
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
