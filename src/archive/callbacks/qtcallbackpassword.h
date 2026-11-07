// GUI-thread presentation boundary for upstream worker password callbacks.
#pragma once
#include "Common/MyString.h"
class CProgressDialog;
bool AskCallbackPassword(CProgressDialog &parent, UString &password, const UString &archive);
bool AskCallbackPassword(CProgressDialog &parent, UString &password, bool &showPassword,
                         const UString &archive, bool opening = false);
bool AskCallbackFilesystemKeys(CProgressDialog &parent, UString &keys);
