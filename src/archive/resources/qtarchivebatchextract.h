// Qt entry point for original GUI/ExtractGUI.cpp and Common/Extract.cpp.
#pragma once
#include "Common/MyWindows.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchiveextractstatus.h"
#include <QStringList>

class QWidget;

struct QtArchiveBatchResult {
    HRESULT Result = E_FAIL;
    bool CallbackOK = false;
    bool MessageWasDisplayed = false;
    std::optional<QtArchiveExtractStatus> ExtractStatus;
};

QtArchiveBatchResult RunQtArchiveBatchExtract(ArchiveBackend &backend, const QStringList &archives,
                                              const QString &destination, const QString &password,
                                              const ExtractionOptions &options, QWidget *parent);
