#pragma once
#include <QString>
// OS object identity without following links; empty means unavailable or a link.
QString QtOwnedTempIdentity(const QString &path);
// Compatibility with existing Linux registry records and lease keys.
QString QtOwnedTempLegacyIdentity(const QString &path);
bool QtOwnedTempMatchesIdentity(const QString &path, const QString &identity);
