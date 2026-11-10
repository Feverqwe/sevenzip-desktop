#include "filemanager/window/qtworkspaceapplication.h"

#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QJsonArray>
#include <QJsonDocument>

void QtWorkspaceApplication::ArchivePublished(const QString &archive, const CPanel *updated) {
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed())
            window->ArchivePublished(archive, updated);
}

void QtWorkspaceApplication::FilesystemPublished(const QStringList &directories,
                                                 const MainWindow *exclude) {
    for (const auto &window : windows_)
        if (window && window != exclude && !window->app_.QtOperations().closed() &&
            window->guiOutputRefresh_)
            window->guiOutputRefresh_->Enqueue(directories, {});
}

void QtWorkspaceApplication::FileListSettingsChanged(const MainWindow &source) {
    for (const auto &window : windows_) {
        if (window && window != &source && !window->app_.QtOperations().closed())
            window->workspace_->WhenReady(
                [window] {
                    if (window)
                        window->applyFileListSettings();
                },
                {}, false, [window] { return window && !window->renameController_->Pending(); });
    }
}

QtWorkspaceApplication::QtWorkspaceApplication(QApplication &application) : QObject(&application) {
    setObjectName(QStringLiteral("sevenzipWorkspaceApplication"));
    connect(&application, &QCoreApplication::aboutToQuit, this, [this] { Save(); });
    InitializeTaskResults();
}

QtWorkspaceApplication *QtWorkspaceApplication::Find() {
    return dynamic_cast<QtWorkspaceApplication *>(
        qApp->findChild<QObject *>(QStringLiteral("sevenzipWorkspaceApplication")));
}

void QtWorkspaceApplication::Register(MainWindow &window) {
    for (const auto &existing : windows_)
        if (existing == &window)
            return;
    windows_.push_back(&window);
    // A newly requested file context resumes ordinary session persistence.
    quitting_ = false;
}

QJsonArray QtWorkspaceApplication::ReadSession() const {
    const QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    return QJsonDocument::fromJson(settings.value("workspace/session").toByteArray()).array();
}

void QtWorkspaceApplication::Save() {
    if (restoring_ || quitting_)
        return;
    QJsonArray session;
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed())
            session.append(window->workspace_->Save());
    // Last-window close has already saved its context before native teardown.
    if (session.isEmpty())
        return;
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue("workspace/session", QJsonDocument(session).toJson(QJsonDocument::Compact));
}

void QtWorkspaceApplication::Closing(MainWindow &window) {
    if (restoring_ || quitting_)
        return;
    QJsonArray session;
    for (const auto &other : windows_)
        if (other && other != &window && !other->app_.QtOperations().closed())
            session.append(other->workspace_->Save());
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue("workspace/session", QJsonDocument(session).toJson(QJsonDocument::Compact));
}

void QtWorkspaceApplication::ApplyWhenReady(MainWindow &window, const QJsonObject &state,
                                            const QtArchiveLocation &archive) {
    ++pendingRestores_;
    restoring_ = true;
    const auto completed = std::make_shared<bool>(false);
    const QPointer<QtWorkspaceApplication> owner(this);
    const auto finish = [owner, completed] {
        if (owner && !std::exchange(*completed, true))
            owner->restoring_ = --owner->pendingRestores_ != 0;
    };
    connect(&window, &QObject::destroyed, this, finish);
    auto *timer = new QTimer(&window);
    timer->setInterval(25);
    connect(timer, &QTimer::timeout, &window, [&window, state, timer, finish, archive] {
        if (window.app_.QtOperations().closed()) {
            finish();
            timer->deleteLater();
            return;
        }
        if (window.app_.QtOperations().active() || window.app_.QtOperations().callActive() ||
            window.backend_->busy())
            return;
        timer->stop();
        timer->deleteLater();
        window.workspace_->Restore(state, finish, finish, archive);
    });
    timer->start();
}

void QtWorkspaceApplication::NewWindowFrom(MainWindow &source) {
    const auto &ui = source.projectedUi();
    if (ui.scanningDisk) {
        const QJsonObject panel{{"path", ui.directory}};
        OpenWindow({{"panels", QJsonArray{panel}}, {"count", 1}}, {});
        return;
    }
    if (ui.scanningArchive) {
        auto *address = static_cast<QtPanelAddressCombo *>(ui.pathCombo);
        const auto &panel = source.app_.GetFocusedPanel();
        const auto archive = QtArchiveLocation::Capture(panel, address->LocationSegments());
        if (!archive.physicalPath.isEmpty()) {
            const QJsonObject location{{"path", ToQString(panel.GetCurrentFolderPrefix())}};
            OpenWindow({{"panels", QJsonArray{location}}, {"count", 1}}, archive);
            return;
        }
    }
    auto &workspace = *source.workspace_;
    const QString path = workspace.CurrentLocation();
    const auto &location = workspace.panels_[source.projectedPanel_].location;
    const QJsonObject panel{{"path", path}, {"nativePlace", location.nativePlaceIdentity}};
    OpenWindow({{"panels", QJsonArray{panel}}, {"count", 1}}, location.archiveLocation);
}

void QtWorkspaceApplication::OpenWindow(const QJsonObject &state, QtArchiveLocation archive) {
    QtFileManagerStartup startup;
    startup.FullPath = ToUString(QDir::homePath());
    std::unique_ptr<ArchiveBackend> backend;
    for (const auto &existing : windows_)
        if (existing && !existing->app_.QtOperations().closed()) {
            backend.reset(existing->backend_->createIndependentBackend());
            break;
        }
    if (!backend)
        backend = std::make_unique<ArchiveBackend>();
    auto *window = new MainWindow(backend.get(), {}, nullptr, &startup);
    backend->setParent(window);
    backend.release();
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (window->backend_->codecCatalog())
        ApplyWhenReady(*window, state, archive);
    else {
        connect(
            window->backend_, &ArchiveBackend::codecCatalogReady, this,
            [this, window, state, archive](bool, const QString &) {
                ApplyWhenReady(*window, state, archive);
            },
            Qt::SingleShotConnection);
        window->backend_->discoverCodecCatalog();
    }
    window->show();
}

void QtWorkspaceApplication::RestoreInitialGeometry(MainWindow &first) {
    const auto session = ReadSession();
    if (!session.isEmpty())
        first.workspace_->RestoreSavedGeometry(session.first().toObject());
}

void QtWorkspaceApplication::Restore(MainWindow &first) {
    const auto session = ReadSession();
    if (session.isEmpty())
        return;
    ApplyWhenReady(first, session.first().toObject(), {});
    for (int i = 1; i < session.size(); ++i) {
        OpenWindow(session[i].toObject(), {});
    }
}
