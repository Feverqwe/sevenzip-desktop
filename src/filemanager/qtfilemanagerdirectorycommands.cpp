#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivedirectoryread.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include <QAction>
#include <QMenu>
#include <QScopedValueRollback>
#include <QStyle>

void QtFileManagerCommands::AttachDirectoryActions() {
    auto *menu = window_.findChild<QMenu *>("fileMenu");
    auto *action = menu->addAction(QString());
    action->setObjectName("calculateDirectorySizeAction");
    QtBindLocalText(action, "ArchiveDirectory", "Calculate folder size");
    action->setIcon(window_.style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    Bind(action, CalculateDirectorySize);
}

bool QtFileManagerCommands::CanCalculateDirectorySize() const {
    auto &panel = window_.app_.GetFocusedPanel();
    auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    if (!folder)
        return false;
    CUIntVector indices;
    panel.Get_ItemIndices_Operated(indices);
    FOR_VECTOR(i, indices)
    if (panel.IsItem_Folder(indices[i])) {
        const int nativeIndex = folder->GetRealIndex(indices[i]);
        if (nativeIndex >= 0 && !folder->DirectoryItemSubtreeComplete(UInt32(nativeIndex)))
            return true;
    }
    return false;
}

void QtFileManagerCommands::CalculateDirectorySizes() {
    const unsigned source = window_.app_.GetFocusedPanelIndex();
    auto &panel = window_.app_.GetFocusedPanel();
    CMyComPtr<IFolderFolder> owner = panel.GetFolder();
    auto *folder = dynamic_cast<CAgentFolder *>(owner.Interface());
    if (!folder)
        return;
    CUIntVector indices;
    panel.Get_ItemIndices_Operated(indices);
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    HRESULT result;
    {
        QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
        CPanel::CDisableNotify notify(panel);
        result = CalculateArchiveDirectorySizes(*folder, indices, &window_,
                                                MainWindow::toolbarSettingsPath());
    }
    // A cancelled recursive read can still have completed individual folders.
    // Refresh the projection from the retained reader, preserving selection.
    window_.withPanelProjection(source, [&] {
        if (window_.renderBoundFolder(false) == S_OK)
            window_.restoreListSelection(selection);
        if (result != S_OK && result != E_ABORT)
            panel.MessageBox_Error_HRESULT(result);
        window_.refreshActions();
    });
}
