#include "filemanager/window/mainwindow.h"

CRecordVector<UInt32> MainWindow::sourceOperatedIndices() {
    CRecordVector<UInt32> indices;
    CPanel &panel = app_.GetFocusedPanel();
    if (!panel.GetFolder())
        return indices;
    panel.Get_ItemIndices_Operated(indices);
    return indices;
}

QStringList MainWindow::selectedArchivePaths() {
    return archivePathsForIndices(sourceOperatedIndices());
}

QStringList MainWindow::archivePathsForIndices(const CRecordVector<UInt32> &indices) {
    QStringList paths;
    if (projectedUi().state != State::Viewing)
        return paths;
    CPanel &panel = app_.GetFocusedPanel();
    if (!panel.GetFolder())
        return paths;
    auto *folder = static_cast<CAgentFolder *>(panel.GetFolder());
    CUIntVector realIndices;
    // Capture full logical paths, including descendants of selected directories.
    folder->GetRealIndices(indices.ConstData(), indices.Size(), false, true, realIndices);
    FOR_VECTOR(i, realIndices) {
        UString path;
        if (folder->GetArchiveItemPath(realIndices[i], path) != S_OK || path.IsEmpty())
            return {};
        paths << ToQString(path);
    }
    paths.removeDuplicates();
    return paths;
}
