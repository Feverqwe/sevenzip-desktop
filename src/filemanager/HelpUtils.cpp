// Adapted from 7-Zip CPP/7zip/UI/FileManager/HelpUtils.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/HelpUtils.h"
#include "help/qthelpservices.h"

void ShowHelpWindow(const char *topicFile, QWidget *parent) {
    // Qt's bundled HTML service takes the source topic directly; the native
    // module path and CHM member syntax belong to HtmlHelp/hh.exe.
    QtOpenHelpTopic(QString::fromUtf8(topicFile), parent);
}
