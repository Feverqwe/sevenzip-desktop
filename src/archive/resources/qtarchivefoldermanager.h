// Native and typed metadata services for the source FileFolderPluginOpen worker.
#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "archive/resources/qtarchivestreamopen.h"
#include "ui/progress/qtprogresstransport.h"
#include <QObject>
#include <memory>

class ArchiveBackend;
class ArchiveCodecCatalog;
class CArchiveFolderManager;
struct COpenOptions;
struct CArchiveLink;
class QtArchiveFolderManager final : public QObject,
                                     public IFolderManager,
                                     public IQtProgressServices,
                                     public IQtArchiveStreamOpenServices,
                                     public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderManager)
    struct COpenServices;
    ArchiveBackend &_backend;
    std::shared_ptr<const ArchiveCodecCatalog> _associationCatalog;
    std::unique_ptr<CArchiveFolderManager> _associationManager;
    CArchiveFolderManager *AssociationManager();
    HRESULT WaitForCodecCatalog(std::shared_ptr<const ArchiveCodecCatalog> &catalog,
                                IProgress *progress);
    HRESULT OpenFolderFileImpl(IInStream *stream, const wchar_t *path, const wchar_t *format,
                               IFolderFolder **folder, IProgress *progress, bool ownedStreamFile,
                               const COpenOptions *options = nullptr,
                               bool materializeStream = false);
    HRESULT OpenLinkImpl(CArchiveLink &link, const COpenOptions &options, COpenServices &services);

  public:
    explicit QtArchiveFolderManager(ArchiveBackend &backend);
    ~QtArchiveFolderManager() override;
    Z7_IFACE_COM7_IMP(IFolderManager)
    HRESULT OpenArchive(const COpenOptions &options, const wchar_t *format, IFolderFolder **folder,
                        IProgress *progress);
    HRESULT OpenOwnedStreamFile(IInStream *stream, const wchar_t *path, const wchar_t *format,
                                IFolderFolder **folder, IProgress *progress, bool materialize,
                                bool *materialized) override;
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport(CProgressDialog &dialog) override;
};
