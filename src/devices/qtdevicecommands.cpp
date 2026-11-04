#include "archive/resources/qtfilesystemreadprompt.h"
#include "devices/deviceaccess.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"

void QtFileManagerCommands::OpenDevice() {
    auto &w = window_;
    const unsigned owner = w.app_.GetFocusedPanelIndex();
    DeviceSelectionDialog dialog(w.backend_->codecCatalog(), &w, w.deviceServices_);
    if (dialog.exec() != QDialog::Accepted)
        return;
    w.activatePanelUi(owner);
    CMyComPtr<IInStream> input = dialog.selectedStream();
    const UString name = ToUString(dialog.selectedName());
    RunNativeOpen([&](CPanel &panel) {
        CPanel::CDisableTimerProcessing timer(panel);
        CPanel::CDisableNotify notify(panel);
        CTempFileInfo temporary;
        COpenResult opened;
        HRESULT result = panel.OpenAsArc(input, temporary, name, UString(), opened);
        if (result == kDeviceReadLimitationsRequired) {
            if (!ConfirmLimitedFilesystemRead(&w, ToQString(opened.ErrorMessage)))
                return;
            CMyComPtr<IInStream> withoutReplay;
            result = CreateDeviceInputAllowingIncompleteRead(input, &withoutReplay);
            if (result == S_OK) {
                input = withoutReplay;
                result = panel.OpenAsArc(input, temporary, name, UString(), opened);
            }
        }
        if (result != S_OK && result != E_ABORT) {
            const QString error =
                !opened.ErrorMessage.IsEmpty() ? ToQString(opened.ErrorMessage)
                : result == E_NOTIMPL
                    ? QCoreApplication::translate("qtdevicecommands",
                                                  "This filesystem uses features that are not "
                                                  "supported for direct reading.")
                    : QCoreApplication::translate(
                          "qtdevicecommands",
                          "Could not open this partition. It may be unsupported or the device may "
                          "have been disconnected.");
            QMessageBox::warning(&w, QCoreApplication::translate("qtdevicecommands", "Open device"),
                                 error);
        }
    });
}
