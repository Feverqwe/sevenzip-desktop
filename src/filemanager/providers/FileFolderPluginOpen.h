// Adapted from 7-Zip CPP/7zip/UI/FileManager/FileFolderPluginOpen.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_FILE_FOLDER_PLUGIN_OPEN_H
#define ZIP7_INC_FILE_FOLDER_PLUGIN_OPEN_H

#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "archive/password/archivepassword.h"
#include "platform/loader/DLL.h"
#include <QWidget>

struct CFfpOpen {
    Z7_CLASS_NO_COPY(CFfpOpen)
  public:
    // out:
    bool Encrypted;
    NativePassword Password;
    bool OwnedStreamFile = false;
    bool MaterializeStreamFile = false;
    bool MaterializedStreamFile = false;

    NWindows::NDLL::CLibrary Library;
    CMyComPtr<IFolderFolder> Folder;
    UString ErrorMessage;

    // Typed engine transport replaces construction of the in-process manager.
    CMyComPtr<IFolderManager> FolderManager;
    QString SettingsPath;
    explicit CFfpOpen(IFolderManager *manager) : Encrypted(false), FolderManager(manager) {}

    HRESULT OpenFileFolderPlugin(IInStream *inStream, const FString &path, const UString &arcFormat,
                                 QWidget *parentWindow);
};

#endif
