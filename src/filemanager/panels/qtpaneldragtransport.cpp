#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "filemanager/panels/qtpanelfolder.h"
#include <QDrag>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QUrl>
#include <QWidget>
#include <new>

QtPanelDragMime::QtPanelDragMime(CDataObject &data) : data_(data) {
    QList<QUrl> urls;
    FOR_VECTOR(i, data.Names) urls.append(QUrl::fromLocalFile(ToQString(data.Names[i])));
    setUrls(urls);
}
QtPanelDragTransport::QtPanelDragTransport(QWidget *source, Executor executor,
                                           Compression compression)
    : widget_(source), nativeExecutor_(!executor), executor_(std::move(executor)),
      compression_(std::move(compression)) {
    if (!executor_)
        executor_ = [](QDrag &drag, Qt::DropActions actions) {
            return drag.exec(actions, Qt::CopyAction);
        };
}
HRESULT QtPanelDragTransport::DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                                         DWORD &effect) try {
    effect = DROPEFFECT_NONE;
    if (!widget_ || active_)
        return E_ABORT;
    QScopedValueRollback<bool> active(active_, true);
#ifdef _WIN32
    if (nativeExecutor_ && QGuiApplication::platformName() == QStringLiteral("windows"))
        return DoNativePanelDrag(data, source, allowed, effect);
#endif
    // Qt has no cross-platform release-before-drop hook equivalent to OLE
    // QueryContinueDrag. Prepare real bytes before exec so native receivers
    // never observe promised paths without their contents. Copy remains native.
    if (data.IsTempFiles) {
        data.CopyFromPanelTo_Folder();
        if (data.Copy_HRESULT != S_OK || !data.Messages.IsEmpty())
            return DRAGDROP_S_CANCEL;
        data.UsePreGlobal = false;
    }
    if (!data.BeginExternalReceiver()) {
        data.Copy_HRESULT = E_FAIL;
        return DRAGDROP_S_CANCEL;
    }
    QDrag drag(widget_);
    drag.setMimeData(new QtPanelDragMime(data));
    Qt::DropActions actions = Qt::IgnoreAction;
    if (allowed & DROPEFFECT_COPY)
        actions |= Qt::CopyAction;
    if (allowed & DROPEFFECT_MOVE)
        actions |= Qt::MoveAction;
    const Qt::DropAction action = executor_(drag, actions);
    if (action == Qt::CopyAction)
        effect = DROPEFFECT_COPY;
    else if (action == Qt::MoveAction)
        effect = DROPEFFECT_MOVE;
    source.GiveFeedback(effect);
    // OLE resets Copy_HRESULT before Drop. Qt exec returns after the target ran;
    // retain its typed result across the source release transition.
    const HRESULT targetResult = data.Copy_HRESULT;
    const HRESULT result = source.QueryContinueDrag(action == Qt::IgnoreAction, 0);
    if (targetResult != S_OK)
        data.Copy_HRESULT = targetResult;
    bool cancellationConfirmed = result == DRAGDROP_S_CANCEL;
#ifdef Q_OS_MACOS
    // Cocoa can return IgnoreAction for an external drop while Finder still
    // consumes the URLs. Native exec alone cannot confirm cancellation; retain
    // exposed archive bytes unless a typed recipient confirms completion.
    if (nativeExecutor_ && data.IsTempFiles)
        cancellationConfirmed = false;
#endif
    if (cancellationConfirmed ||
        (data.m_Transfer_WasSet && data.m_Transfer.Target.FuncType == k_DragTargetMode_Drop_End &&
         (data.m_Transfer.Target.Flags & (k_TargetFlags_WasCanceled | k_TargetFlags_WasProcessed |
                                          k_TargetFlags_MustBeProcessedBySource))))
        data.FinishExternalReceiver();
    return result;
} catch (const std::bad_alloc &) {
    // Rejoin OnDrag's normal DragEnd/error path. A receiver may already have
    // consumed URLs, so its unconfirmed temporary lease must stay protected.
    effect = DROPEFFECT_NONE;
    data.Copy_HRESULT = E_OUTOFMEMORY;
    return DRAGDROP_S_CANCEL;
} catch (...) {
    effect = DROPEFFECT_NONE;
    data.Copy_HRESULT = E_FAIL;
    return DRAGDROP_S_CANCEL;
}
HRESULT QtPanelDragTransport::CompressFiles(CPanel &panel, const UString &folder,
                                            const UString &name, const UStringVector &paths,
                                            bool wait) {
    return compression_ ? compression_(panel, folder, name, paths, wait) : E_NOTIMPL;
}
