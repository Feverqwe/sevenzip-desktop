#include "archive/resources/qtarchivedirectoryread.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "archive/agent/Agent.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/OpenCallback.h"
#include "localization/LangUtils.h"
#include <QCoreApplication>

namespace {
HRESULT RunDirectoryRead(CAgentFolder &folder, bool recursive, const CUIntVector *selection,
                         QWidget *parent, const QString &settingsPath,
                         bool disableQuestions = false) {
    // Reuse the joined native opening lifecycle. The folder and source proxy
    // remain quiescent until this modal worker has finished, including Cancel.
    struct Request {
        CAgentFolder &Folder;
        bool Recursive;
        const CUIntVector *Selection;
        COpenArchiveCallback UI;
        HRESULT Result = E_FAIL;
        Request(CAgentFolder &folder, bool recursive, const CUIntVector *selection)
            : Folder(folder), Recursive(recursive), Selection(selection) {}
        static THREAD_FUNC_DECL Run(void *opaque) {
            auto &request = *static_cast<Request *>(opaque);
            CProgressCloser closer(request.UI.ProgressDialog);
            try {
                request.UI.ProgressDialog.WaitCreating();
                auto progress = CreateArchiveOpenReadRequest(&request.UI);
                request.Result =
                    request.Selection
                        ? request.Folder.LoadDirectorySelection(request.Selection->ConstData(),
                                                                request.Selection->Size(), progress)
                        : request.Folder.LoadDirectoryItems(request.Recursive, progress);
            } catch (...) {
                request.Result = E_FAIL;
            }
            return 0;
        }
    } request{folder, recursive, selection};
    request.UI.SettingsPath = settingsPath;
    request.UI.ProgressDialog.DisableUserQuestions |= disableQuestions;
    request.UI.ParentWindow = parent;
    request.UI.ProgressDialog.MainWindow = parent;
    request.UI.ProgressDialog.MainTitle = L"7-Zip";
    request.UI.ProgressDialog.WaitMode = true;
    request.UI.ProgressDialog.CancellationMayLeaveChanges = false;
    auto *resource = dynamic_cast<QtArchiveFileResource *>(folder.GetEngineResource());
    if (!resource)
        return E_FAIL;
    auto transport = resource->CreateProgressTransport(request.UI.ProgressDialog, true);
    request.UI.ProgressDialog.AttachTransport(transport.get());
    const UString title =
        selection
            ? UString(QCoreApplication::translate("ArchiveDirectory", "Calculating folder size…")
                          .toStdWString()
                          .c_str())
            : LangString(IDS_OPENNING);
    NWindows::CThread thread;
    const auto started = thread.Create(Request::Run, &request);
    if (started)
        return HRESULT_FROM_WIN32(started);
    request.UI.StartProgressDialog(title, thread);
    return request.Result;
}
} // namespace

HRESULT ReadArchiveDirectory(CAgentFolder &folder, bool recursive, QWidget *parent,
                             const QString &settingsPath, bool disableQuestions) {
    return folder.DirectoryItemsNeedLoad(recursive)
               ? RunDirectoryRead(folder, recursive, nullptr, parent, settingsPath,
                                  disableQuestions)
               : S_OK;
}

HRESULT CalculateArchiveDirectorySizes(CAgentFolder &folder, const CUIntVector &indices,
                                       QWidget *parent, const QString &settingsPath) {
    return indices.IsEmpty() ? S_OK
                             : RunDirectoryRead(folder, true, &indices, parent, settingsPath);
}
