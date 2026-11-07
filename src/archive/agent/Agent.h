// Adapted from 7-Zip UI/Agent/Agent.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/UI/Common/UpdateAction.h"
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "archive/agent/AgentProxy.h"
#include "archive/agent/IFolderArchive.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/archivereadstream.h"
#include "common/collections/MyVector.h"
#include "fileops/patterns/Wildcard.h"
#include "platform/loader/DLL.h"
#include "ui/progress/qtprogresstransport.h"
#include <atomic>
#include <optional>

struct CProxyItem {
    unsigned DirIndex;
    unsigned Index;
};

class CAgent;
class CAgentFolder;
class IQtAgentOpenServices;
struct CExtractNtOptions;
enum AGENT_OP {
    AGENT_OP_Uni,
    AGENT_OP_Delete,
    AGENT_OP_CreateFolder,
    AGENT_OP_Rename,
    AGENT_OP_CopyFromFile
};
Z7_PURE_INTERFACES_BEGIN
#define Z7_IFACEM_IArchiveFolderInternal(x)                                                        \
    x(GetAgentFolder(CAgentFolder **agentFolder))                                                  \
        x(UpdateFrom(const wchar_t *fromFolderPath, const wchar_t *const *itemsPaths,              \
                     UInt32 numItems, const NUpdateArchive::CActionSet &actionSet,                 \
                     IProgress *progress))                                                         \
            x(UpdateFromPaths(const wchar_t *const *itemsPaths, UInt32 numItems,                   \
                              const NUpdateArchive::CActionSet &actionSet,                         \
                              NWildcard::ECensorPathMode pathMode, IProgress *progress))
Z7_IFACE_CONSTR_FOLDERARC(IArchiveFolderInternal, 0xC)
Z7_PURE_INTERFACES_END

class CAgentFolder final : public IFolderFolder,
                           public IArchiveLayerFolder,
                           public IArchiveGetStreamWithRequest,
                           public IFolderCompare,
                           public IFolderGetItemName,
                           public IFolderProperties,
                           public IGetFolderArcProps,
                           public IArchiveGetRawProps,
                           public IFolderSetFlatMode,
                           public IFolderAltStreams,
                           public IArchiveFolderInternal,
                           public IInArchiveGetStream,
                           public IFolderOperations,
                           public IArchiveFolder,
                           public IFolderSetZoneIdMode,
                           public IFolderSetZoneIdFile,
                           public IQtProgressServices,
                           public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderFolder)
    Z7_COM_QI_ENTRY(IFolderProperties)
    Z7_COM_QI_ENTRY(IFolderGetItemName)
    Z7_COM_QI_ENTRY(IFolderCompare)
    Z7_COM_QI_ENTRY(IGetFolderArcProps)
    Z7_COM_QI_ENTRY(IArchiveGetRawProps)
    Z7_COM_QI_ENTRY(IFolderSetFlatMode)
    Z7_COM_QI_ENTRY(IFolderAltStreams)
    Z7_COM_QI_ENTRY(IArchiveFolderInternal)
    Z7_COM_QI_ENTRY(IInArchiveGetStream)
    Z7_COM_QI_ENTRY(IArchiveLayerFolder)
    Z7_COM_QI_ENTRY(IArchiveGetStreamWithRequest)
    else if (iid == IID_IFolderOperations && (CanCopyFromFile() || CanCopyTo())) {
        *outObject = static_cast<IFolderOperations *>(this);
    }
    else if (iid == IID_IArchiveFolder && CanCopyTo()) {
        *outObject = static_cast<IArchiveFolder *>(this);
    }
    Z7_COM_QI_ENTRY(IFolderSetZoneIdMode)
    Z7_COM_QI_ENTRY(IFolderSetZoneIdFile)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    CAgentFolder() = default;
    // Borrowed while the source modal CopyTo worker is joined. UI supplies
    // only metadata choices and the integration path policy.
    const CExtractNtOptions *QtExtractNtOptions = nullptr;
    Z7_IFACE_COM7_IMP(IFolderFolder)
    Z7_COM7F_IMP(GetContainerDepth(UInt64 *depth))
    Z7_COM7F_IMP(SetParentContainerDepth(UInt64 depth))
    Z7_COM7F_IMP(GetStreamWithRequest(UInt32 index, ISequentialInStream **stream,
                                      IProgress *request))
    Z7_IFACE_COM7_IMP(IFolderGetItemName)
    Z7_IFACE_COM7_IMP(IFolderCompare)
    Z7_IFACE_COM7_IMP(IFolderProperties)
    Z7_IFACE_COM7_IMP(IGetFolderArcProps)
    Z7_IFACE_COM7_IMP(IArchiveGetRawProps)
    Z7_IFACE_COM7_IMP(IFolderSetFlatMode)
    Z7_IFACE_COM7_IMP(IFolderAltStreams)
    Z7_IFACE_COM7_IMP(IArchiveFolderInternal)
    Z7_IFACE_COM7_IMP(IInArchiveGetStream)
    Z7_IFACE_COM7_IMP(IFolderOperations)
    Z7_IFACE_COM7_IMP(IArchiveFolder)
    Z7_IFACE_COM7_IMP(IFolderSetZoneIdMode)
    Z7_IFACE_COM7_IMP(IFolderSetZoneIdFile)
    void Init(const CProxyArc *proxy, const CProxyArc2 *proxy2, unsigned proxyDirIndex,
              CAgent *agent);
    HRESULT BindToFolder_Internal(unsigned proxyDirIndex, IFolderFolder **resultFolder);
    HRESULT BindToAltStreams_Internal(unsigned proxyDirIndex, IFolderFolder **resultFolder);
    int GetRealIndex(unsigned index) const;
    UString GetName(UInt32 index) const;
    UString GetFullPrefix(UInt32 index) const;
    void GetRealIndices(const UInt32 *indices, UInt32 numItems, bool includeAltStreams,
                        bool includeFolderSubItemsInFlatMode, CUIntVector &realIndices) const;
    unsigned GetNumberOfItems() const;
    CProxyItem GetItem(unsigned index) const;
    // Read-only metadata boundary for original archive-index Path queries.
    IInArchive *GetArchiveProperties() const;
    // Resolve archive-index paths with the same defaults as source extraction.
    HRESULT GetArchiveItemPath(UInt32 index, UString &path) const;
    // Borrowed engine service for Qt presentation; the source agent retains it.
    IUnknown *GetEngineResource() const;
    bool DirectoryItemsNeedLoad(bool recursive) const;
    HRESULT LoadDirectoryItems(bool recursive, IProgress *progress = nullptr);
    HRESULT LoadDirectorySelection(const UInt32 *indices, UInt32 count, IProgress *progress);
    bool DirectoryItemSubtreeComplete(UInt32 nativeIndex) const;
    // File-only refresh bridge; native ReOpen and folder binding retain ownership.
    HRESULT ReOpen(IArchiveOpenCallback *callback, IFolderFolder **folder);
    HRESULT CloseArchive();
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport(CProgressDialog &dialog) override;
    void GetPathParts(UStringVector &pathParts, bool &isAltStreamFolder);
    HRESULT CommonUpdateOperation(AGENT_OP operation, bool moveMode, const wchar_t *newItemName,
                                  const NUpdateArchive::CActionSet *actionSet,
                                  const UInt32 *indices, UInt32 numItems, IProgress *progress);

  private:
    friend class CAgent;
    void RestorePath(const UStringVector &pathParts, bool isAltStreamFolder);
    void LoadFolder(unsigned dirIndex);
    void GetPrefix(UInt32 index, UString &prefix) const;
    int CompareItems3(UInt32 index1, UInt32 index2, PROPID propID);
    int CompareItems2(UInt32 index1, UInt32 index2, PROPID propID, Int32 propIsRaw);
    int ComparePrefixes(UInt32 index1, UInt32 index2);
    UString _temp1, _temp2;
    bool IsBound() const;
    bool CanCopyFromFile() const;
    bool CanCopyTo() const;
    const CProxyArc *_proxy = nullptr;
    const CProxyArc2 *_proxy2 = nullptr;
    UInt64 _proxyGeneration = 0;
    struct CClosedArchivePath {
        UStringVector Parts;
        UString Prefix;
        bool IsAltStreamFolder = false;
    };
    std::optional<CClosedArchivePath> _closedArchivePath;
    bool _isAltStreamFolder = false;
    unsigned _proxyDirIndex = 0;
    bool _flatMode = false;
    bool _loadAltStreams = false;
    NExtract::NZoneIdMode::EEnum _zoneMode = NExtract::NZoneIdMode::kNone;
    CByteBuffer _zoneBuf;
    CRecordVector<CProxyItem> _items;
    // Typed metadata boundary replaces the in-process archive property call.
    UString _archiveType;
    CMyComPtr<IInFolderArchive> _agent;
    CAgent *_agentSpec = nullptr;
};

// Source archive owner; opening/extraction transport stays behind ArchiveBackend.
class CAgent final : public IInFolderArchive,
                     public IFolderArcProps,
                     public ISetProperties,
                     public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_3(IInFolderArchive, IFolderArcProps, ISetProperties)
  public:
    Z7_IFACE_COM7_IMP(IInFolderArchive)
    Z7_IFACE_COM7_IMP(IFolderArcProps)
    Z7_IFACE_COM7_IMP(ISetProperties)
    explicit CAgent(IQtAgentOpenServices *openServices = nullptr);
    ~CAgent();
    HRESULT Init(IFolderFolder *archiveProps, const UString &archiveType,
                 IUnknown *engineResource = nullptr);
    CProxyArc *_proxy;
    CProxyArc2 *_proxy2;
    CArchiveLink _archiveLink;
    UString _updatePathPrefix;
    bool _updatePathPrefix_is_AltFolder;
    bool _isDeviceFile;
    bool _isHashHandler = false;
    CAgentFolder *_agentFolder;
    UString ArchiveType;
    FStringVector _names;
    FString _folderPrefix; // for new files from disk
    std::optional<NWildcard::ECensorPathMode> GuiPathMode;
    CObjectVector<UString> m_PropNames;
    CObjectVector<NWindows::NCOM::CPropVariant> m_PropValues;
    UString _archiveFilePath;
    // INVALID_FILE_ATTRIBUTES has no non-Windows header definition.
    DWORD _attrib = (DWORD)-1;
    FString _hashBaseFolderPrefix;
    bool ThereIsPathProp = false;
    const CArc &GetArc() const { return _archiveLink.Arcs.Back(); }
    UString GetTypeOfArc(const CArc &arc) const;
    IInArchive *GetArchive() const {
        if (_archiveLink.Arcs.IsEmpty())
            return NULL;
        return GetArc().Archive;
    }
    bool CanUpdate() const;
    bool Is_Attrib_ReadOnly() const {
        return _attrib != (DWORD)-1 && (_attrib & FILE_ATTRIBUTE_READONLY);
    }
    bool IsThere_ReadOnlyArc() const;
    void KeepModeForNextOpen() { _archiveLink.KeepModeForNextOpen(); }
    HRESULT SetFolder(IFolderFolder *folder);
    HRESULT SetFiles(const wchar_t *folderPrefix, const wchar_t *const *names, UInt32 numNames);
    HRESULT DoOperation(FStringVector *requestedPaths, FStringVector *processedPaths,
                        CCodecs *codecs, int formatIndex, ISequentialOutStream *outArchiveStream,
                        const Byte *stateActions, const wchar_t *sfxModule,
                        IFolderArchiveUpdateCallback *updateCallback100) throw();
    HRESULT CommonUpdate(ISequentialOutStream *outArchiveStream, unsigned numUpdateItems,
                         IArchiveUpdateCallback *updateCallback);
    HRESULT DeleteItems(ISequentialOutStream *outArchiveStream, const UInt32 *indices,
                        UInt32 numItems, IFolderArchiveUpdateCallback *updateCallback100);
    HRESULT CreateFolder(ISequentialOutStream *outArchiveStream, const wchar_t *folderName,
                         IFolderArchiveUpdateCallback *updateCallback100);
    HRESULT RenameItem(ISequentialOutStream *outArchiveStream, const UInt32 *indices,
                       UInt32 numItems, const wchar_t *newItemName,
                       IFolderArchiveUpdateCallback *updateCallback100);
    HRESULT UpdateOneFile(ISequentialOutStream *outArchiveStream, const UInt32 *indices,
                          UInt32 numItems, const wchar_t *diskFilePath,
                          IFolderArchiveUpdateCallback *updateCallback100);
    // External engine stream/file lifetime replaces the in-process archive's
    // stream ownership. All folders retain this source CAgent owner.
    void AttachEngineResource(IUnknown *resource) { _engineResource = resource; }
    IUnknown *GetEngineResource() const { return _engineResource; }
    IArchiveOpenCallback *_progress_ArchiveOpenCallback_for_Open = nullptr;
    IProgress *_progress_for_Open = nullptr;

  private:
    friend class CAgentFolder;
    std::atomic<UInt64> _proxyGeneration{0};
    HRESULT ReadItems();
    HRESULT RefreshDirectoryProxy(IProgress *progress);
    CMyComPtr<IUnknown> _engineResource;
    IQtAgentOpenServices *_openServices;
    const CCodecs *_openCodecs = nullptr;
};

// ArchiveFolderOpen's source factory and icon/association owner. The retained
// QtArchiveFolderManager supplies engine services to the opening worker.
struct CCodecIcons {
    struct CIconPair {
        UString Ext;
        int IconIndex;
    };
    CObjectVector<CIconPair> IconPairs;
    void LoadIcons(HMODULE m);
    bool FindIconIndex(const UString &ext, int &iconIndex) const;
};

class CArchiveFolderManager {
    const CCodecs *_codecs;
    IQtAgentOpenServices *_openServices = nullptr;
    CObjectVector<CCodecIcons> CodecIconsVector;
    CCodecIcons InternalIcons;
    bool WasLoaded = false;
    void LoadFormats();

  public:
    explicit CArchiveFolderManager(const CCodecs &codecs) : _codecs(&codecs) {}
    explicit CArchiveFolderManager(IQtAgentOpenServices &services)
        : _codecs(nullptr), _openServices(&services) {}
    HRESULT OpenFolderFile(IInStream *inStream, const wchar_t *filePath, const wchar_t *arcFormat,
                           IFolderFolder **resultFolder, IProgress *progress);
    HRESULT GetExtensions(BSTR *extensions);
    HRESULT GetIconPath(const wchar_t *ext, BSTR *iconPath, Int32 *iconIndex);
};
