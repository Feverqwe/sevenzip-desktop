#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/about/AboutDialog.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlangcontrols.h"
#include "operations/CompressCall.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/progress/ProgressDialog2.h"
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

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
    w.workspace_->Navigate([&]() -> HRESULT {
        auto &panel = w.app_.GetFocusedPanel();
        const bool wasFilesystem = panel.IsFSFolder();
        CMyComPtr<IFolderFolder> previousFolder = panel.GetFolder();
        const UString previousPrefix = panel.GetCurrentFolderPrefix();
        const auto previousState = w.projectedUi().state;
        const QString previousDirectory = w.projectedUi().directory;
        const auto focused = w.projectedUi().table->currentIndex();
        if (focused.isValid() && focused.data(QtPanelList::SourceIndexRole).toUInt() != UInt32(-1))
            w.workspace_->RetainNavigationFolder();
        if (wasFilesystem)
            w.projectedUi().returnDirectory = previousDirectory;
        w.setState(MainWindow::State::Operating);
        {
            QScopedValueRollback<bool> source(w.sourceArchiveProgress_, true);
            open(panel);
        }
        if (const auto published = w.workspace_->NavigationPublication())
            return *published;
        if (w.projectedUi().scanningDisk || w.projectedUi().scanningArchive)
            return S_OK;
        if ((IFolderFolder *)previousFolder == panel.GetFolder() &&
            previousPrefix == panel.GetCurrentFolderPrefix()) {
            w.setState(previousState);
            return S_FALSE;
        }
        return w.renderBoundFolder();
    });
}

void QtFileManagerCommands::OpenItem() { OpenNative(true, true, nullptr); }
void QtFileManagerCommands::OpenItemInside(const wchar_t *type) { OpenNative(true, false, type); }
void QtFileManagerCommands::OpenItemOutside() { OpenNative(false, true, nullptr); }
void QtFileManagerCommands::EditItem(bool editor) {
    auto &panel = window_.app_.GetFocusedPanel();
    panel.EditItem(editor);
}
void QtFileManagerCommands::Rename() { window_.app_.GetFocusedPanel().RenameFile(); }
void QtFileManagerCommands::CopyTo() {
    window_.app_.OnCopy(false, false, window_.app_.GetFocusedPanelIndex());
}
void QtFileManagerCommands::MoveTo() {
    window_.app_.OnCopy(true, false, window_.app_.GetFocusedPanelIndex());
}
void QtFileManagerCommands::MoveToTrash() {
    const unsigned index = window_.app_.GetFocusedPanelIndex();
    CSelectedState selection;
    window_.app_.Panels[index].MoveItemsToTrash(*window_.trashService_, selection);
    window_.app_.QtOperations().ContinueScanAfterSourceCall(index);
}
void QtFileManagerCommands::CalculateCrc(const char *method) {
    window_.calculateHash(QString::fromLatin1(method));
}
void QtFileManagerCommands::Properties() { window_.showSelectedProperties(); }
void QtFileManagerCommands::CreateFolder() {
    const unsigned index = window_.app_.GetFocusedPanelIndex();
    auto &panel = window_.app_.Panels[index];
    auto *view = window_.panelUi_[index].viewStack;
    auto *parent = view->parentWidget();
    if (auto *name = parent->findChild<QLineEdit *>("inlineFolderName");
        name && name->isVisible()) {
        name->setFocus();
        return;
    }

    auto *editor = new QWidget(parent);
    editor->setObjectName("inlineCreateFolder");
    auto *row = new QVBoxLayout(editor);
    row->setContentsMargins(12, 6, 12, 6);
    auto *name = new QLineEdit(editor);
    name->setObjectName("inlineFolderName");
    const auto hint = QCoreApplication::translate("InlineCreateFolder", "Folder name");
    name->setPlaceholderText(hint);
    name->setAccessibleName(hint);
    row->addWidget(name);
    auto *error = new QtWrappingLabel(editor);
    error->setObjectName("inlineFolderError");
    error->setTextFormat(Qt::PlainText);
    error->hide();
    row->addWidget(error);
    auto *layout = static_cast<QVBoxLayout *>(parent->layout());
    layout->insertWidget(layout->indexOf(view), editor);
    const auto finish = [view, editor] {
        editor->hide();
        editor->deleteLater();
        view->currentWidget()->setFocus();
    };
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, editor, finish);
    QObject::connect(name, &QLineEdit::textEdited, error, &QWidget::hide);

    const CMyComPtr<IFolderFolder> source = panel.GetFolder();
    const UString prefix = panel.GetCurrentFolderPrefix();
    QObject::connect(
        name, &QLineEdit::returnPressed, editor,
        [this, index, source, prefix, name, error, editor, finish] {
            auto &panel = window_.app_.Panels[index];
            if (!window_.app_.IsPanelVisible(index) || panel.GetFolder() != source.Interface() ||
                panel.GetCurrentFolderPrefix() != prefix) {
                finish();
                return;
            }
            auto &operations = window_.app_.QtOperations();
            if (operations.closed() || operations.blocking() || operations.callActive())
                return;
            // The context menu may have temporarily projected this pane while
            // creating the inline editor. Submit against the editor's owner.
            window_.activatePanelUi(index);
            QString detail;
            HRESULT result = E_INVALIDARG;
            const QString value = name->text();
            if (!value.isEmpty() && !value.contains(QChar::Null)) {
                const QPointer<QWidget> alive(editor);
                editor->setEnabled(false);
                {
                    QtAppOperationState::CallScope call(operations);
                    {
                        QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
                        result = panel.CreateFolder(ToUString(value), detail);
                    }
                    operations.ContinueScanAfterSourceCall(index);
                }
                if (!alive || operations.closed())
                    return;
                editor->setEnabled(true);
            }
            if (result == S_OK) {
                finish();
                return;
            }
            error->setText(detail.isEmpty() ? ToQString(HResultToMessage(result)) : detail);
            error->show();
            name->setFocus();
        });
    editor->setToolTip(ToQString(prefix));
    editor->show();
    name->setFocus();
}
void QtFileManagerCommands::AddToArchive() { window_.createDialog(); }
void QtFileManagerCommands::ExtractArchives() { window_.extractArchives(); }
void QtFileManagerCommands::TestArchives() { window_.testArchive(); }
bool QtFileManagerCommands::ExecuteExtensionCommand(unsigned id) {
    switch (id) {
    case SortAscending:
    case SortDescending: {
        auto &panel = window_.app_.GetFocusedPanel();
        if (panel._ascending != (id == SortAscending))
            panel.SortItemsWithPropID(panel.GetSortID());
        window_.refreshActions();
        break;
    }
    case CalculateDirectorySize:
        CalculateDirectorySizes();
        break;
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
        const unsigned owner = window_.app_.GetFocusedPanelIndex();
        const QString directory = window_.projectedUi().state == MainWindow::State::Viewing
                                      ? QFileInfo(window_.projectedUi().archive).absolutePath()
                                      : window_.projectedUi().directory;
        const QString path = QFileDialog::getOpenFileName(
            &window_, QCoreApplication::translate("qtfilemanagercommandworkflows", "Open archive"),
            directory, window_.backend_->openDialogFilter());
        if (!path.isEmpty()) {
            window_.activatePanelUi(owner);
            window_.openArchive(path);
        }
        break;
    }
    case Back:
        window_.goBack();
        break;
    case Forward:
        window_.workspace_->Travel(1);
        break;
    default:
        return false;
    }
    return true;
}
void QtFileManagerCommands::CloseWindow() { window_.close(); }
void QtFileManagerCommands::SelectAll(bool select) { window_.SelectAll(select); }
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
void QtFileManagerCommands::RefreshView() {
    if (window_.app_.GetFocusedPanel().ArchiveContextOutdated())
        window_.workspace_->RefreshPublishedArchive(window_.projectedPanel_, true);
    else
        window_.app_.RefreshView();
}
void QtFileManagerCommands::OptionsDialog() { window_.showSettings(); }
void QtFileManagerCommands::OpenBookmark(unsigned index) { window_.openBookmark(index); }
void QtFileManagerCommands::SetBookmark(unsigned index) { window_.saveBookmark(index); }

// Original CApp dispatch retains the program-menu plugin until invocation and
// releases it through the same source ExecuteFileCommand branch afterward.
void QtFileManagerCommands::InvokePluginCommand(unsigned id) {
    window_.programFileMenu_->InvokePluginCommand(id);
}
void QtFileManagerCommands::ReleasePluginMenus() { window_.programFileMenu_->ReleasePluginMenus(); }
void QtFileManagerCommands::SwitchOnOffOnePanel() { window_.togglePanels(); }
void QtFileManagerCommands::AboutDialog() {
    CAboutDialog dialog(&window_);
    dialog.Create();
}
