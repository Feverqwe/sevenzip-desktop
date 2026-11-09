#pragma once
#include "Common/MyString.h"
#include <QString>
#include <functional>

QString QtAvailableRenamePath(const QString &target,
                              const std::function<bool(const QString &)> &reserved = {});
bool QtAutoRenamePath(FString &path);
