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
    std::function<bool()> prepareUrls_;
    bool canceled_ = false;

  protected:
    QVariant retrieveData(const QString &format, QMetaType type) const override;

  public:
    explicit QtPanelDragMime(CDataObject &data, std::function<bool()> prepareUrls = {});
    CDataObject &Data() const { return data_; }
    void ConfirmRecipientFinished() { data_.FinishExternalReceiver(); }
    void Cancel() { canceled_ = true; }
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
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
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
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    PromiseMaterializer promiseMaterializer_;
#endif
    bool active_ = false;
};
