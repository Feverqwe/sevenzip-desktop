// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelFolderChange.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source address presentation, hierarchy and edit handlers; Qt supplies services.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/RootFolder.h"
#include "fileops/patterns/Wildcard.h"

static int GetRealIconIndex_for_DirPath(IQtPanelAddressCombo &header, CFSTR path, DWORD attrib) {
    attrib |= FILE_ATTRIBUTE_DIRECTORY; // optional
    int index = -1;
    if ((index = header.GetPathIconIndex(fs2us(path), attrib)) >= 0)
        if (index >= 0)
            return index;
    return g_Ext_to_Icon_Map.GetIconIndex_DIR(attrib);
}

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
