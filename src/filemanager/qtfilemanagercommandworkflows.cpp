#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/about/AboutDialog.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "filemanager/HelpUtils.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "operations/CompressCall.h"
#include <QScopedValueRollback>

bool QtFileManagerCommands::IsShiftDown() const {
    return QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
}

void QtFileManagerCommands::OpenNative(bool internal, bool external, const wchar_t *type) {
    RunNativeOpen([=](CPanel &panel) {
        if (internal && !external)
            panel.OpenFocusedItemAsInternal(type);
        else
            panel.OpenSelectedItems(internal);
    });
}

void QtFileManagerCommands::ActivateItems(Qt::KeyboardModifiers modifiers) {
    auto &operations = window_.app_.QtOperations();
    auto &panel = window_.app_.GetFocusedPanel();
    if (operations.closed() || operations.blocking() || operations.callActive() ||
        window_.sourceArchiveProgress_ || !panel.ShouldProcessNotifications() ||
        !panel.GetFolder() || QApplication::activeModalWidget())
        return;
    QtAppOperationState::CallScope call(operations);
    RunNativeOpen([=](CPanel &owner) { owner.OnNotifyActivateItems(modifiers); });
}

void QtFileManagerCommands::RunNativeOpen(const std::function<void(CPanel &)> &open) {
    auto &w = window_;
    auto &panel = w.app_.GetFocusedPanel();
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    const bool wasFilesystem = panel.IsFSFolder();
    const bool wasRoot = panel.IsRootFolder();
    CMyComPtr<IFolderFolder> previousFolder = panel.GetFolder();
    const UString previousPrefix = panel.GetCurrentFolderPrefix();
    const auto previousState = w.state_;
    const QString previousDirectory = w.directory_;
    if (wasFilesystem)
        w.returnDirectory_ = previousDirectory;
    w.setState(MainWindow::State::Operating);
    {
        QScopedValueRollback<bool> source(w.sourceArchiveProgress_, true);
        open(panel);
    }
    if (wasFilesystem && panel.IsFSFolder() &&
        QDir::cleanPath(ToQString(GetFolderPath(panel.GetFolder()))) != previousDirectory)
        w.history_.append(previousDirectory);
    if (wasRoot && panel.IsFSFolder())
        w.history_.append(QString()); // Empty native root prefix in Qt Back history.
    if ((IFolderFolder *)previousFolder == panel.GetFolder() &&
        previousPrefix == panel.GetCurrentFolderPrefix())
        w.setState(previousState);
    else
        w.renderBoundFolder();
}

void QtFileManagerCommands::OpenItem() { OpenNative(true, true, nullptr); }
void QtFileManagerCommands::OpenItemInside(const wchar_t *type) { OpenNative(true, false, type); }
void QtFileManagerCommands::OpenItemOutside() { OpenNative(false, true, nullptr); }
void QtFileManagerCommands::EditItem(bool editor) {
    auto &panel = window_.app_.GetFocusedPanel();
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    panel.EditItem(editor);
}
void QtFileManagerCommands::DiffFiles() { window_.app_.DiffFiles(); }
void QtFileManagerCommands::Rename() { window_.app_.GetFocusedPanel().RenameFile(); }
void QtFileManagerCommands::CopyTo() {
    window_.app_.OnCopy(false, false, window_.app_.GetFocusedPanelIndex());
}
void QtFileManagerCommands::MoveTo() {
    window_.app_.OnCopy(true, false, window_.app_.GetFocusedPanelIndex());
}
void QtFileManagerCommands::Delete(bool toRecycleBin) {
    const unsigned index = window_.app_.GetFocusedPanelIndex();
    CSelectedState selection;
    window_.app_.Panels[index].DeleteItems(toRecycleBin, *window_.trashService_, selection);
    window_.app_.QtOperations().ContinueScanAfterSourceCall(index);
}
void QtFileManagerCommands::CalculateCrc(const char *method) {
    window_.calculateHash(QString::fromLatin1(method));
}
void QtFileManagerCommands::Split() { window_.startVolume(VolumeOperation::Mode::Split); }
void QtFileManagerCommands::Combine() { window_.startVolume(VolumeOperation::Mode::Combine); }
void QtFileManagerCommands::Properties() { window_.showSelectedProperties(); }
void QtFileManagerCommands::CreateFolder() {
    const unsigned index = window_.app_.GetFocusedPanelIndex();
    window_.app_.Panels[index].CreateFolder();
    window_.app_.QtOperations().ContinueScanAfterSourceCall(index);
}
void QtFileManagerCommands::CreateFile() {
    const unsigned index = window_.app_.GetFocusedPanelIndex();
    window_.app_.Panels[index].CreateFile();
    window_.app_.QtOperations().ContinueScanAfterSourceCall(index);
}
void QtFileManagerCommands::AddToArchive() { window_.createDialog(); }
void QtFileManagerCommands::ExtractArchives() { window_.extractArchives(); }
void QtFileManagerCommands::TestArchives() { window_.testArchive(); }
bool QtFileManagerCommands::ExecuteExtensionCommand(unsigned id) {
    switch (id) {
    case MountReadOnly:
        MountReadOnlyContainer();
        break;
    case ManageMounts:
        ShowMounts();
        break;
    case OpenDevicePicker:
        OpenDevice();
        break;
    case OpenArchivePicker: {
        const QString directory = window_.state_ == MainWindow::State::Viewing
                                      ? QFileInfo(window_.archive_).absolutePath()
                                      : window_.directory_;
        const QString path = QFileDialog::getOpenFileName(
            &window_, QCoreApplication::translate("qtfilemanagercommandworkflows", "Open archive"),
            directory, window_.backend_->openDialogFilter());
        if (!path.isEmpty())
            window_.openArchive(path);
        break;
    }
    case Back:
        window_.goBack();
        break;
    default:
        return false;
    }
    return true;
}
void QtFileManagerCommands::CloseWindow() { window_.close(); }
void QtFileManagerCommands::SelectAll(bool select) { window_.SelectAll(select); }
void QtFileManagerCommands::InvertSelection() { window_.InvertSelection(); }
void QtFileManagerCommands::SelectSpec(bool select) { window_.SelectSpec(select); }
void QtFileManagerCommands::SelectByType(bool select) { window_.SelectByType(select); }
void QtFileManagerCommands::Refresh_StatusBar() { window_.refreshActions(); }
void QtFileManagerCommands::SetListViewMode(unsigned index) { window_.app_.SetListViewMode(index); }
void QtFileManagerCommands::SortItemsWithPropID(unsigned propID) {
    window_.app_.GetFocusedPanel().SortItemsWithPropID(propID);
}
void QtFileManagerCommands::OpenRootFolder() {
    window_.goRoot(true, MainWindow::RootLocation::Drives);
}
void QtFileManagerCommands::OpenParentFolder() { window_.goParent(); }
void QtFileManagerCommands::FoldersHistory() { window_.showFoldersHistory(); }
void QtFileManagerCommands::ChangeFlatMode() { window_.app_.ChangeFlatMode(); }
void QtFileManagerCommands::RefreshView() { window_.app_.RefreshView(); }
void QtFileManagerCommands::SwitchStandardToolbar() { window_.app_.SwitchStandardToolbar(); }
void QtFileManagerCommands::SwitchArchiveToolbar() { window_.app_.SwitchArchiveToolbar(); }
void QtFileManagerCommands::SwitchButtonsLables() { window_.app_.SwitchButtonsLables(); }
void QtFileManagerCommands::SwitchLargeButtons() { window_.app_.SwitchLargeButtons(); }
bool &QtFileManagerCommands::TimestampShowUTC() { return g_Timestamp_Show_UTC; }
void QtFileManagerCommands::OptionsDialog() { window_.showSettings(); }
void QtFileManagerCommands::OpenBookmark(unsigned index) { window_.openBookmark(index); }
void QtFileManagerCommands::SetBookmark(unsigned index) { window_.saveBookmark(index); }

// Original CApp dispatch retains the program-menu plugin until invocation and
// releases it through the same source ExecuteFileCommand branch afterward.
void QtFileManagerCommands::InvokePluginCommand(unsigned id) {
    window_.programFileMenu_->InvokePluginCommand(id);
}
void QtFileManagerCommands::ReleasePluginMenus() { window_.programFileMenu_->ReleasePluginMenus(); }
void QtFileManagerCommands::VerCtrl(unsigned id) {
    auto &w = window_;
    CSelectedState selection;
    w.app_.GetFocusedPanel().SaveSelectedState(selection);
    w.app_.VerCtrl(id);
    w.showDirectory(w.directory_, false);
    w.restoreListSelection(selection);
    w.refreshActions();
}
void QtFileManagerCommands::Comment() {
    auto &w = window_;
    const unsigned index = w.app_.GetFocusedPanelIndex();
    {
        QScopedValueRollback<bool> source(w.sourceArchiveProgress_, true);
        w.app_.Panels[index].ChangeComment();
    }
    w.app_.QtOperations().ContinueScanAfterSourceCall(index);
    w.refreshActions();
}
void QtFileManagerCommands::Link() {
    auto &w = window_;
    QtAppOperationState::CallScope call(w.app_.QtOperations());
    CPanel::CDisableTimerProcessing disableTimerProcessing1(w.app_.Panels[0]);
    CPanel::CDisableTimerProcessing disableTimerProcessing2(w.app_.Panels[1]);
    w.app_.Link();
    w.refreshActions();
}
void QtFileManagerCommands::OpenAltStreams() {
    auto &w = window_;
    QtAppOperationState::CallScope call(w.app_.QtOperations());
    w.app_.GetFocusedPanel().OpenAltStreams();
    w.renderBoundFolder();
    w.refreshActions();
}
void QtFileManagerCommands::Change_AutoRefresh_Mode() {
    window_.app_.Change_AutoRefresh_Mode();
    window_.autoRefreshAction_->setChecked(window_.app_.Get_AutoRefresh_Mode());
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("view/autoRefresh"), window_.app_.Get_AutoRefresh_Mode());
}
void QtFileManagerCommands::SwitchOnOffOnePanel() { window_.togglePanels(); }
void QtFileManagerCommands::RedrawListItems_InPanels() {
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("view/timestampUTC"), g_Timestamp_Show_UTC);
    window_.redrawTimestampColumns();
}
unsigned QtFileManagerCommands::TimestampLevelCount() const {
    return window_.app_._timestampLevels.Size();
}
int QtFileManagerCommands::TimestampLevel(unsigned index) const {
    return window_.app_._timestampLevels[index];
}
void QtFileManagerCommands::SetTimestampLevel(int level) {
    window_.app_.SetTimestampLevel(level);
    QSettings settings(MainWindow::toolbarSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("view/timestampLevel"), level);
    window_.redrawTimestampColumns();
}
void QtFileManagerCommands::MyBenchmark(bool totalMode) {
    auto &w = window_;
    QtAppOperationState::CallScope call(w.app_.QtOperations());
    QString error;
    auto *operation = w.backend_->startGuiBenchmark(totalMode, error);
    w.observeGuiArchiveOperation(operation, error);
}
void QtFileManagerCommands::ShowHelpWindow(const char *topic) { ::ShowHelpWindow(topic, &window_); }
void QtFileManagerCommands::AboutDialog() {
    const auto catalog = window_.backend_->codecCatalog();
    CAboutDialog dialog(&window_, catalog ? &catalog->nativeCodecs() : nullptr);
    dialog.Create();
}
void QtFileManagerCommands::MyBrowseForTempFolder() {
    ::MyBrowseForTempFolder(&window_, window_.fileOpener_);
}
