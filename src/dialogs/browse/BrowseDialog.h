// Adapted from 7-Zip CPP/7zip/UI/FileManager/BrowseDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include "platform/filepicker/CommonDialog.h"
class QWidget;
class QString;
bool MyBrowseForFolder(QWidget *owner, const UString &title, const UString &path,
                       UString &resultPath);

QString BrowseForExistingFile(QWidget *owner, const QString &title, const QString &directory,
                              const QString &nameFilters);

// Original BrowseDialog.h filter DTO; Qt translates only the native dialog
// presentation syntax, retaining the source filter order and selected index.
struct CBrowseFilterInfo {
    UStringVector Masks;
    UString Description;
};

struct CBrowseInfo : public NWindows::CCommonDialogInfo {
    bool BrowseForFile(const CObjectVector<CBrowseFilterInfo> &filters);
};
