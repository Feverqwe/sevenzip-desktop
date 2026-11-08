#include "7zip/UI/Explorer/resource.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "operations/qtoperationlauncher.h"
#include <QEventLoop>
#include <QFileInfo>
#include <QScopedValueRollback>

QtFileManagerDrag::DroppedPathAction
QtFileManagerDrag::ChooseDroppedPathAction(const QString &path) {
    QMessageBox dialog(
        QMessageBox::Question, QCoreApplication::translate("DroppedPath", "Choose an action"),
        QCoreApplication::translate("DroppedPath", "What would you like to do with “%1”?")
            .arg(QFileInfo(path).fileName()),
        QMessageBox::Open | QMessageBox::Cancel, &window_);
    dialog.setObjectName("droppedPathAction");
    dialog.setTextFormat(Qt::PlainText);
    auto *compress = dialog.addButton(
        QCoreApplication::translate("ArchiveRedesign", "Create archive"), QMessageBox::ActionRole);
    compress->setObjectName("compressDroppedPath");
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.setEscapeButton(QMessageBox::Cancel);
    dialog.exec();
    if (dialog.clickedButton() == dialog.button(QMessageBox::Open))
        return DroppedPathAction::Open;
    if (dialog.clickedButton() == compress)
        return DroppedPathAction::Compress;
    return DroppedPathAction::Cancel;
}

bool QtFileManagerDrag::ConfirmCopyToArchive(CPanel &panel, DWORD effect) {
    UString s = LangString(effect == DROPEFFECT_MOVE ? IDS_MOVE_TO : IDS_COPY_TO);
    s.Add_LF();
    s += panel.GetFsPath();
    s.Add_LF();
    AddLangString(s, IDS_WANT_TO_COPY_FILES);
    s += " ?";
    return QMessageBox::question(
               &window_, ToQString(LangString(IDS_CONFIRM_FILE_COPY)), ToQString(s),
               QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel) == QMessageBox::Yes;
}
UInt32 QtFileManagerDrag::Drag_OnContextMenu(int x, int y, UInt32 mask, DWORD allowedEffects) {
    struct Pair {
        UInt32 command;
        unsigned language;
    };
    const Pair pairs[] = {
        {NDragMenu::k_Copy_Base | NDragMenu::k_MenuFlag_Copy, IDS_COPY},
        {NDragMenu::k_Copy_Base | NDragMenu::k_MenuFlag_Move, IDS_MOVE},
        {NDragMenu::k_Copy_ToArc | NDragMenu::k_MenuFlag_Copy, IDS_COPY_TO},
        {NDragMenu::k_AddToArc | NDragMenu::k_MenuFlag_Copy, IDS_CONTEXT_COMPRESS},
        {NDragMenu::k_Cancel, 402}};
    QMenu menu(&window_);
    menu.setObjectName("dragContextMenu");
    window_.setFocus();
    for (const auto &pair : pairs) {
        const UInt32 cmd = pair.command & NDragMenu::k_MenuFlags_CmdMask;
        if (cmd != NDragMenu::k_Cancel) {
            const DWORD effect =
                pair.command & NDragMenu::k_MenuFlag_Move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            if (!(mask & (UInt32(1) << cmd)) || !(allowedEffects & effect))
                continue;
        }
        UString name = LangString(pair.language);
        if (name.IsEmpty()) {
            if (cmd == NDragMenu::k_Cancel)
                name = "Cancel";
            else
                name.Add_UInt32(pair.language);
        }
        if (cmd == NDragMenu::k_Copy_ToArc) {
            name.Add_Space();
            AddLangString(name, IDS_CONTEXT_ARCHIVE);
        }
        if (cmd == NDragMenu::k_Cancel)
            menu.addSeparator();
        menu.addAction(ToQString(name))->setData(pair.command);
    }
    QAction *chosen = menu.exec({x, y});
    return chosen ? chosen->data().toUInt() : UInt32(NDragMenu::k_Cancel);
}
HRESULT QtFileManagerDrag::CompressFiles(CPanel &, const UString &folder, const UString &name,
                                         const UStringVector &paths, bool waitFinish) {
    QStringList inputs;
    FOR_VECTOR(i, paths)
    inputs.append(ToQString(paths[i]));
    QString error;
    auto *operation =
        window_.backend_->startGuiCompress(QFileInfo(ToQString(folder + name)).absoluteFilePath(),
                                           {}, true, inputs, false, true, error);
    const HRESULT launched = window_.observeGuiArchiveOperation(operation, error);
    if (!operation || !waitFinish)
        return launched;

    // PanelDrag keeps temporary sources alive until this call returns. Preserve
    // CompressCall's waitFinish branch while servicing the independent child.
    // Prevent manager teardown/reentrant commands from releasing that source.
    QtAppOperationState::CallScope call(window_.app_.QtOperations());
    QScopedValueRollback<bool> progressOwner(window_.sourceArchiveProgress_, true);
    QEventLoop wait;
    QtOperationResult result;
    connect(operation, &QtOperationLauncher::finished, &wait,
            [&](const QtOperationResult &finished) {
                result = finished;
                wait.quit();
            });
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    using Outcome = QtOperationResult::Outcome;
    if (result.Status == Outcome::Cancelled)
        return E_ABORT;
    return result.Status == Outcome::Success ? S_OK : E_FAIL;
}
