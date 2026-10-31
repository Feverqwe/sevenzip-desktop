#include "Common/StringConvert.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"

CRecordVector<UInt32> MainWindow::sourceOperatedIndices() {
    CRecordVector<UInt32> indices;
    CPanel &panel = app_.GetFocusedPanel();
    if (!panel.GetFolder())
        return indices;
    panel.Get_ItemIndices_Operated(indices);
    return indices;
}

QStringList MainWindow::selectedArchiveItemsPaths() {
    QStringList paths;
    if (state_ != State::Viewing)
        return paths;
    const auto indices = sourceOperatedIndices();
    auto *folder = static_cast<CAgentFolder *>(app_.GetFocusedPanel().GetFolder());
    FOR_VECTOR(i, indices)
    paths << ToQString(folder->GetFullPrefix(indices[i]) + folder->GetName(indices[i]));
    return paths;
}

QStringList MainWindow::selectedFilesystemPaths() {
    QStringList paths;
    CPanel &panel = app_.GetFocusedPanel();
    if (!panel.GetFolder() || (state_ != State::Filesystem && !panel.IsAltStreamsFolder()))
        return paths;
    const auto indices = sourceOperatedIndices();
    panel.LoadFullPath();
    UStringVector filePaths;
    panel.GetFilePaths(indices, filePaths);
    FOR_VECTOR(i, filePaths)
    paths << QtFilesystemPath(us2fs(filePaths[i]));
    return paths;
}

QStringList MainWindow::selectedArchivePaths() {
    return archivePathsForIndices(sourceOperatedIndices());
}

QStringList MainWindow::archivePathsForIndices(const CRecordVector<UInt32> &indices) {
    QStringList paths;
    if (state_ != State::Viewing)
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
