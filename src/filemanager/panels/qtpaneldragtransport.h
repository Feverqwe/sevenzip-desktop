#pragma once
#include "filemanager/panels/qtpaneldragmodel.h"
#include <QMimeData>
#include <QPointer>
#include <functional>
class QDrag;
class QWidget;

// Typed in-process carrier; external receivers see ordinary local-file URLs.
// No serialized pointer or caller-supplied MIME marker establishes a source.
class QtPanelDragMime final : public QMimeData {
    std::shared_ptr<CDataObject> owner_;
    CDataObject &data_;

  public:
    explicit QtPanelDragMime(CDataObject &data);
    CDataObject &Data() const { return data_; }
    void ConfirmRecipientFinished() { data_.FinishExternalReceiver(); }
};

class QtPanelDragTransport final : public IQtPanelDragServices {
  public:
    using Executor = std::function<Qt::DropAction(QDrag &, Qt::DropActions)>;
    using Compression = std::function<HRESULT(CPanel &, const UString &, const UString &,
                                              const UStringVector &, bool)>;
    explicit QtPanelDragTransport(QWidget *source, Executor executor = {},
                                  Compression compression = {});
    HRESULT DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                       DWORD &effect) override;
    HRESULT CompressFiles(CPanel &, const UString &, const UString &, const UStringVector &,
                          bool) override;
    bool Active() const { return active_; }
#ifdef Q_OS_MACOS
    using PromiseMaterializer = std::function<bool(CDataObject &, CPanel &)>;
    void SetPromiseMaterializer(PromiseMaterializer materializer) {
        promiseMaterializer_ = std::move(materializer);
    }
#endif

  private:
    QPointer<QWidget> widget_;
    const bool nativeExecutor_;
    Executor executor_;
    Compression compression_;
#ifdef Q_OS_MACOS
    PromiseMaterializer promiseMaterializer_;
#endif
    bool active_ = false;
};
