#pragma once

#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/combo/ComboDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashservice.h"
#include "settings/RegistryUtils.h"
#include "support/drag_event_position.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

class MainWindowOperationsTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init();
    void inlineRenameCollisionRestoresSelection();
    void deleteRoutesTrashAndShiftPermanent();
    void sourceFilesystemCreateAndInlineRename();
    void inlineRenameCancel_data();
    void inlineRenameCancel();
    void twoPanelFilesystemCopyRefreshesDestination();
    void twoPanelArchiveCopyRefreshesFilesystemDestination();
    void stage7FilesystemCommands();
    void stage7ArchiveCopyAndDropCancellation();
    void fallbackMoveRetainsOneSettledResult_data();
    void fallbackMoveRetainsOneSettledResult();
    void diskPanelCopiesIntoOppositeArchive();
    void archivePanelsCopyAndMoveThroughStaging_data();
    void archivePanelsCopyAndMoveThroughStaging();
    void damagedArchiveTransferPreservesBothArchives_data();
    void damagedArchiveTransferPreservesBothArchives();
    void protectedArchivePanelsCopyWithStoredPasswords();
    void nestedArchivePanelCannotWriteIntoOppositeArchive();
    void stage7ArchiveCommandStates();
    void extractAskUsesSourceOverwriteDialog();
    void skippedArchiveMoveKeepsSourceAndShowsPartialResult();
    void backendOverwriteDisplaysNativeMetadata_data();
    void backendOverwriteDisplaysNativeMetadata();
    void sourceNoPathsAskFlattensSelectedFolder();
    void encryptedAskUsesSourceCallback_data();
    void nestedFolderAskUsesSourceRelativePath();
    void encryptedAskUsesSourceCallback();
    void movesRealArchiveEntryToDirectory();
    void stage7ArchiveMutations();
    void encryptedArchiveMutationReusesSourcePassword();
    void addToOpenArchiveState();
    void createDialogAndStateTransitions();
};
