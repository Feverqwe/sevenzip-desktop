// Worker-to-GUI backend requests used by native archive callbacks.
#pragma once
#include "Common/MyWindows.h"
#include <QList>
#include <functional>
class ArchiveBackend;
struct ArchiveInfo;
struct ArchiveEntry;
class QString;
class CProgressSync;
class IUnknown;
HRESULT RunArchiveBackendRequest(ArchiveBackend &backend, const std::function<void()> &start,
                                 IUnknown *passwordCallback, CProgressSync *sync);
HRESULT ReadArchiveBackendListing(ArchiveBackend &backend, const QString &path,
                                  const QString &format, const QString &password,
                                  IUnknown *passwordCallback, CProgressSync *sync,
                                  ArchiveInfo &info, QList<ArchiveEntry> &entries);
