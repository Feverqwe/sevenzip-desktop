#pragma once
#include "Common/MyVector.h"
#include "Common/MyWindows.h"
#include <QString>
class CAgentFolder;
class QWidget;
HRESULT ReadArchiveDirectory(CAgentFolder &folder, bool recursive, QWidget *parent,
                             const QString &settingsPath);
HRESULT CalculateArchiveDirectorySizes(CAgentFolder &folder, const CUIntVector &indices,
                                       QWidget *parent, const QString &settingsPath);
