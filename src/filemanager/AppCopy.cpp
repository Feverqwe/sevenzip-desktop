// Copy/Move workflow extracted from 7-Zip App.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native routing/refresh phases retain their existing Qt transport adaptations.
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "archive/agent/Agent.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/PanelInfo.h"
#include "fileops/native/FileDir.h"
#include "fileops/patterns/Wildcard.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QCoreApplication>
#include <QDir>
#include <QScopedValueRollback>

struct CApp::CopyRequest {
    CopyRequest(unsigned source, unsigned destination, bool move, bool copyToSame)
        : Source(source), Destination(destination), Move(move), CopyToSame(copyToSame) {}
    const unsigned Source;
    const unsigned Destination;
    const bool Move;
    const bool CopyToSame;
    CSelectedState SourceSelection;
    CSelectedState DestinationSelection;
};

// Reduces path to the part that exists on disk (or the root prefix).
// The source helper normalizes the result with WCHAR_PATH_SEPARATOR.
static void Reduce_Path_To_RealFileSystemPath(UString &path) {
    unsigned prefixSize = NWindows::NFile::NName::GetRootPrefixSize(path);
    while (!path.IsEmpty()) {
        if (NWindows::NFile::NFind::DoesDirExist_FollowLink(us2fs(path))) {
            NWindows::NFile::NName::NormalizeDirPathPrefix(path);
            break;
        }
        int pos = path.ReverseFind_PathSepar();
        if (pos < 0) {
            path.Empty();
            break;
        }
        path.DeleteFrom((unsigned)(pos + 1));
        if ((unsigned)pos + 1 == prefixSize)
            break;
        path.DeleteFrom((unsigned)pos);
    }
}

void CApp::OnCopy(bool move, bool copyToSame, unsigned srcPanelIndex) {
    if (!_presentation || srcPanelIndex >= kNumPanelsMax)
        return;
    const unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
    CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[destPanelIndex];
    CPanel::CDisableTimerProcessing2 disableTimerProcessing1(&destPanel);
    CPanel::CDisableTimerProcessing2 disableTimerProcessing2(&srcPanel);

    if (move) {
        if (!srcPanel.CheckBeforeUpdate(IDS_MOVE))
            return;
    } else if (!srcPanel.DoesItSupportOperations()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }

    CRecordVector<UInt32> indices;
    UString destPath;
    bool useDestPanel = false;
    {
        if (copyToSame) {
            const int focusedItem = srcPanel._selectionList.GetFocusedItem();
            if (focusedItem < 0)
                return;
            const unsigned realIndex = srcPanel.GetRealItemIndex(focusedItem);
            if (realIndex == UInt32(-1))
                return;
            indices.Add(realIndex);
            destPath = srcPanel.GetItemName(realIndex);
        } else {
            srcPanel.Get_ItemIndices_OperSmart(indices);
            if (indices.IsEmpty())
                return;
            destPath = destPanel.GetFsPath();
            if (NumPanels == 1)
                Reduce_Path_To_RealFileSystemPath(destPath);
        }
    }

    QStringList sourcePaths;
    FOR_VECTOR(i, indices)
    sourcePaths << ToQString(srcPanel.GetItemFullPath(indices[i]));
    const bool renameSource = srcPanel.IsFSFolder() || srcPanel.IsAltStreamsFolder();
    const bool peerWritable = NumPanels == 2 && destPanel.DoesItSupportOperations() &&
                              !destPanel.IsThereReadOnlyFolder() && !destPanel.IsRootFolder() &&
                              !destPanel.IsFSDrivesFolder();
    if ((copyToSame && renameSource) || (!copyToSame && NumPanels == 2 && !peerWritable)) {
        destPath = srcPanel.GetFsPath();
        if (!copyToSame)
            Reduce_Path_To_RealFileSystemPath(destPath);
    }
    const auto totals = srcPanel.GetItemTotals(indices);
    CCopyDialog copyDialog(move, ToQString(destPath), sourcePaths, _window,
                           srcPanel.GetItemsInfoString(indices, &totals));
    const UInt64 unknown = (UInt64)(Int64)-1;
    copyDialog.SetSourceDetails(
        ToQString(srcPanel.GetFsPath()),
        totals.filesSize == unknown || totals.foldersSize == unknown
            ? std::nullopt
            : std::optional<quint64>(totals.filesSize + totals.foldersSize));
    QtCopyDestinationContext destinationContext;
    destinationContext.SourceDirectory = ToQString(srcPanel.GetFsPath());
    destinationContext.RelativeBase = ToQString(srcPanel.GetFsPath());
    if (indices.Size() == 1)
        destinationContext.SourceName = ToQString(srcPanel.GetItemName(indices[0]));
    if (NumPanels == 2) {
        destinationContext.PeerDestination = ToQString(destPanel.GetFsPath());
        destinationContext.PeerWritable = peerWritable;
    }
    const UString peerPath = NumPanels == 2 ? destPanel.GetFsPath() : UString();
    const bool peerIsFilesystem = NumPanels == 2 && destPanel.IsFSFolder();
    destinationContext.CanRename = [renameSource, peerPath, peerIsFilesystem](const QString &path) {
        const UString native = ToUString(path);
        bool alternate = false;
#ifdef _WIN32
        alternate = NWindows::NFile::NName::IsAltPathPrefix(us2fs(native));
#endif
        return renameSource &&
               (peerPath.IsEmpty() || peerIsFilesystem ||
                CompareFileNames(native, peerPath) != 0) &&
               !alternate;
    };
    copyDialog.ConfigureDestination(std::move(destinationContext));
    if (copyDialog.exec() != QDialog::Accepted)
        return;
    destPath = copyDialog.Value;
    if (destPath.IsEmpty()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    UString correctName;
    if (!srcPanel.CorrectFsPath(destPath, correctName)) {
        srcPanel.MessageBox_Error_HRESULT(E_INVALIDARG);
        return;
    }
    if (NWindows::NFile::NName::IsAbsolutePath(destPath))
        destPath.Empty();
    else
        destPath = srcPanel.GetFsPath();
    destPath += correctName;

#if defined(_WIN32) && !defined(UNDER_CE)
    if (destPath.Len() != 0 && destPath[0] == '\\')
        if (destPath.Len() == 1 || destPath[1] != '\\') {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        }
#endif

    bool possibleToUseDestPanel = false;
    if (CompareFileNames(destPath, destPanel.GetFsPath()) == 0) {
        if (NumPanels == 1 || CompareFileNames(destPath, srcPanel.GetFsPath()) == 0) {
            srcPanel.MessageBox_Error(QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot copy files onto itself")));
            return;
        }
        if (destPanel.DoesItSupportOperations())
            possibleToUseDestPanel = true;
    }

    bool destIsFsPath = false;
    if (possibleToUseDestPanel) {
        if (destPanel.IsFSFolder() || destPanel.IsAltStreamsFolder())
            destIsFsPath = true;
        else if (destPanel.IsFSDrivesFolder() || destPanel.IsRootFolder()) {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        }
    } else {
#ifdef _WIN32
        // Alternate-stream destinations are allowed only through a peer ADS folder.
        if (NWindows::NFile::NName::IsAltPathPrefix(us2fs(destPath))) {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
        } else
#endif
        {
            if (indices.Size() == 1 && !destPath.IsEmpty() && !IsPathSepar(destPath.Back())) {
                const int pos = destPath.ReverseFind_PathSepar();
                if (pos < 0) {
                    srcPanel.MessageBox_Error_UnsupportOperation();
                    return;
                }
                UString prefix = destPath.Left(pos + 1);
                if (!NWindows::NFile::NDir::CreateComplexDir(us2fs(prefix))) {
                    const HRESULT lastError = GetLastError_noZero_HRESULT();
                    srcPanel.MessageBox_Error_2Lines_Message_HRESULT(prefix, lastError);
                    return;
                }
            } else {
                NWindows::NFile::NName::NormalizeDirPathPrefix(destPath);
                if (!NWindows::NFile::NDir::CreateComplexDir(us2fs(destPath))) {
                    const HRESULT lastError = GetLastError_noZero_HRESULT();
                    srcPanel.MessageBox_Error_2Lines_Message_HRESULT(destPath, lastError);
                    return;
                }
            }
            destIsFsPath = true;
        }
    }
    if (!destIsFsPath)
        useDestPanel = true;
    if (copyDialog.usesDestinationName() && useDestPanel) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }

    // Qt filesystem execution consumes portable slashes; classification above
    // retains the source path and provider rules before this string boundary.
    QString output = QDir::cleanPath(ToQString(destPath));
    if (!destPath.IsEmpty() && IsPathSepar(destPath.Back()) && !output.endsWith('/'))
        output += '/';

    const bool useSrcPanel = !useDestPanel || !srcPanel.Is_IO_FS_Folder();
    const bool useTemp = useSrcPanel && useDestPanel;
    if (useTemp && NumPanels == 1) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    auto request = std::make_shared<CopyRequest>(srcPanelIndex, destPanelIndex, move, copyToSame);
    srcPanel.SaveSelectedState(request->SourceSelection);
    destPanel.SaveSelectedState(request->DestinationSelection);
    // Keep the original RefreshListCtrl caller and its saved state across the
    // asynchronous presentation boundary. OnCopy ignores reload HRESULTs.
    _copyRequest = std::move(request);
    // Qt execution can return while the operation owner retains these guards.
    // Unwind the source stack in reverse order before that owner snapshots it.
    disableTimerProcessing2.Restore();
    disableTimerProcessing1.Restore();
    const auto route = copyDialog.usesDestinationName() ? QtCopyRoute::DirectFileSystem
                       : !useDestPanel                  ? QtCopyRoute::FileSystem
                       : useTemp                        ? QtCopyRoute::StagedDestinationPanel
                                                        : QtCopyRoute::DestinationPanel;
    _presentation->ExecuteCopy(move, copyToSame, srcPanelIndex, output, indices, route);
}

void CApp::CompleteCopy() {
    auto request = std::move(_copyRequest);
    if (!request || _qtOperations.closed())
        return;
    // The original post-operation body runs after Qt publishes both panels.
    CPanel &srcPanel = Panels[request->Source];
    CPanel &destPanel = Panels[request->Destination];
    CPanel::CDisableNotify disableNotify1(destPanel);
    CPanel::CDisableNotify disableNotify2(srcPanel);
    RefreshTitleAlways();
    if (request->CopyToSame || request->Move)
        srcPanel.RestoreLoadedSelection(request->SourceSelection);
    if (!request->CopyToSame) {
        destPanel.RestoreLoadedSelection(request->DestinationSelection);
        srcPanel.KillSelection();
    }
    disableNotify1.Restore();
    disableNotify2.Restore();
    srcPanel.SetFocusToList();
}

bool CApp::RefreshCopyPanel(unsigned index) {
    const auto request = _copyRequest;
    if (!request || _qtOperations.closed() || index >= kNumPanelsMax)
        return false;
    // Synchronous publication can re-enter completion and release the owner.
    if (index == request->Source)
        Panels[index].RefreshListCtrl(request->SourceSelection);
    else if (index == request->Destination)
        Panels[index].RefreshListCtrl(request->DestinationSelection);
    return true;
}

QtArchiveTransferResult CApp::CopyArchiveToArchive(unsigned srcPanelIndex,
                                                   const CRecordVector<UInt32> &indices,
                                                   UStringVector *messages) {
    if (NumPanels != 2 || srcPanelIndex >= kNumPanelsMax || indices.IsEmpty())
        return {E_INVALIDARG, {}};
    const unsigned destPanelIndex = 1 - srcPanelIndex;
    CPanel &srcPanel = Panels[srcPanelIndex];
    CPanel &destPanel = Panels[destPanelIndex];
    CPanel::CDisableTimerProcessing disableTimerProcessing1(destPanel);
    CPanel::CDisableTimerProcessing disableTimerProcessing2(srcPanel);

    NWindows::NFile::NDir::CTempDir tempDirectory;
    if (!tempDirectory.Create(FTEXT("7zO")))
        return {E_FAIL, {}};
    FString tempDirPrefix = tempDirectory.GetPath();
    NWindows::NFile::NName::NormalizeDirPathPrefix(tempDirPrefix);

    CCopyToOptions options;
    options.folder = fs2us(tempDirPrefix);
    // CAgentFolder::CopyTo returns E_NOTIMPL for move. The caller deletes the
    // source only after the destination transaction commits.
    options.moveMode = false;
    options.includeAltStreams = true;
    options.replaceAltStreamChars = false;
    options.showErrorMessages = true;
    QtArchiveExtractReport report;
    report.Purpose = QtArchiveExtractPurpose::ArchiveStaging;
    HRESULT result = srcPanel.CopyTo(options, indices, messages, &report);
    if (result == S_OK && report.HadErrors)
        result = E_FAIL;

    if (result == S_OK && (!messages || messages->IsEmpty())) {
        if (!report.Status || !report.Status->Complete || !report.Status->StagingCompatible)
            return {E_FAIL, QCoreApplication::translate(
                                "archive_transfer",
                                "Some selected items cannot be transferred without data or path "
                                "changes. Both archives were left unchanged.")};
        UStringVector filePaths;
        filePaths.ClearAndReserve(indices.Size());
        FOR_VECTOR(i, indices)
        filePaths.AddInReserved(srcPanel.GetItemName_for_Copy(indices[i]));
        auto *destination = dynamic_cast<CAgentFolder *>(destPanel.GetFolder());
        CMyComPtr<IUnknown> resourceOwner =
            destination ? destination->GetEngineResource() : nullptr;
        auto *resource = dynamic_cast<QtArchiveFileResource *>(resourceOwner.Interface());
        const auto reader = resource ? resource->NativeReaderLease() : nullptr;
        const auto *format = reader ? reader->formatInfo() : nullptr;
        const auto &inputs = report.Status->Inputs;
        const bool hasMetadata =
            inputs.SymLinks || inputs.HardLinks || !inputs.AltStreamPaths.isEmpty();
        if (hasMetadata && (!format || (inputs.SymLinks && !format->Flags_SymLinks()) ||
                            (inputs.HardLinks && !format->Flags_HardLinks()) ||
                            (!inputs.AltStreamPaths.isEmpty() && !format->Flags_AltStreams())))
            return {E_NOTIMPL, QCoreApplication::translate(
                                   "archive_transfer",
                                   "The destination format cannot preserve the selected links or "
                                   "alternate streams. Both archives were left unchanged.")};
        std::optional<QScopedValueRollback<std::optional<QtArchiveTransferInputs>>> transferInputs;
        if (resource)
            transferInputs.emplace(resource->TransferInputs(), inputs);
        result = destPanel.CopyFrom(false, fs2us(tempDirPrefix), filePaths, false, messages);
    }
    return {result, {}};
}
