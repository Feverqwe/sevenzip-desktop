#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/qtguicommand.h"
#include "support/native_mapi_loader.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <windows.h>

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    const QString provider =
        QFileInfo(qEnvironmentVariable("SEVENZIP_MAPI_TEST_PROVIDER")).canonicalFilePath();
    // Preload the exact private provider before original DllSecur startup.
    // Failure is terminal: the fixture must never call the system mail client.
    HMODULE module = LoadPrivateMAPIProvider(provider);
    if (!module)
        return 101;
    QJsonArray messages;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, &application, [&] {
        if (auto *dialog = qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget())) {
            if (qEnvironmentVariable("SEVENZIP_MAPI_TEST_DIALOG") == "cancel")
                dialog->reject();
            else
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            messages.append(box->text());
            box->accept();
        } else if (auto *progress =
                       dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget())) {
            auto *close = progress->findChild<QPushButton *>("IDCANCEL");
            if (close && close->property("languageID").toUInt() == IDS_CLOSE)
                close->click();
        }
    });
    // Only the shell-email fixture requests automation in this private host.
    if (qEnvironmentVariableIsSet("SEVENZIP_MAPI_TEST_DIALOG"))
        dialogs.start(5);
    const QString before = QDir::currentPath();
    const int result = RunSevenzipGUI(application);
    QFile report(qEnvironmentVariable("SEVENZIP_MAPI_TEST_REPORT") + ".host.json");
    if (!report.open(QIODevice::WriteOnly))
        return 105;
    report.write(QJsonDocument(QJsonObject{{"cwdBefore", before},
                                           {"cwdAfter", QDir::currentPath()},
                                           {"result", result},
                                           {"messages", messages},
                                           {"provider", provider}})
                     .toJson());
    FreeLibrary(module);
    return result;
}
