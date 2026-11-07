// Qt projection of the native Explorer menu tree; invocation stays with its owner.
#include "archive/codecs/archivecodeccatalog.h"
#include "filemanager/panels/panelmenuids.h"
#include "filemanager/qtfilemanagershellmenu.h"
#include "filemanager/window/mainwindow.h"
#include "settings/ViewSettings.h"
#include <QBitmap>
#include <QMenu>

bool QtFileManagerShellMenu::Exposed(const CZipContextMenu::CCommandMapItem &command) {
    using C = CZipContextMenu;
    switch (command.CommandInternalID) {
    case C::kOpen:
        return command.ArcType != UString(L"*") && !command.ArcType.IsPrefixedBy(L"#");
    case C::kHash_Generate_SHA256:
    case C::kHash_TestArc:
        return false;
    default:
        return true;
    }
}

bool QtFileManagerShellMenu::Available(const CZipContextMenu::CCommandMapItem &command) const {
    if (!Exposed(command))
        return false;
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
    case C::kOpen:
    case C::kExtract:
    case C::kExtractHere:
    case C::kExtractTo:
    case C::kTest:
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
        return true;
    default:
        return false; // Commands without an applicable transport stay unavailable.
    }
}

void QtFileManagerShellMenu::Render(QMenu &menu, const NQtShellMenu::CMenuHandle &tree) {
    QPixmap bitmap(QStringLiteral(":/upstream/7zip/UI/Explorer/MenuLogo.bmp"));
    if (!bitmap.isNull())
        bitmap.setMask(
            bitmap.createMaskFromColor(bitmap.toImage().pixelColor(0, 0), Qt::MaskInColor));
    RenderItems(menu, tree, QIcon(bitmap));
}

void QtFileManagerShellMenu::RenderItems(QMenu &menu, const NQtShellMenu::CMenuHandle &tree,
                                         const QIcon &icon) {
    bool pendingSeparator = false;
    for (const auto &entry : tree.Items) {
        if (entry.Type == NQtShellMenu::kSeparator) {
            pendingSeparator = !menu.actions().isEmpty();
            continue;
        }
        const unsigned offset = entry.ID - kMenuCmdID_Plugin_Start;
        if (offset >= _context->Commands().Size())
            continue;
        const auto &command = _context->Commands()[offset];
        if (!Exposed(command))
            continue;
        if (pendingSeparator) {
            menu.addSeparator();
            pendingSeparator = false;
        }
        QAction *action;
        if (entry.SubMenu) {
            auto *child = menu.addMenu(ToQString(entry.Text));
            RenderItems(*child, *entry.SubMenu, icon);
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
        } else if (command.Verb == UString(L"SevenZip.OpenWithType."))
            action->setText(
                QCoreApplication::translate("qtfilemanagershellmenu", "Open in new window as"));
        action->setProperty("shellCommandID", entry.ID);
        if (entry.Bitmap && !icon.isNull())
            action->setIcon(icon);
        action->setProperty("shellVerb", ToQString(command.Verb));
        if (command.Verb == UString(L"SevenZip"))
            action->setObjectName("sevenZipContextGroup");
    }
}
