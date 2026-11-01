#include "filemanager/qtfilemanagershellmenu.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/App.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include "operations/CompressCall.h"
#include "platform/shell/qtnativeshellmenu.h"
#include <QBitmap>
#include <QMenu>
#include <QMessageBox>
#include <QScopedValueRollback>

QtFileManagerShellMenu::QtFileManagerShellMenu(MainWindow &window, QMenu &menu,
                                               std::function<void(unsigned)> dispatch)
    : _window(window), _menu(menu),
      _services(
          *window.backend_, &window,
          [this](const UString &path, const UString &type) { return Open(path, type); },
          [this](const UString &folder, const UString &name, const UString &type, bool addExtension,
                 const UStringVector &paths, bool email, bool showDialog, bool waitFinish) {
              if (!waitFinish && _window.canLaunchGuiFilesystemOperation())
                  return _window.launchGuiArchiveCompress(folder, name, type, addExtension, paths,
                                                          email, showDialog);
              return ::CompressFiles(folder, name, type, addExtension, paths, email, showDialog,
                                     waitFinish, _services);
          },
          {},
          [this](CCompressDialog &source, const CObjectVector<NWildcard::CCensorPath> &paths,
                 QWidget *parent) {
              return CreateArchiveDialog::PresentSource(source, paths, _services.Catalog(), parent);
          },
          MainWindow::toolbarSettingsPath(),
          [this](const UStringVector &paths, bool hashMode) {
              if (_window.canLaunchGuiFilesystemOperation())
                  return _window.launchGuiArchiveTest(paths, hashMode);
              return ::TestArchives(paths, hashMode, _services);
          },
          [this](const UStringVector &paths, const UString &folder, bool showDialog, bool elimDup,
                 UInt32 writeZone) {
              if (_window.canLaunchGuiFilesystemOperation())
                  return _window.launchGuiArchiveExtract(paths, folder, showDialog, elimDup,
                                                         writeZone);
              return ::ExtractArchives(paths, folder, showDialog, elimDup, writeZone, _services);
          },
          {},
          [this](const UStringVector &paths, const UString &method) {
              if (_window.canLaunchGuiFilesystemOperation())
                  return _window.launchGuiHash(paths, method);
              return ::CalcChecksum(paths, method, UString(), UString(), _services);
          }),
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
    if (_dispatch) {
        _dispatch(id);
        return;
    }
    _services.ResetResult();
    {
        QScopedValueRollback<bool> source(_window.sourceArchiveProgress_, true);
        _window.app_.GetFocusedPanel().InvokePluginCommand(id, *this);
    }
    _window.refreshActions();
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

bool QtFileManagerShellMenu::Available(const CZipContextMenu::CCommandMapItem &command) const {
    const auto catalog = _window.backend_->codecCatalog();
    if (!catalog || catalog->result() != S_OK)
        return false;
    using C = CZipContextMenu;
    switch (command.CommandInternalID) {
#ifdef _WIN32
    case C::kCompressEmail:
#endif
    case C::kCompress:
        FOR_VECTOR(i, catalog->nativeCodecs().Formats) {
            const auto &format = catalog->nativeCodecs().Formats[i];
            if (format.UpdateEnabled && !format.Flags_HashHandler())
                return true;
        }
        return false;
#ifdef _WIN32
    case C::kCompressTo7zEmail:
#endif
    case C::kCompressTo7z:
        return catalog->canUpdate("7z");
#ifdef _WIN32
    case C::kCompressToZipEmail:
#endif
    case C::kCompressToZip:
        return catalog->canUpdate("zip");
    case C::kHash_Generate_SHA256:
        return catalog->canUpdate("hash");
    case C::kOpen:
    case C::kExtract:
    case C::kExtractHere:
    case C::kExtractTo:
    case C::kTest:
    case C::kHash_TestArc:
        return true;
    case C::kHash_CRC32:
    case C::kHash_CRC64:
    case C::kHash_XXH64:
    case C::kHash_MD5:
    case C::kHash_SHA1:
    case C::kHash_SHA256:
    case C::kHash_SHA384:
    case C::kHash_SHA512:
    case C::kHash_SHA3_256:
    case C::kHash_BLAKE2SP:
    case C::kHash_All:
        return true;
    default:
        return false; // Commands without an applicable transport stay unavailable.
    }
}

void QtFileManagerShellMenu::Render(QMenu &menu, const NQtShellMenu::CMenuHandle &tree) {
    for (const auto &entry : tree.Items) {
        if (entry.Type == NQtShellMenu::kSeparator) {
            menu.addSeparator();
            continue;
        }
        const unsigned offset = entry.ID - kMenuCmdID_Plugin_Start;
        if (offset >= _context->Commands().Size())
            continue;
        const auto &command = _context->Commands()[offset];
        QAction *action;
        if (entry.SubMenu) {
            auto *child = menu.addMenu(ToQString(entry.Text));
            Render(*child, *entry.SubMenu);
            action = child->menuAction();
            bool available = false;
            for (auto *item : child->actions())
                available |= !item->isSeparator() && item->isEnabled();
            action->setEnabled(available);
        } else {
            action = menu.addAction(ToQString(entry.Text));
            action->setEnabled(Available(command));
            const quint64 generation = _generation;
            QObject::connect(action, &QAction::triggered, &menu, [this, offset, generation] {
                // Deferred deletion keeps the QAction alive while its menu is
                // rebuilt; that old action must not invoke a new context.
                Dispatch(offset + kMenuCmdID_Plugin_Start, generation);
            });
        }
        if (command.CommandInternalID == CZipContextMenu::kOpen) {
            if (command.ArcType.IsEmpty())
                action->setText(QCoreApplication::translate("qtfilemanagershellmenu",
                                                            "Open archive in new window"));
            else if (command.ArcType == UString(L"*"))
                action->setText(QCoreApplication::translate("qtfilemanagershellmenu",
                                                            "Single archive layer (*)"));
            else if (command.ArcType == UString(L"#"))
                action->setText(QCoreApplication::translate("qtfilemanagershellmenu",
                                                            "Parse file as archive (#)"));
            else if (command.ArcType == UString(L"#:e"))
                action->setText(
                    QCoreApplication::translate("qtfilemanagershellmenu", "Scan every byte (#:e)"));
        } else if (command.Verb == UString(L"SevenZip.OpenWithType."))
            action->setText(
                QCoreApplication::translate("qtfilemanagershellmenu", "Open in new window as"));
        action->setProperty("shellCommandID", entry.ID);
        if (entry.Bitmap) {
            QPixmap bitmap(QStringLiteral(":/upstream/7zip/UI/Explorer/MenuLogo.bmp"));
            if (!bitmap.isNull()) {
                bitmap.setMask(
                    bitmap.createMaskFromColor(bitmap.toImage().pixelColor(0, 0), Qt::MaskInColor));
                action->setIcon(QIcon(bitmap));
            }
        }
        action->setProperty("shellVerb", ToQString(command.Verb));
        if (command.Verb == UString(L"SevenZip"))
            action->setObjectName("sevenZipContextGroup");
    }
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
    if (id >= kMenuCmdID_Plugin_Start + 400)
        return _system && _system->CanInvoke(id);
#endif
    if (!_context || id < kMenuCmdID_Plugin_Start)
        return false;
    const unsigned offset = id - kMenuCmdID_Plugin_Start;
    return offset < _context->Commands().Size() && Available(_context->Commands()[offset]) &&
           !_context->Commands()[offset].IsPopup;
}

void QtFileManagerShellMenu::ResetResult() { _services.ResetResult(); }

HRESULT QtFileManagerShellMenu::InvokeCommand(unsigned offset, bool system, const UString &) {
#ifdef _WIN32
    if (system)
        return _system ? _system->Invoke(offset) : E_INVALIDARG;
#endif
    if (system || !_context)
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
bool QtFileManagerShellMenu::ShowSystemMenu() const { return _window.app_.ShowSystemMenu; }

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
    if (indices.IsEmpty() || _window.projectedUi().state != MainWindow::State::Filesystem)
        return;
#ifdef _WIN32
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
            action->setObjectName("systemContextGroup");
        } else
            action->setParent(&_menu);
        _menu.insertAction(_menu.actions().isEmpty() ? nullptr : _menu.actions().first(), action);
    }
    _system = std::move(system);
#else
    (void)panel;
    (void)extended;
    auto *action =
        new QAction(QCoreApplication::translate("qtfilemanagershellmenu", "System"), &_menu);
    action->setObjectName("systemContextGroup");
    action->setEnabled(false);
    _menu.insertAction(_menu.actions().isEmpty() ? nullptr : _menu.actions().first(), action);
#endif
}
