// Adapted from the FileFolderPluginOpen/ReOpen worker-modal lifecycle,
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f, Igor Pavlov, LGPL-2.1-or-later.
// See third_party/7zip/License.txt. Qt owns the native GetStream worker boundary.
// The original handler's factory may read its parent while building mappings.
#include "archive/resources/qtarchiveitemopen.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Common/ComTry.h"
#include "Windows/Thread.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadstream.h"
#include "filemanager/OpenCallback.h"
#include "localization/LangUtils.h"

namespace {
struct CItemOpen {
    CMyComPtr<IInArchiveGetStream> Source;
    UInt32 Index;
    COpenArchiveCallback UI;
    CMyComPtr<ISequentialInStream> Stream;
    HRESULT Result = E_FAIL;
    static THREAD_FUNC_DECL Run(void *parameter) {
        auto &request = *static_cast<CItemOpen *>(parameter);
        CProgressCloser closer(request.UI.ProgressDialog);
        try {
            CMyComPtr<IProgress> progress = CreateArchiveOpenReadRequest(&request.UI);
            CMyComPtr<IArchiveGetStreamWithRequest> factory;
            request.Source.QueryInterface(IID_IArchiveGetStreamWithRequest, &factory);
            request.Result =
                factory ? factory->GetStreamWithRequest(request.Index, &request.Stream, progress)
                        : request.Source->GetStream(request.Index, &request.Stream);
        } catch (...) {
            request.Result = E_FAIL;
        }
        return 0;
    }
};
} // namespace

HRESULT GetArchiveItemStream(IFolderFolder *folder, UInt32 index, ISequentialInStream **stream,
                             QWidget *parent, const QString &settingsPath, bool &passwordDefined,
                             UString &password) {
    COM_TRY_BEGIN
    if (!folder || !stream)
        return E_INVALIDARG;
    *stream = nullptr;
    CItemOpen request;
    folder->QueryInterface(IID_IInArchiveGetStream, (void **)&request.Source);
    if (!request.Source)
        return S_OK;
    request.Index = index;
    request.UI.SettingsPath = settingsPath;
    request.UI.ParentWindow = parent;
    request.UI.PasswordIsDefined = passwordDefined;
    request.UI.Password = password;
    request.UI.ProgressDialog.MainWindow = parent;
    request.UI.ProgressDialog.MainTitle = L"7-Zip";
    request.UI.ProgressDialog.WaitMode = true;
    NWindows::CThread thread;
    const WRes started = thread.Create(CItemOpen::Run, &request);
    if (started != 0)
        return HRESULT_FROM_WIN32(started);
    request.UI.StartProgressDialog(LangString(IDS_OPENNING), thread);
    passwordDefined = request.UI.PasswordIsDefined;
    WipeNativePassword(password);
    password = request.UI.Password;
    if (request.Result == S_OK)
        *stream = request.Stream.Detach();
    return request.Result;
    COM_TRY_END
}
