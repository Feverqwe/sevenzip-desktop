// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelMenu.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt supplies menus; native shell invocation remains a separate service.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/panelmenuids.h"
#include "filemanager/panels/qtpanelcontextmenuservices.h"
#include "filemanager/panels/qtpanelpluginservices.h"
#include "filemanager/qtfilemenuservices.h"
#include "localization/LangUtils.h"
#include "platform/shell/ContextMenu.h"
#include "settings/ViewSettings.h"
#include <QApplication>
#include <QClipboard>

void CPanel::EditCut() {
    if (_fileClipboardPresentation) {
        _fileClipboardPresentation(0);
        return;
    }
    // InvokeSystemCommand("cut");
}

void CPanel::EditCopy() {
    if (_fileClipboardPresentation) {
        _fileClipboardPresentation(1);
        return;
    }
    UString s;
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Selected(indices);
    FOR_VECTOR(i, indices) {
        if (i != 0)
            s += "\xD\n";
        s += GetItemName(indices[i]);
    }
    // Qt's Unicode clipboard replaces ClipboardSetText(_mainWindow, s).
    QApplication::clipboard()->setText(ToQString(s));
}

void CPanel::EditPaste() {
    if (_fileClipboardPresentation) {
        _fileClipboardPresentation(2);
        return;
    }
    // The upstream file-transfer and InvokeSystemCommand("paste") bodies
    // are commented out. Preserve its active no-op.
}

bool CPanel::InvokePluginCommand(unsigned id, IQtPanelPluginServices &services) {
    UInt32 offset;
    const bool isSystemMenu = (id >= kSystemStartMenuID);
    if (isSystemMenu) {
        if (!services.HasContextMenu(true))
            return false;
        offset = id - kSystemStartMenuID;
    } else {
        if (!services.HasContextMenu(false))
            return false;
        offset = id - kSevenZipStartMenuID;
    }
    // CMINVOKECOMMANDINFO's numeric verb, parent and current directory are
    // transported by the native/Qt facade rather than Win32 pointers.
    const HRESULT result = services.InvokeCommand(offset, isSystemMenu, _currentFolderPrefix);
    if (result == S_OK) {
        KillSelection();
        return true;
    } else
        services.ShowInvokeError(result);
    return false;
}

void CPanel::CreateSevenZipMenu(NQtShellMenu::CMenuHandle *menuSpec, bool showExtendedVerbs,
                                const CRecordVector<UInt32> &operatedIndices, int firstDirIndex,
                                std::unique_ptr<CZipContextMenu> &sevenZipContextMenu,
                                const QString &settingsPath, IQtContextMenuServices &services) {
    sevenZipContextMenu.reset();
    NQtShellMenu::CMenu menu;
    menu.Attach(menuSpec);
    auto contextMenu = std::make_unique<CZipContextMenu>(settingsPath, &services);
    CZipContextMenu *contextMenuSpec = contextMenu.get();
    GetFilePaths(operatedIndices, contextMenuSpec->_fileNames);
    contextMenuSpec->Init_For_7zFM();
    contextMenuSpec->_attribs.FirstDirIndex = firstDirIndex;
    // Original CMF_EXPLORE / Z7_WIN_CMF_EXTENDEDVERBS, transported as values.
    unsigned flags = 4;
    if (showExtendedVerbs)
        flags |= 0x00000100;
    const HRESULT res =
        contextMenu->QueryContextMenu(menu, 0, kSevenZipStartMenuID, kSystemStartMenuID - 1, flags);
    const bool sevenZipMenuCreated = SUCCEEDED(res);
    if (sevenZipMenuCreated)
        sevenZipContextMenu = std::move(contextMenu);
}

void CPanel::CreateFileMenu(IQtFileMenuServices &menu, bool programMenu,
                            IQtPanelPluginMenuServices *plugins) {
    if (plugins)
        plugins->ReleasePluginMenus();
    const bool showExtendedVerbs = plugins && plugins->IsShiftDown();
    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);
    const int firstDirIndex = FindDir_InOperatedList(operatedIndices);
    if (!IsArcFolder() && plugins) {
        plugins->CreateSevenZipMenu(*this, showExtendedVerbs, operatedIndices, firstDirIndex);
        plugins->CreateSystemMenu(*this, showExtendedVerbs, operatedIndices);
    }
    CFileMenu fm;
    fm.readOnly = IsThereReadOnlyFolder();
    fm.isHashFolder = IsHashFolder();
    fm.programMenu = programMenu;
    // Qt stores/renders plugin rows separately from CFileMenu's projection;
    // its zero position is the original menu.GetItemCount() insertion point.
    fm.Load(menu, 0);
}

bool CPanel::OnContextMenu(QWidget *windowHandle, int xPos, int yPos,
                           IQtPanelContextMenuServices &services) {
    if (services.IsHeader(windowHandle)) {
        services.ShowColumnsContextMenu(xPos, yPos);
        return true;
    }
    if (!services.IsList(windowHandle))
        return false;

    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);

    // Negative coordinates can name real monitors; only (-1, -1) is keyboard.
    if (xPos == -1 && yPos == -1) {
        if (operatedIndices.Size() == 0) {
            xPos = 0;
            yPos = 0;
        } else {
            const int itemIndex = _selectionList.GetFocusedItem();
            if (itemIndex == -1)
                return false;
            QRect rect;
            if (!services.GetItemRect(itemIndex, rect))
                return false;
            xPos = rect.center().x();
            yPos = rect.center().y();
        }
        services.ClientToScreen(xPos, yPos);
    }

    services.CreatePopup();
    CreateFileMenu(services.Menu(), false, &services.Plugins());
    const unsigned id = services.Track(xPos, yPos);
    if (id == 0)
        return true;
    if (id >= kMenuCmdID_Plugin_Start) {
        services.InvokePluginCommand(id);
        return true;
    }
    if (services.ExecuteFileCommand(id))
        return true;
    return true;
}

static bool IsReadOnlyFolder(IFolderFolder *folder) {
    if (!folder)
        return false;
    bool res = false;
    {
        NWindows::NCOM::CPropVariant prop;
        if (folder->GetFolderProperty(kpidReadOnly, &prop) == S_OK)
            if (prop.vt == VT_BOOL)
                res = VARIANT_BOOLToBool(prop.boolVal);
    }
    return res;
}

bool CPanel::IsThereReadOnlyFolder() const {
    if (_archiveContextOutdated)
        return true;
    if (!_folderOperations)
        return true;
    if (IsReadOnlyFolder(_folder))
        return true;
    FOR_VECTOR(i, _parentFolders)
    if (IsReadOnlyFolder(_parentFolders[i].ParentFolder))
        return true;
    return false;
}

bool CPanel::CheckBeforeUpdate(UINT resourceID) {
    if (!_folderOperations) {
        MessageBox_Error_UnsupportOperation();
        return false;
    }
    for (int i = (int)_parentFolders.Size(); i >= 0; i--) {
        IFolderFolder *folder;
        if (i == (int)_parentFolders.Size())
            folder = _folder;
        else
            folder = _parentFolders[i].ParentFolder;
        if (!IsReadOnlyFolder(folder))
            continue;
        UString s;
        AddLangString(s, resourceID);
        s.Add_LF();
        AddLangString(s, IDS_OPERATION_IS_NOT_SUPPORTED);
        s.Add_LF();
        if (i == 0)
            s += GetFolderPath(folder);
        else
            s += _parentFolders[i - 1].VirtualPath;
        s.Add_LF();
        AddLangString(s, IDS_PROP_READ_ONLY);
        MessageBox_Error(s);
        return false;
    }
    return true;
}
