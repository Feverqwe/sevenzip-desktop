#pragma once

#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "Common/StringConvert.h"
#include "archive/resources/qtarchivebatchextract.h"
#include "dialogs/extract/ExtractDialog.h"
#include "dialogs/messages/MessagesDialog.h"
#include "gui/ExtractGUI.h"
#include "platform/shell/qtshellarchiveservices.h"
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

namespace ExtractDialogFixture {
// Exercise the real modal consumer rather than translating widget values into
// a second request in the test. The callback chooses OK or Cancel itself.
inline QtArchiveBatchResult Run(ArchiveBackend &backend, const QString &archive,
                                const QString &destination,
                                const std::function<void(CExtractDialog &)> &interact) {
    QTimer timer;
    bool presented = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QWidget *widget = QApplication::activeModalWidget();
        if (!widget)
            return;
        if (auto *dialog = qobject_cast<CExtractDialog *>(widget); dialog && !presented) {
            presented = true;
            interact(*dialog);
        }
        // Handle the active modal only: clicking the progress window beneath
        // its final message can enter a nested Cancel question before native
        // completion resumes. Preserve the native Result/CallbackOK below.
        else if (auto *box = qobject_cast<QMessageBox *>(widget))
            box->accept();
        else if (auto *messages = dynamic_cast<CMessagesDialog *>(widget))
            messages->accept();
        else if (widget->objectName() == "progressDialog") {
            auto *close = widget->findChild<QPushButton *>("IDCANCEL");
            if (close && close->property("languageID").toUInt() == IDS_CLOSE)
                close->click();
        }
    });
    timer.start(1);
    CCodecs codecs;
    CObjectVector<COpenType> types;
    UStringVector paths, fullPaths;
    paths.Add(UString(QFileInfo(archive).absoluteFilePath().toStdWString().c_str()));
    fullPaths = paths;
    NWildcard::CCensorNode censor;
    censor.Add_Wildcard();
    CExtractOptions options;
    options.OutputDir = us2fs(UString(destination.toStdWString().c_str()));
    options.OutDirMode = NExtractOutDirMode::k_Direct;
    CMyComPtr2_Create<IFolderArchiveExtractCallback, CExtractCallbackImp> callback;
    callback->SettingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    QtShellArchiveServices services(backend, nullptr);
    QtArchiveBatchResult result;
    result.Result = services.ExtractGUI(codecs, types, paths, fullPaths, censor, options, true,
                                        result.MessageWasDisplayed, *callback.ClsPtr());
    result.CallbackOK = callback->IsOK();
    return result;
}
} // namespace ExtractDialogFixture
