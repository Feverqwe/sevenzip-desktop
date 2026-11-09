#include "filemanager/panels/qtpaneldolphindrag.h"
#include "filemanager/panels/qtpaneldragtransport.h"

#include <QDBusMessage>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

namespace {
constexpr auto ServiceMime = "application/x-kde-ark-dndextract-service";
constexpr auto PathMime = "application/x-kde-ark-dndextract-path";

class DolphinDragMime final : public QtPanelDragMime {
  public:
    DolphinDragMime(CDataObject &data, QtPanelDolphinDrop &drop, bool &requested)
        : QtPanelDragMime(data, {}, false), drop_(&drop), requested_(requested) {}

  private:
    QVariant retrieveData(const QString &format, QMetaType type) const override {
        if (drop_ && format == QLatin1String(PathMime))
            requested_ = true;
        return QtPanelDragMime::retrieveData(format, type);
    }

    QPointer<QtPanelDolphinDrop> drop_;
    bool &requested_;
};
} // namespace

QtPanelDolphinDrop::QtPanelDolphinDrop() : bus_(QDBusConnection::sessionBus()) {}

QtPanelDolphinDrop::~QtPanelDolphinDrop() {
    if (!path_.isEmpty())
        bus_.unregisterObject(path_);
}

std::unique_ptr<QtPanelDragMime> QtPanelDolphinDrop::CreateMime(CDataObject &data) {
    if (!bus_.isConnected())
        return {};
    const auto path =
        QStringLiteral("/org/sevenzip/Desktop/Drop/d") + QUuid::createUuid().toString(QUuid::Id128);
    if (!bus_.registerVirtualObject(path, this))
        return {};
    path_ = path;
    auto mime = std::make_unique<DolphinDragMime>(data, *this, requested_);
    mime->setData(QLatin1String(ServiceMime), bus_.baseService().toUtf8());
    mime->setData(QLatin1String(PathMime), path_.toUtf8());
    return mime;
}

QString QtPanelDolphinDrop::introspect(const QString &) const {
    return QStringLiteral(
        "<interface name=\"org.kde.ark.DndExtract\">"
        "<method name=\"extractSelectedFilesTo\"><arg type=\"s\" direction=\"in\"/></method>"
        "</interface>");
}

bool QtPanelDolphinDrop::handleMessage(const QDBusMessage &message,
                                       const QDBusConnection &connection) {
    if (message.interface() != QStringLiteral("org.kde.ark.DndExtract") ||
        message.member() != QStringLiteral("extractSelectedFilesTo"))
        return false;
    const auto arguments = message.arguments();
    const QString path = arguments.value(0).toString();
    if (!requested_ || !destination_.isEmpty() || message.signature() != QStringLiteral("s") ||
        path.contains(QChar::Null) || !QDir::isAbsolutePath(path) || !QFileInfo(path).isDir()) {
        connection.send(message.createErrorReply(QDBusError::InvalidArgs,
                                                 QStringLiteral("Invalid drop destination")));
        return true;
    }
    destination_ = path;
    connection.send(message.createReply());
    if (wait_)
        wait_->quit();
    return true;
}

QString QtPanelDolphinDrop::DestinationAfterDrop() {
    if (requested_ && destination_.isEmpty()) {
        QEventLoop loop;
        wait_ = &loop;
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        wait_ = nullptr;
    }
    return destination_;
}
