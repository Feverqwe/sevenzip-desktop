#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"

class QWidget;
namespace NWindows {
class CProcess;
}

// Retain the original association launch and its optional native process handle.
HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window,
                         NWindows::CProcess &process);
void StartApplicationDontWait(const UString &dir, const UString &path, QWidget *window);
