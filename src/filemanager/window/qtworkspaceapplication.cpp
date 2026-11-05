#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>

QtWorkspaceApplication::QtWorkspaceApplication(QApplication &application) : QObject(&application) {
    setObjectName(QStringLiteral("sevenzipWorkspaceApplication"));
    connect(&application, &QCoreApplication::aboutToQuit, this, [this] { Save(); });
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

void QtWorkspaceApplication::SetAutoRefresh(bool enabled) {
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue("view/autoRefresh", enabled);
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed()) {
            window->app_.AutoRefresh_Mode = enabled;
            window->app_.SetPanels_AutoRefresh_Mode();
            const QSignalBlocker block(window->autoRefreshAction_);
            window->autoRefreshAction_->setChecked(enabled);
        }
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

void QtWorkspaceApplication::Quit() {
    Save();
    quitting_ = true;
    qApp->closeAllWindows();
    // A protected operation may refuse shutdown. Resume ordinary persistence.
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed()) {
            quitting_ = false;
            return;
        }
}

void QtWorkspaceApplication::ApplyWhenReady(MainWindow &window, const QJsonObject &state,
                                            std::function<void()> ready) {
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
    connect(timer, &QTimer::timeout, &window, [&window, state, timer, finish, ready] {
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
        window.workspace_->Restore(
            state,
            [finish, ready] {
                finish();
                if (ready)
                    ready();
            },
            finish);
    });
    timer->start();
}

MainWindow *QtWorkspaceApplication::NewWindow(const QString &path, const QJsonObject &session,
                                              std::function<void(MainWindow &)> ready) {
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
    const QJsonObject state =
        session.isEmpty()
            ? QJsonObject{{"panels",
                           QJsonArray{QJsonObject{
                               {"tabs", QJsonArray{QJsonObject{{"path", path}, {"view", 3}}}},
                               {"active", 0}}}},
                          {"count", 1}}
            : session;
    if (window->backend_->codecCatalog())
        ApplyWhenReady(*window, state, [window, ready] {
            if (ready)
                ready(*window);
        });
    else {
        connect(
            window->backend_, &ArchiveBackend::codecCatalogReady, this,
            [this, window, state, ready](bool, const QString &) {
                ApplyWhenReady(*window, state, [window, ready] {
                    if (ready)
                        ready(*window);
                });
            },
            Qt::SingleShotConnection);
        window->backend_->discoverCodecCatalog();
    }
    window->show();
    return window;
}

void QtWorkspaceApplication::Restore(MainWindow &first) {
    const auto session = ReadSession();
    if (session.isEmpty())
        return;
    ApplyWhenReady(first, session.first().toObject());
    for (int i = 1; i < session.size(); ++i) {
        NewWindow({}, session[i].toObject());
    }
}
