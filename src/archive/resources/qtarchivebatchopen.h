// File-only native opener for the source Common/Extract.cpp archive loop.
#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "archive/opening/OpenArchive.h"
#include "ui/progress/qtprogresstransport.h"
#include <QString>
#include <memory>

class ArchiveBackend;
class QtArchiveFolderManager;
class CProgressDialog;
struct ExtractionOptions;

class QtArchiveBatchOpen final : public IQtArchiveOpenService, public IQtProgressServices {
    ArchiveBackend &_backend;
    CMyComPtr2<IFolderManager, QtArchiveFolderManager> _manager;
    QString _destination;
    const ExtractionOptions *_extractOptions = nullptr;
    UString _openType;
    CProgressDialog *_dialog = nullptr; // Borrowed until the joined extraction finishes.

  public:
    explicit QtArchiveBatchOpen(ArchiveBackend &backend);
    ~QtArchiveBatchOpen() override;
    void SetExtractionPolicy(const QString &destination, const ExtractionOptions &options);
    void SetOpenType(const UString &type) { _openType = type; }
    HRESULT Open(CArchiveLink &link, const COpenOptions &options,
                 IOpenCallbackUI *callback) override;
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport(CProgressDialog &dialog) override;
};
