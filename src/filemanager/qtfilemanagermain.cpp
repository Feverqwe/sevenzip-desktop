#include "Common/Common.h"
#include "filemanager/qtfilemanagerinitservices.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/associations/qtassociationfileopen.h"
#include "platform/memory/qtmemorylockservices.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/progress/qtprogresscontrols.h"
#ifdef Q_OS_MACOS
#include "app/qtapplicationstartup.h"
#endif
#ifdef _WIN32
#include "C/Alloc.h"
#endif
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <QThreadPool>
#include <QTimer>
#include <memory>

namespace {
bool WaitForFileManagerCodecs(ArchiveBackend &backend, QString &error,
                              QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents) {
    QEventLoop discovery;
    bool ready = false;
    QObject::connect(&backend, &ArchiveBackend::codecCatalogReady, &discovery,
                     [&](bool success, const QString &message) {
                         ready = success;
                         error = message;
                         discovery.quit();
                     });
    if (backend.discoverCodecCatalog())
        discovery.exec(flags);
    return ready;
}
} // namespace

MainWindow *OpenSevenzipAssociatedFile(const QString &path) {
    auto backend = std::make_unique<ArchiveBackend>();
    QtFileManagerStartup startup;
    startup.FullPath = QFileInfo(path).absolutePath().toStdWString().c_str();
    auto receiver = std::make_unique<MainWindow>(backend.get(), std::function<bool(const QUrl &)>{},
                                                 nullptr, &startup);
    receiver->setAttribute(Qt::WA_DeleteOnClose);
    auto *window = receiver.get();
    auto *service = backend.get();
    service->setParent(window);
    backend.release();
    // The original independent FM launch loads its codec owner before opening
    // the requested archive. A macOS file-open event must honor that same
    // prerequisite without sharing another window's operation state.
    QObject::connect(
        service, &ArchiveBackend::codecCatalogReady, window,
        [window, path](bool success, const QString &message) {
            const QPointer<MainWindow> alive(window);
            if (success) {
                if (!QFileInfo::exists(path)) {
                    window->show();
                    QMessageBox warning(QMessageBox::Warning, QStringLiteral("Sevenzip"),
                                        QCoreApplication::translate("mainwindow_archiveopening",
                                                                    "File not found: ") +
                                            path,
                                        QMessageBox::Ok, window);
                    warning.setTextFormat(Qt::PlainText);
                    warning.exec();
                    return;
                }
                window->openPath(path);
                if (alive)
                    window->show();
            } else {
                window->show();
                QMessageBox::critical(window, QStringLiteral("7-Zip"), message);
            }
        },
        Qt::SingleShotConnection);
    // Discovery can hold dyld's process-wide loader lock even on its worker.
    // Expose the requested document after binding, without a directory flash.
    service->discoverCodecCatalog();
    return receiver.release();
}

int RunSevenzipDesktop(QApplication &application) {
    QThreadPool::globalInstance()->start(
        [] { QtOwnedTempRegistry().CleanUnusedSystemTemporaryObjects(); });
    QtWorkspaceApplication workspace(application);
    QtAssociationFileOpen associatedFiles(application);
    QObject::connect(&associatedFiles, &QtAssociationFileOpen::OpenRequested, &application,
                     [](const QString &path) { OpenSevenzipAssociatedFile(path); });
#ifdef Q_OS_MACOS
    // Cocoa delivers initial documents before didFinishLaunching. Await that
    // native transaction; a single processEvents flush can return too early.
    if (!QtWaitForMacApplicationLaunch())
        return 0;
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool associatedStartup = associatedFiles.Activate();
#else
    associatedFiles.Activate();
    const bool associatedStartup = false;
#endif
    return RunSevenzipFileManager(application, associatedStartup);
}

int RunSevenzipFileManager(QApplication &application, bool associatedStartup) {
    return NQtFileManager::WinMain([&] {
        NQtFileManager::GetRamInfo();
#if defined(_WIN32) && defined(Z7_LARGE_PAGES)
        z7_LargePage_Set(0, 0, 0);
#endif
        LoadLangOneTime();
        QtFileManagerStartup startup;
        if (!associatedStartup) {
#ifdef Q_OS_WIN
            startup = WinMain2(UString(GetCommandLineW()));
            // Qt's main() has no nCmdShow parameter. Recover the original WinMain
            // launch value before a progress dialog can consume the first ShowWindow.
            STARTUPINFOW nativeStartup{};
            nativeStartup.cb = sizeof(nativeStartup);
            GetStartupInfoW(&nativeStartup);
            startup.ShowCommand = nativeStartup.dwFlags & STARTF_USESHOWWINDOW
                                      ? nativeStartup.wShowWindow
                                      : NQtFileManager::NShowCommand::Default;
#else
            const QStringList arguments = application.arguments();
            startup = WinMain2(UString(arguments.value(1).toStdWString().c_str()),
                               UString(arguments.mid(2).join(' ').toStdWString().c_str()));
#endif
        }
        const QtMemoryLockServices memoryLock;
        NQtFileManager::SetMemoryLock(FmSettingsPath(), memoryLock);
        QtInitializeLinkPlatform();
        if (associatedStartup)
            return application.exec();
        ArchiveBackend backend;
        bool showBeforeDiscovery = false;
#ifdef Q_OS_MACOS
        showBeforeDiscovery =
            !startup.NeedOpenArc && startup.ShowCommand != NQtFileManager::NShowCommand::Hide;
#endif
        QString codecError;
        if (!showBeforeDiscovery && !WaitForFileManagerCodecs(backend, codecError) &&
            startup.NeedOpenArc)
            throw UString(codecError.toStdWString().c_str());
        try {
            const bool quitOnClose = application.quitOnLastWindowClosed();
            const auto restore =
                qScopeGuard([&] { application.setQuitOnLastWindowClosed(quitOnClose); });
            application.setQuitOnLastWindowClosed(false);
            MainWindow window(&backend, {}, nullptr, &startup);
            const auto restoreSession = [&] {
                if (startup.FullPath.IsEmpty())
                    if (auto *workspace = QtWorkspaceApplication::Find())
                        workspace->Restore(window);
                if (!codecError.isEmpty())
                    window.statusBar()->showMessage(codecError);
            };
            if (!showBeforeDiscovery)
                restoreSession();
            const QString loading =
                showBeforeDiscovery
                    ? QCoreApplication::translate("ProgressRedesign", "Loading archive formats…")
                    : QString();
            if (showBeforeDiscovery) {
                window.setCursor(QtWaitCursor());
                window.statusBar()->showMessage(loading);
            }
            if (startup.ShowCommand != NQtFileManager::NShowCommand::Hide)
                window.show();
            if (showBeforeDiscovery)
                QTimer::singleShot(0, &window, [&] {
                    // Paint the filesystem window before the worker can take
                    // dyld's loader lock. Defer input until archive commands
                    // can use the retained codec catalog.
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    if (!window.isVisible())
                        return;
                    WaitForFileManagerCodecs(backend, codecError,
                                             QEventLoop::ExcludeUserInputEvents);
                    if (!window.isVisible())
                        return;
                    window.unsetCursor();
                    if (window.statusBar()->currentMessage() == loading)
                        window.statusBar()->clearMessage();
                    restoreSession();
                });
            application.setQuitOnLastWindowClosed(quitOnClose);
            return application.exec();
        } catch (const QtFileManagerStartupFailure &failure) {
            // WM_CREATE refusal makes InitInstance fail and WinMain2 returns
            // FALSE. E_ABORT skips the error message in the original body.
            if (!failure.Message.IsEmpty())
                QMessageBox::critical(nullptr, QStringLiteral("7-Zip"),
                                      QString::fromWCharArray(failure.Message));
            return 0;
        }
    });
}
