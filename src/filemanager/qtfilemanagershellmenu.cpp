#include "filemanager/qtfilemanagershellmenu.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/panelmenuids.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include "operations/CompressCall.h"
#include "platform/shell/qtnativeshellmenu.h"
#include <QMenu>
#include <QMessageBox>
#include <QScopedValueRollback>

QtFileManagerShellMenu::QtFileManagerShellMenu(MainWindow &window, QMenu &menu,
                                               std::function<void(unsigned)> dispatch)
    : _window(window), _menu(menu),
      _services(
          *window.backend_, &window,
          {.Open = [this](const UString &path, const UString &type) { return Open(path, type); },
           .Compress =
               [this](const UString &folder, const UString &name, const UString &type,
                      bool addExtension, const UStringVector &paths, bool email, bool showDialog,
                      bool waitFinish) {
                   if (!waitFinish && _window.canLaunchGuiFilesystemOperation())
                       return _window.launchGuiArchiveCompress(folder, name, type, addExtension,
                                                               paths, email, showDialog);
                   return ::CompressFiles(folder, name, type, addExtension, paths, email,
                                          showDialog, waitFinish, _services);
               },
           .Modal =
               [this](CCompressDialog &source, const CObjectVector<NWildcard::CCensorPath> &paths,
                      QWidget *parent) {
                   return CreateArchiveDialog::PresentSource(source, paths, _services.Catalog(),
                                                             parent);
               },
           .Test =
               [this](const UStringVector &paths, bool hashMode) {
                   if (_window.canLaunchGuiFilesystemOperation())
                       return _window.launchGuiArchiveTest(paths, hashMode);
                   return ::TestArchives(paths, hashMode, _services);
               },
           .Extract =
               [this](const UStringVector &paths, const UString &folder, bool showDialog,
                      bool elimDup, UInt32 writeZone) {
                   if (_window.canLaunchGuiFilesystemOperation())
                       return _window.launchGuiArchiveExtract(paths, folder, showDialog, elimDup,
                                                              writeZone);
                   return ::ExtractArchives(paths, folder, showDialog, elimDup, writeZone,
                                            _services);
               },
           .Hash =
               [this](const UStringVector &paths, const UString &method) {
                   const auto indices = _window.sourceOperatedIndices();
                   const auto &panel = _window.app_.GetFocusedPanel();
                   if (paths.Size() == 1 && indices.Size() == 1 && panel.IsFSFolder() &&
                       !panel.IsItem_Folder(indices[0]) &&
                       panel.GetItemFullPath(indices[0]) == paths[0]) {
                       _window.calculateHash(ToQString(method));
                       return S_OK;
                   }
                   if (_window.canLaunchGuiFilesystemOperation())
                       return _window.launchGuiHash(paths, method);
                   return ::CalcChecksum(paths, method, _services);
               }},
          MainWindow::toolbarSettingsPath()),
      _dispatch(std::move(dispatch)) {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(upstream_shell_icons);
        return true;
    }();
    (void)initialized;
}

QtFileManagerShellMenu::~QtFileManagerShellMenu() = default;

void QtFileManagerShellMenu::Dispatch(unsigned id, quint64 generation) {
    if (generation != _generation || !CanInvoke(id))
        return;
    _dispatch(id);
}

HRESULT QtFileManagerShellMenu::Open(const UString &path, const UString &type) {
    // The original kOpen starts another 7zFM, preserving the caller's folder.
    // Qt supplies an independent window/backend instead of an OS command.
    auto backend = std::unique_ptr<ArchiveBackend>(_window.backend_->createIndependentBackend());
    auto *receiver = new MainWindow(backend.get());
    backend->setParent(receiver);
    backend.release();
    receiver->setObjectName("shellArchiveWindow");
    receiver->setAttribute(Qt::WA_DeleteOnClose);
    receiver->show();
    return receiver->openArchiveAs(ToQString(path), ToQString(type));
}

bool QtFileManagerShellMenu::HasContextMenu(bool system) const {
#ifdef _WIN32
    if (system)
        return bool(_system);
#endif
    return !system && bool(_context);
}

bool QtFileManagerShellMenu::CanInvoke(unsigned id) const {
#ifdef _WIN32
    if (id >= kSystemStartMenuID)
        return _system && _system->CanInvoke(id);
#endif
    if (!_context || id < kMenuCmdID_Plugin_Start)
        return false;
    const unsigned offset = id - kMenuCmdID_Plugin_Start;
    return offset < _context->Commands().Size() && Available(_context->Commands()[offset]) &&
           !_context->Commands()[offset].IsPopup;
}

void QtFileManagerShellMenu::InvokePluginCommand(unsigned id) {
    _services.ResetResult();
    QScopedValueRollback<bool> source(_window.sourceArchiveProgress_, true);
    _window.app_.GetFocusedPanel().InvokePluginCommand(id, *this);
}

HRESULT QtFileManagerShellMenu::InvokeCommand(unsigned offset, bool system, const UString &) {
#ifdef _WIN32
    if (system)
        return _system ? _system->Invoke(offset) : E_INVALIDARG;
#endif
    if (system || !_context)
        return E_INVALIDARG;
    if (offset >= _context->Commands().Size() || !Exposed(_context->Commands()[offset]))
        return E_INVALIDARG;
    const HRESULT result = _context->InvokeCommand(offset);
    return result == S_OK ? _services.Result() : result;
}

void QtFileManagerShellMenu::ShowInvokeError(HRESULT result) {
    // Synchronous native operations already present their source errors;
    // cancellation needs no second message box in the parent window.
    if (result != E_ABORT && result != _services.Result())
        QMessageBox::warning(&_window, QStringLiteral("InvokeCommand"),
                             ToQString(HResultToMessage(result)));
}

void QtFileManagerShellMenu::ReleasePluginMenus() {
    ++_generation;
    _context.reset();
#ifdef _WIN32
    _system.reset();
#endif
}
bool QtFileManagerShellMenu::IsShiftDown() const {
    return QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
}

void QtFileManagerShellMenu::CreateSevenZipMenu(CPanel &panel, bool extended,
                                                const CRecordVector<UInt32> &indices,
                                                int firstDirIndex) {
    if (_window.projectedUi().state != MainWindow::State::Filesystem)
        return;
    panel.LoadFullPath();
    NQtShellMenu::CMenuHandle tree;
    panel.CreateSevenZipMenu(&tree, extended, indices, firstDirIndex, _context,
                             MainWindow::toolbarSettingsPath(), _services);
    if (_context)
        Render(_menu, tree);
}

void QtFileManagerShellMenu::CreateSystemMenu(CPanel &panel, bool extended,
                                              const CRecordVector<UInt32> &indices) {
#ifdef _WIN32
    if (indices.IsEmpty() || _window.projectedUi().state != MainWindow::State::Filesystem)
        return;
    auto system = std::make_unique<QtNativeShellMenu>();
    QMenu rendered;
    const quint64 generation = _generation;
    if (!system->Create(panel, extended, indices, rendered,
                        [this, generation](unsigned id) { Dispatch(id, generation); }))
        return;
    // Source inserts the System submenu at position zero. Transfer its Qt
    // presentation while retaining the native handles in the generation owner.
    for (auto *action : rendered.actions()) {
        if (action->menu()) {
            action->menu()->setParent(&_menu);
            action->menu()->setTitle(
                QCoreApplication::translate("qtfilemanagershellmenu", "System actions"));
            action->setObjectName("systemContextGroup");
        } else
            action->setParent(&_menu);
        _menu.insertAction(_menu.actions().isEmpty() ? nullptr : _menu.actions().first(), action);
    }
    _system = std::move(system);
#else
    (void)panel;
    (void)extended;
    (void)indices;
#endif
}
