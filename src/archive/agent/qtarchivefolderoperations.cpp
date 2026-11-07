// Qt operation capabilities and progress adaptation for the native archive folder.
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"

std::unique_ptr<IQtProgressTransport>
CAgentFolder::CreateProgressTransport(CProgressDialog &dialog) {
    auto *resource = dynamic_cast<QtArchiveFileResource *>(_agentSpec->GetEngineResource());
    return resource ? resource->CreateProgressTransport(
                          dialog, ArchiveUsesNativeItemIndexes(_agentSpec->GetArchive()))
                    : nullptr;
}

bool CAgentFolder::CanCopyFromFile() const {
    if (!_agentSpec || !_agentSpec->CanUpdate())
        return false;
    auto *resource = dynamic_cast<QtArchiveFileResource *>(_agentSpec->GetEngineResource());
    return resource && resource->HasUpdateService();
}

bool CAgentFolder::CanCopyTo() const {
    return _agentSpec && ArchiveHasExtractService(_agentSpec->GetArchive());
}

// Remaining unavailable operation bodies stay with their source owners.
Z7_COM7F_IMF(CAgentFolder::CreateFile(const wchar_t *, IProgress *)) { return E_NOTIMPL; }
