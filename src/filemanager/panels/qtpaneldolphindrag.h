#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <memory>

class CDataObject;
class QEventLoop;
class QtPanelDragMime;

class QtPanelDolphinDrop final : public QDBusVirtualObject {
  public:
    QtPanelDolphinDrop();
    ~QtPanelDolphinDrop() override;

    std::unique_ptr<QtPanelDragMime> CreateMime(CDataObject &data);
    QString DestinationAfterDrop();

  private:
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

    QDBusConnection bus_;
    QString path_;
    QString destination_;
    QEventLoop *wait_ = nullptr;
    bool requested_ = false;
};
