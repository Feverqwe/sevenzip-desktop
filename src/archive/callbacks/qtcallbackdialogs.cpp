#include "archive/callbacks/qtcallbackdialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMetaObject>
#include <QThread>

HRESULT RunCallbackDialog(CProgressDialog &parent, const std::function<HRESULT()> &body) {
    if (QThread::currentThread() == parent.thread())
        return body();
    parent.WaitCreating();
    // Widgets stay on the GUI thread; the source operation joins its worker
    // before destroying this progress parent.
    HRESULT result = E_FAIL;
    QMetaObject::invokeMethod(&parent, [&] { result = body(); }, Qt::BlockingQueuedConnection);
    return result;
}

HRESULT RunCallbackQuestion(CProgressDialog &parent, const std::function<HRESULT()> &body) {
    if (parent.Sync.Get_Stopped())
        return E_ABORT;
    return RunCallbackDialog(parent, [&] { return parent.Sync.Get_Stopped() ? E_ABORT : body(); });
}
