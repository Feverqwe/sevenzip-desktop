#include "dialogs/password/PasswordDialog.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#include "Windows/MemoryLock.h"
#include "fileops/links/qtlinkservices.h"
#endif

namespace {
class StartupCommandsObserver final : public QObject {
  public:
    bool shown = false;
    bool archiveCommandsReady = false;
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = dynamic_cast<MainWindow *>(object);
        if (!shown && window && event->type() == QEvent::Show) {
            shown = true;
            auto *test = window->findChild<QAction *>("testArchiveAction");
            archiveCommandsReady = test && test->isEnabled();
        }
        return false;
    }
};
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setApplicationName("Sevenzip File Manager entry probe");
    StartupCommandsObserver startupCommands;
    application.installEventFilter(&startupCommands);
    bool oleInitialized = false;
#ifdef Q_OS_WIN
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    // The retained Windows headers keep an older API declaration baseline.
    // Inspect the Qt-initialized apartment without changing that baseline or
    // loading/initializing COM in the probe.
    using GetApartmentType = HRESULT(WINAPI *)(APTTYPE *, APTTYPEQUALIFIER *);
    const auto getApartmentType = reinterpret_cast<GetApartmentType>(
        GetProcAddress(GetModuleHandleW(L"ole32.dll"), "CoGetApartmentType"));
    oleInitialized = getApartmentType && SUCCEEDED(getApartmentType(&apartment, &qualifier)) &&
                     (apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA);
#endif
    bool bound = false;
    bool refusal = false;
    bool password = false;
    bool timedOut = false;
    bool privilegeDropped = false;
    bool linkCopied = false;
    const QString copyLink = qEnvironmentVariable("SEVENZIP_PROBE_COPY_LINK");
#ifdef Q_OS_WIN
    if (!copyLink.isEmpty()) {
        NWindows::NSecurity::EnablePrivilege(SE_RESTORE_NAME, false);
        privilegeDropped =
            NWindows::NSecurity::EnablePrivilege(TEXT("SeCreateSymbolicLinkPrivilege"), false);
    }
#endif
    QJsonObject windowState;
    const bool inspectState = qEnvironmentVariableIsSet("SEVENZIP_PROBE_WINDOW_STATE");
    const QString expected = qEnvironmentVariable("SEVENZIP_PROBE_MEMBER");
    QTimer observer;
    QObject::connect(&observer, &QTimer::timeout, &application, [&] {
        if (auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget())) {
            password = true;
            dialog->reject();
            return;
        }
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            refusal = true;
            dialog->accept();
            return;
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *window = dynamic_cast<MainWindow *>(widget);
            if (!window || (!inspectState && !window->isVisible()))
                continue;
            auto *table = window->findChild<FileTable *>("archiveTable");
            if (!table || !table->isEnabled())
                continue;
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->model()->index(row, 0).data().toString() != expected)
                    continue;
                bound = true;
#ifdef Q_OS_WIN
                if (!copyLink.isEmpty())
                    linkCopied = QtCopyFileSystemLink(
                        copyLink, qEnvironmentVariable("SEVENZIP_PROBE_LINK_DESTINATION"));
#endif
                if (inspectState)
                    windowState = {
                        {"visible", window->isVisible()},
                        {"maximized", window->isMaximized()},
                        {"minimized", window->isMinimized()},
                        {"withoutActivation", window->testAttribute(Qt::WA_ShowWithoutActivating)}};
                window->close();
                return;
            }
        }
    });
    observer.start(5);
    QTimer::singleShot(30000, &application, [&] {
        timedOut = true;
        application.quit();
    });
    const int result = RunSevenzipDesktop(application);
    observer.stop();
    const QJsonObject report{
        {"bound", bound},
        {"archiveCommandsReadyAtFirstShow", startupCommands.archiveCommandsReady},
        {"refusal", refusal},
        {"password", password},
        {"timeout", timedOut},
        {"windowState", windowState},
        {"exitCode", result},
        {"privilegeDropped", privilegeDropped},
        {"linkCopied", linkCopied},
        {"oleInitialized", oleInitialized}};
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    return timedOut ? 1 : result;
}
