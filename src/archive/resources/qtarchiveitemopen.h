#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyString.h"
#include <QString>

class QWidget;
// Source OpenItemInArchive acquisition, with the ordinary joined open dialog.
HRESULT GetArchiveItemStream(IFolderFolder *folder, UInt32 index, ISequentialInStream **stream,
                             QWidget *parent, const QString &settingsPath, bool &passwordDefined,
                             UString &password, bool disableQuestions = false);
