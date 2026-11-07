#pragma once
#include "Common/MyString.h"
#include <QString>

void QtInitializeLinkPlatform();
bool QtIsFileSystemLink(const QString &path);
bool QtRenameFileSystemEntry(const QString &source, const QString &destination);
bool QtRenameFileSystemEntryWithoutReplacing(const QString &source, const QString &destination);
// Destination is a new entry in the caller's private transaction stage.
bool QtCopyFileSystemLink(const QString &source, const QString &destination);
bool QtRemoveFileSystemEntry(const QString &path);
