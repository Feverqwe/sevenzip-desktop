// Adapted from 7-Zip CPP/7zip/UI/FileManager/Panel.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Bound source IFolder owns metadata and paths; Qt supplies list controls and
// narrow presentation/operation adapters around the partial source panel.
#pragma once

#include "7zip/Archive/IArchive.h"
#include "7zip/PropID.h"
#include "7zip/UI/Common/ExtractMode.h"
#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyBuffer.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "Common/MyTypes.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchiveextractstatus.h"
#include "archive/resources/qtarchiveupdatepublication.h"
#include "common/collections/MyVector.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/qtpanellist.h"
#include "fileops/native/FileDir.h"
#include "fileops/patterns/Wildcard.h"
#include "platform/loader/DLL.h"
#include "platform/process/qtexternalprocess.h"
#include "platform/temp/qtownedtempregistry.h"
#include "settings/ViewSettings.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
class IQtCompressCallServices;
struct CPanelItemTotals;
#include <optional>

#ifdef _WIN32
#define ROOT_FS_FOLDER L"C:\\"
#else
#define ROOT_FS_FOLDER L"/"
#endif

struct CSelectedState;
struct CExitEventLauncher;
class QColor;
class QIcon;
struct CExternalCodecs;

struct CAppState;
class CFastFolders;
class CListViewDialog;
class QtPropertyReport;
struct CHashBundle;
class CPanel;
class CApp;
struct QtPanelKeyDown;
class CMyComboBoxEdit {
  public:
    CPanel *_panel = nullptr;
    bool OnMessage(const QtPanelKeyDown &key, CApp &app);
};
class IQtPanelDragServices;
class QWidget;
class CVirtFileSystem;
class CZipContextMenu;
class IQtContextMenuServices;
class IQtPanelAddressCombo;
struct QtAddressSegment;
enum class QtAddressCommand;
enum class QtAddressEditReason;
#ifdef _WIN32
struct IContextMenu;
#endif
namespace NQtShellMenu {
struct CMenuHandle;
}
namespace NUpdateArchive {
struct CActionSet;
}

class CPanelCallback {
  public:
    virtual void OnTab() = 0;
    virtual void SetFocusToPath(unsigned index) = 0;
    virtual void OnCopy(bool move, bool copyToSame) = 0;
    virtual void OnSetSameFolder() = 0;
    virtual void OnSetSubFolder() = 0;
    virtual void PanelWasFocused() = 0;
    virtual void DragBegin() = 0;
    virtual void DragEnd() = 0;
    virtual void RefreshTitle(bool always) = 0;
};

struct CCopyToOptions {
    bool streamMode;
    bool moveMode;
    bool testMode;
    bool extractMode;
    bool includeAltStreams;
    bool replaceAltStreamChars;
    bool showErrorMessages;
    bool DisableUserQuestions = false;
    bool NeedRegistryZone;
    NExtract::NZoneIdMode::EEnum ZoneIdMode;
    CByteBuffer ZoneBuf;
    UString folder;
    UString TestScope;
    NExtract::NPathMode::EEnum pathMode;
    NExtract::NOverwriteMode::EEnum overwriteMode;
    UStringVector hashMethods;
    // Borrowed only while source CopyTo joins its worker; the caller retains the catalog.
    const CExternalCodecs *ExternalCodecs = nullptr;
    std::function<bool(const CHashBundle &)> PresentHashResult;
    CVirtFileSystem *VirtFileSystemSpec;
    CCopyToOptions()
        : streamMode(false), moveMode(false), testMode(false), extractMode(false),
          includeAltStreams(true), replaceAltStreamChars(false), showErrorMessages(false),
          NeedRegistryZone(true), ZoneIdMode(NExtract::NZoneIdMode::kNone),
          pathMode(NExtract::NPathMode::kCurPaths), overwriteMode(NExtract::NOverwriteMode::kAsk),
          VirtFileSystemSpec(NULL) {}
};

struct CPanelProperty {
    UString Name;
    UString Value;
};

// Native selection summary; presentation retains unknown sizes as unknown.
struct CPanelStatus {
    int selectedCount = 0;
    int totalCount = 0;
    std::optional<UInt64> selectedSize;
};

struct CPanelItem {
    QString RelPath;
    bool IsFolder = false;
    bool SizeDefined = false;
    UInt64 Size = 0;
    UInt64 PackSize = 0;
    bool PackSizeDefined = true;
    UInt64 NumSubDirs = 0;
    UInt64 NumSubFiles = 0;
    CObjectVector<CPanelProperty> Properties;
    CObjectVector<CPanelProperty> ArchiveProperties;
    std::optional<UInt32> Crc;
    QMap<PROPID, QByteArray> RawProperties;
    std::optional<NWindows::NCOM::CPropVariant> MTime;
};

struct CSelectedState {
    int FocusedItem;
    bool SelectFocused;
    bool FocusedName_Defined;
    bool PreserveAddress = false;
    bool KeepViewport = true;
    UString FocusedName;
    UStringVector SelectedNames;

    CSelectedState() : FocusedItem(-1), SelectFocused(true), FocusedName_Defined(false) {}
};

struct CTempFileInfo {
    // Narrow ownership adapter, copied with native nested/process temp records.
    std::shared_ptr<QtOwnedTempLease> OwnedTempLease;
    UInt32 FileIndex; // index of file in folder
    bool NeedDelete;
    UString RelPath; // Relative path of file from Folder
    FString FolderPath;
    FString FilePath;
    NWindows::NFile::NFind::CFileInfo FileInfo;

    CTempFileInfo() : FileIndex((UInt32)(Int32)-1), NeedDelete(false) {}
    void DeleteDirAndFile() const {
        if (!NeedDelete)
            return;
        const bool ownedDirectory =
            !OwnedTempLease || OwnedTempLease->MatchesObjectAt(ToQString(fs2us(FolderPath)));
        if (!ownedDirectory && !OwnedTempLease->MatchesObjectAt(ToQString(fs2us(FilePath))))
            return;
        const auto remove = [&] {
            NWindows::NFile::NDir::DeleteFileAlways(FilePath);
            if (ownedDirectory)
                NWindows::NFile::NDir::RemoveDir(FolderPath);
        };
        if (OwnedTempLease)
            OwnedTempLease->RemoveIfUnused(ToQString(fs2us(ownedDirectory ? FolderPath : FilePath)),
                                           remove);
        else
            remove();
    }
    bool WasChanged_from_TempFileInfo(const NWindows::NFile::NFind::CFileInfo &newFileInfo) const {
        return newFileInfo.Size != FileInfo.Size ||
               Compare_FiTime(&newFileInfo.MTime, &FileInfo.MTime) != 0;
    }
};

class QtPanelLocationTransaction;

struct CFolderLink : public CTempFileInfo {
    bool KeepOnOutdatedClose = false;
    bool IsVirtual; // == true (if archive was open via IInStream):
                    //    archive was open from another archive,
                    //    archive size meets the size conditions derived from g_RAM_Size.
                    //    VirtFileSystem was used
                    //    archive was fully extracted to memory.
    bool UsePassword;
    NWindows::NDLL::CLibrary Library;
    CMyComPtr<IFolderFolder>
        ParentFolder;         // can be NULL, if parent is FS folder (in _parentFolders[0])
    UString ParentFolderPath; // including tail slash (doesn't include paths parts of parent in next
                              // level)
    ArchivePassword Password;
    UString VirtualPath; // without tail slash
    CByteBuffer ZoneBuf; // ZoneBuf for virtaul stream (IsVirtual)

    CFolderLink() : IsVirtual(false), UsePassword(false) {}
    bool WasChanged_from_FolderLink(const NWindows::NFile::NFind::CFileInfo &newFileInfo) const {
        // we call it, if we have two real files.
        // if archive was virtual, it means that we have updated that virtual to real file.
        return IsVirtual || CTempFileInfo::WasChanged_from_TempFileInfo(newFileInfo);
    }
};

struct COpenResult {
    bool ArchiveIsOpened;
    bool Encrypted;
    UString ErrorMessage;
    COpenResult() : ArchiveIsOpened(false), Encrypted(false) {}
};

struct CPropColumn {
    int Order;
    PROPID ID;
    VARTYPE Type;
    bool IsVisible;
    bool IsRawProp;
    UInt32 Width;
    UString Name;

    bool IsEqualTo(const CPropColumn &a) const {
        return Order == a.Order && ID == a.ID && Type == a.Type && IsVisible == a.IsVisible &&
               IsRawProp == a.IsRawProp && Width == a.Width && Name == a.Name;
    }
    int Compare(const CPropColumn &a) const { return MyCompare(Order, a.Order); }
    int Compare_NameFirst(const CPropColumn &a) const {
        if (ID == kpidName) {
            if (a.ID != kpidName)
                return -1;
        } else if (a.ID == kpidName)
            return 1;
        return MyCompare(Order, a.Order);
    }
};

Qt::Alignment GetColumnAlign(PROPID propID, VARTYPE varType);

class CPropColumns : public CObjectVector<CPropColumn> {
  public:
    int FindItem_for_PropID(PROPID id) const {
        FOR_VECTOR(i, (*this))
        if ((*this)[i].ID == id)
            return (int)i;
        return -1;
    }
    bool IsEqualTo(const CPropColumns &props) const {
        if (Size() != props.Size())
            return false;
        FOR_VECTOR(i, (*this))
        if (!(*this)[i].IsEqualTo(props[i]))
            return false;
        return true;
    }
};

class CPanel : public QObject {
  public:
    CPanel();
    ~CPanel() override;
    CPanel(const QString &prefix, const CObjectVector<CPanelItem> &items);
    explicit CPanel(IFolderFolder *folder);
    void ReleasePanel();
    UString GetResumeFolderPath() const;
    void ExitExternalWorkers(bool hardExit);
    bool HasExternalWorkers() const;
    void RebindWindowHost(QWidget *window, CPanelCallback *callback, CAppState *state);
    void AttachAddressFocus(std::function<void()> focus) {
        _addressFocusPresentation = std::move(focus);
    }
    void AttachFileClipboard(std::function<void(unsigned)> command) {
        _fileClipboardPresentation = std::move(command);
    }
    HRESULT Create(QWidget *mainWindow, QWidget *parentWindow, UINT id,
                   const UString &currentFolderPrefix, const UString &arcFormat,
                   CPanelCallback *panelCallback, CAppState *appState, bool needOpenArc,
                   COpenResult &openRes);
    bool OnCreate();
    // Replaces CreateEx and Win32 child-control construction, not path binding.
    void AttachCreationPresentation(std::function<bool(CPanel &)> create) {
        _createPresentation = std::move(create);
    }
    void AttachReloadPresentation(std::function<HRESULT(const CSelectedState &)> reload) {
        _reloadPresentation = std::move(reload);
    }
    void AttachArchivePublication(std::function<void(const QString &, const CPanel *)> publish) {
        _archivePublication = std::move(publish);
    }
    void AttachInvalidationPresentation(std::function<void()> invalidate) {
        _invalidatePresentation = std::move(invalidate);
    }
    void AttachStatusPresentation(std::function<void(const CPanelStatus &)> status) {
        _statusPresentation = std::move(status);
    }
    void AttachViewModePresentation(std::function<void(UInt32)> view) {
        _viewModePresentation = std::move(view);
    }
    void InvalidateList() {
        if (_invalidatePresentation)
            _invalidatePresentation();
        _selectionList.Invalidate();
    }
    bool PanelCreated = false;
    bool AutoRefresh_Mode = true;
    void Set_AutoRefresh_Mode(bool mode) { AutoRefresh_Mode = mode; }
    void MarkPortableFolderChanged() { _portableFolderChanged = true; }
    void OnTimer();
    void OnReload(bool preserveAddress = false);
    // Qt retains the source caller's error policy across bounded row batches.
    std::function<void(HRESULT)> GetReloadCompletion() const { return _reloadCompletion; }
    HRESULT RefreshListCtrl_SaveFocused(bool preserveAddress = false);
    UInt32 _listViewMode = 3;
    int _xSize = 0;
    PROPID _sortID = 0;
    bool _ascending = true;
    Int32 _isRawSortProp = 0;
    int m_DropHighlighted_SelectionIndex = -1;
    UString m_DropHighlighted_SubFolderName;
    static bool GetDefaultSortAscending(PROPID propID);
    void SetSortRawStatus();
    void SortItemsWithPropID(PROPID propID);
    void OnColumnClick(PROPID propID);
    bool _showDots = false;
    UInt32 GetListViewMode() const { return _listViewMode; }
    void SetListViewMode(UInt32 index);
    void ChangeFlatMode();
    PROPID GetSortID() const { return _sortID; }
    bool GetFlatMode() const { return _flatMode; }
    // Qt notification adapters replace the Win32 OnNotify entry point.
    bool ShouldProcessNotifications() const { return _processNotify; }
    bool ShouldDisplayItemText() const { return !_dontShowMode; }
    void Disable_Processing_Timer_Notify_StatusBar() {
        _processTimer = false;
        _processNotify = false;
        _processStatusBar = false;
    }

    class CDisableTimerProcessing {
        Z7_CLASS_NO_COPY(CDisableTimerProcessing)
        bool _processTimer;
        CPanel &_panel;

      public:
        CDisableTimerProcessing(CPanel &panel Z7_lifetimebound) : _panel(panel) { Disable(); }
        ~CDisableTimerProcessing() { Restore(); }
        void Disable() {
            _processTimer = _panel._processTimer;
            _panel._processTimer = false;
        }
        void Restore() { _panel._processTimer = _processTimer; }
    };

    class CDisableTimerProcessing2 {
        Z7_CLASS_NO_COPY(CDisableTimerProcessing2)
        bool _processTimer;
        CPanel *_panel;

      public:
        CDisableTimerProcessing2(CPanel *panel Z7_lifetimebound)
            : _processTimer(true), _panel(panel) {
            Disable();
        }
        ~CDisableTimerProcessing2() { Restore(); }
        void Disable() {
            if (_panel) {
                _processTimer = _panel->_processTimer;
                _panel->_processTimer = false;
            }
        }
        void Restore() {
            if (_panel) {
                _panel->_processTimer = _processTimer;
                _panel = NULL;
            }
        }
    };

    class CDisableNotify {
        Z7_CLASS_NO_COPY(CDisableNotify)
        bool _processNotify;
        bool _processStatusBar;
        CPanel &_panel;

      public:
        CDisableNotify(CPanel &panel Z7_lifetimebound) : _panel(panel) { Disable(); }
        ~CDisableNotify() { Restore(); }
        void Disable() {
            _processNotify = _panel._processNotify;
            _processStatusBar = _panel._processStatusBar;
            _panel._processNotify = false;
            _panel._processStatusBar = false;
        }
        void SetMemMode_Enable() {
            _processNotify = true;
            _processStatusBar = true;
        }
        void Restore() {
            _panel._processNotify = _processNotify;
            _panel._processStatusBar = _processStatusBar;
        }
    };
    void ReleaseFolder();
    void AttachPanelCallback(CPanelCallback *callback) { _panelCallback = callback; }
    void AttachExternalFileServices(IQtExternalFileServices *services) {
        _externalFileServices = services;
    }
    void AttachDragServices(IQtPanelDragServices *services) { _dragServices = services; }
    void OnDrag(bool isRightButton);
    HRESULT CompressDropFiles(const UStringVector &filePaths, const UString &folderPath,
                              bool createNewArchive, bool moveMode, UInt32 sourceFlags,
                              UInt32 &targetFlags,
                              std::optional<CSelectedState> *refreshSelection = nullptr,
                              bool *published = nullptr);
    HRESULT CopyFsItems(CCopyToOptions &options, const UStringVector &filePaths,
                        UStringVector *messages) {
        bool usePassword = false;
        NativePassword password;
        CRecordVector<UInt32> indices;
        return CopyTo(options, indices, messages, usePassword, password, &filePaths);
    }
    HRESULT CopyFromNoAsk(bool moveMode, const UStringVector &filePaths,
                          std::optional<CSelectedState> *refreshSelection = nullptr,
                          bool *published = nullptr);
    void RefreshTitle(bool always = false) {
        if (_panelCallback)
            _panelCallback->RefreshTitle(always);
    }
    void RefreshTitleAlways() { RefreshTitle(true); }
    void Get_ZoneId_Stream_from_ParentFolders(CByteBuffer &buf);
    void ExtractArchives(IQtCompressCallServices &services);
    void TestArchives(IQtCompressCallServices *services = nullptr);
    HRESULT CopyTo(CCopyToOptions &options, const CRecordVector<UInt32> &indices,
                   UStringVector *messages, QtArchiveExtractReport *report = nullptr) {
        bool usePassword = false;
        NativePassword password;
        if (!_parentFolders.IsEmpty()) {
            const CFolderLink &fl = _parentFolders.Back();
            usePassword = fl.UsePassword;
            password = fl.Password.native();
        }
        return CopyTo(options, indices, messages, usePassword, password, nullptr, report);
    }
    HRESULT CopyTo(CCopyToOptions &options, const CRecordVector<UInt32> &indices,
                   UStringVector *messages, bool &usePassword, UString &password,
                   const UStringVector *filePaths = NULL, QtArchiveExtractReport *report = nullptr);
    HRESULT CopyFrom(bool moveMode, const UString &folderPrefix, const UStringVector &filePaths,
                     bool showErrorMessages, UStringVector *messages,
                     const NUpdateArchive::CActionSet *actionSet = nullptr,
                     std::optional<NWildcard::ECensorPathMode> pathMode = std::nullopt,
                     bool *published = nullptr);
    void SetNewFolder(IFolderFolder *folder);
    void SetArchiveOpenPassword(std::optional<ArchivePassword> password) {
        _archiveOpenPassword = std::move(password);
    }
    void AttachFolderManager(IFolderManager *manager, const QString &settingsPath) {
        _folderManager = manager;
        _settingsPath = settingsPath;
    }
    HRESULT OpenAsArc(IInStream *inStream, const CTempFileInfo &tempFileInfo,
                      const UString &virtualFilePath, const UString &arcFormat,
                      COpenResult &openRes, bool materializeStream = false);
    HRESULT OpenAsArc_Msg(IInStream *inStream, const CTempFileInfo &tempFileInfo,
                          const UString &virtualFilePath, const UString &arcFormat,
                          bool materializeStream = false, UString *failure = nullptr);
    HRESULT OpenAsArc_Name(const UString &relPath, const UString &arcFormat);
    HRESULT OpenAsArc_Index(unsigned index, const wchar_t *type = NULL);
    void OpenFolder(unsigned index);
    void OpenFolderExternal(unsigned index);
    void OpenItem(unsigned index, bool tryInternal, bool tryExternal, const wchar_t *type = NULL);
    void OpenFocusedItemAsInternal(const wchar_t *type);
    void OpenSelectedItems(bool tryInternal);
    void EditItem(unsigned index, bool useEditor);
    void EditItem(bool useEditor);
    bool IsFocusedFile() const { return GetFocusedFileIndex() != (UInt32)-1; }
    void OpenItemInArchive(unsigned index, bool tryInternal, bool tryExternal,
                           const wchar_t *type = nullptr);
    HRESULT OpenItemInArchiveInternal(unsigned index, const wchar_t *type = nullptr,
                                      bool materializeStream = false, UString *failure = nullptr);
    bool IsVirus_Message(const UString &name);
    HRESULT OnOpenItemChanged(UInt32 index, const wchar_t *fullFilePath, bool usePassword,
                              const UString &password);
    QtArchiveUpdateResult UpdateExternalFile(UInt32 index, const wchar_t *fullFilePath,
                                             bool usePassword, const UString &password);
    void ReleaseArchiveContext(bool removeTemporaryFiles);
    void MarkArchiveOutdated() { _archiveContextOutdated = true; }
    bool MarkArchiveOutdated(const QString &path);
    bool ArchiveContextOutdated() const { return _archiveContextOutdated; }
    bool PreserveOutdatedArchiveCopies();
    bool SetArchiveOpenDialogs(bool allow) {
        const bool previous = _archiveOpenDialogs;
        _archiveOpenDialogs = allow;
        return previous;
    }
    bool OnOpenItemChanged(const CTempFileInfo &tpi, const UString &fullPathFolderPrefix,
                           bool usePassword, const UString &password,
                           std::optional<CSelectedState> *refreshSelection = nullptr);
    bool CloseOneLevel(bool allowCancel = false);
    void CloseOpenFolders();
    void LoadFullPath();
    void LoadFullPathAndShow(IQtPanelAddressCombo &header,
                             const std::optional<UString> &displayPath);
    bool OnComboBoxCommand(QtAddressCommand code, IQtPanelAddressCombo &header);
    bool OnNotifyComboBoxEnter(const UString &text, IQtPanelAddressCombo &header);
    std::vector<QtAddressSegment> AddressAncestors(IQtPanelAddressCombo &header,
                                                   bool includeCurrentNamespace = true);
    bool OnNotifyComboBoxEndEdit(QtAddressEditReason reason, IQtPanelAddressCombo &header);
    void AddComboBoxItem(IQtPanelAddressCombo &header, const UString &name, int iconIndex,
                         unsigned indent, bool addToList, UInt32 namespaceID = 0);
    void SetToRootFolder();
    HRESULT BindToPath(const UString &fullPath, const UString &arcFormat, COpenResult &openRes);
    HRESULT BindToPathAndRefresh(const UString &path);
    // Share the original Windows quote handling with Qt address transport.
    static UString PrepareBindPath(const UString &path);
    UString GetParentDirPrefix() const;
    HRESULT OpenParentFolder();
    void OpenRootFolder();
    void OpenDrivesFolder();
    const CObjectVector<CFolderLink> &GetParentFolders() const { return _parentFolders; }
    const UString &GetCurrentFolderPrefix() const { return _currentFolderPrefix; }
    // Borrowed interface for Qt property rendering; ownership stays in CPanel.
    IFolderFolder *GetFolder() const { return _folder; }
    UInt64 FolderBindingRevision() const { return _folderBindingRevision; }
    void AttachPresentation(QWidget *list, QWidget *address);
    void SetFocusToList();
    void SetFocusToPath();
    void SetFocusToLastRememberedItem();
    // Qt's list activation adapter supplies the native notification modifiers.
    void OnNotifyActivateItems(Qt::KeyboardModifiers modifiers);
    bool OnKeyDown(const QtPanelKeyDown &keyDownInfo, CApp &app);
    bool OnListViewKeyDown(const QtPanelKeyDown &key, CApp &app);
    bool OnAddressEditKeyDown(const QtPanelKeyDown &key, CApp &app);
    void EditCut();
    void EditCopy();
    void EditPaste();
    void AttachSelectionView(QAbstractItemView *view,
                             QtPanelList::SortColumnResolver sortColumn = {},
                             std::function<void(int)> editLabel = {});
    void DeleteListItems() {
        if (_selectionList.GetItemCount() != 0) {
            const bool b = _enableItemChangeNotify;
            _enableItemChangeNotify = false;
            _selectionList.DeleteListItems();
            _enableItemChangeNotify = b;
        }
    }
    void UpdateSelection();
    void SelectAll(bool selectMode);
    void KillSelection();
    bool IsItemMarked(UInt32 index) const;
    struct FocusState {
        bool Present = false;
        bool Parent = false;
        bool Selected = false;
    };
    FocusState GetFocusState() const;
    void SaveSelectedState(CSelectedState &state);
    void GetSelectedNames(UStringVector &selectedNames);
    void Get_ItemIndices_Selected(CRecordVector<UInt32> &indices) const;
    void Get_ItemIndices_Operated(CRecordVector<UInt32> &indices) const;
    void Get_ItemIndices_All(CRecordVector<UInt32> &indices) const;
    void Get_ItemIndices_OperSmart(CRecordVector<UInt32> &indices) const;
    // Fixture-only snapshot prefix; production consumers use LoadFullPath.
    void SetCurrentFolderPrefix(const QString &prefix) { _currentFolderPrefix = ToUString(prefix); }
    UString GetFolderTypeID() const;
    bool IsFolderTypeEqTo(const char *s) const;
    bool IsRootFolder() const;
    bool IsFSFolder() const;
    bool IsFSDrivesFolder() const;
    bool IsNetFolder() const;
    bool IsAltStreamsFolder() const;
    bool IsArcFolder() const;
    bool IsHashFolder() const;
    bool Is_IO_FS_Folder() const {
        return IsFSFolder() || IsFSDrivesFolder() || IsAltStreamsFolder();
    }
    bool IsFsOrPureDrivesFolder() const {
        return IsFSFolder() || (IsFSDrivesFolder() && !IsDeviceDrivesPrefix());
    }
    int FindDir_InOperatedList(const CRecordVector<UInt32> &indices) const;
    void CreateFileMenu(class IQtFileMenuServices &menu, bool programMenu,
                        class IQtPanelPluginMenuServices *plugins = nullptr);
    bool InvokePluginCommand(unsigned id, class IQtPanelPluginServices &services);
    bool OnContextMenu(QWidget *windowHandle, int xPos, int yPos,
                       class IQtPanelContextMenuServices &services);
#ifdef _WIN32
    void InvokeSystemCommand(const char *command);
    HRESULT CreateShellContextMenu(const CRecordVector<UInt32> &operatedIndices,
                                   CMyComPtr<IContextMenu> &systemContextMenu);
    HRESULT InvokeShellContextMenu(unsigned offset, IContextMenu *systemContextMenu);
    void CreateSystemMenu(HMENU menuSpec, bool showExtendedVerbs,
                          const CRecordVector<UInt32> &operatedIndices,
                          CMyComPtr<IContextMenu> &systemContextMenu);
#endif
    void CreateSevenZipMenu(NQtShellMenu::CMenuHandle *menuSpec, bool showExtendedVerbs,
                            const CRecordVector<UInt32> &operatedIndices, int firstDirIndex,
                            std::unique_ptr<CZipContextMenu> &sevenZipContextMenu,
                            const QString &settingsPath, IQtContextMenuServices &services);
    CPanelStatus Refresh_StatusBar() const;
    bool IsThereReadOnlyFolder() const;
    bool DoesItSupportOperations() const { return _folderOperations != NULL; }
    bool CheckBeforeUpdate(UINT resourceID);
    bool CorrectFsPath(const UString &path, UString &result);
    HRESULT CreateFolder(UString name, QString &error, QString *destinationDirectory = nullptr,
                         QString *publishedArchive = nullptr);
    void RenameFile();
    bool OnBeginLabelEdit(int row);
    HRESULT OnEndLabelEdit(UInt32 realIndex, const UString &enteredName,
                           QString *destinationDirectory = nullptr,
                           QString *publishedArchive = nullptr);
    HRESULT MoveItemsToTrash(class IQtTrashService &trash, CSelectedState &selection,
                             const CRecordVector<UInt32> *explicitIndices = nullptr,
                             bool confirm = true, unsigned *moved = nullptr);
    QtArchiveUpdateResult RemoveItemsAfterMove(const CRecordVector<UInt32> &indices,
                                               CSelectedState &selection, bool confirm);
    bool IsDeviceDrivesPrefix() const { return _currentFolderPrefix.IsEqualTo("\\\\.\\"); }
    bool IsSuperDrivesPrefix() const { return _currentFolderPrefix.IsEqualTo("\\\\?\\"); }
    UString GetFsPath() const;
    UString GetDriveOrNetworkPrefix() const;
    void AddToArchive(class IQtCompressCallServices &services);
    UString GetItemName_for_Copy(unsigned index) const;
    UString GetItemName(unsigned index) const;
    UString GetItemRelPath(unsigned index) const;
    UString GetItemRelPath2(unsigned index) const;
    void Add_ItemRelPath2_To_String(unsigned itemIndex, UString &s) const;
    void GetFilePaths(const CRecordVector<UInt32> &operatedIndices, UStringVector &paths) const;
    UString GetItemFullPath(unsigned index) const;
    bool IsItem_Folder(UInt32 index) const;
    bool IsItem_Deleted(unsigned itemIndex) const;
    QColor OnCustomDraw(unsigned realIndex) const;
    int CompareItemIndices(UInt32 first, UInt32 second, PROPID sortID,
                           bool isRawProp = false) const;
    HRESULT InitColumns();
    // Original native column-order validation, consumed during Qt publication.
    bool GetColumnOrder(CIntArr &columns) const;
    void AddColumn(const CPropColumn &prop);
    void DeleteColumn(unsigned index);
    // Qt's popup supplies the selected source catalog index.
    void ShowColumnsContextMenu(unsigned index);
    // Source load stage; workers carry their original result before properties.
    HRESULT LoadListItems(bool itemsLoaded = false, HRESULT loadedResult = S_OK);
    // Original RefreshListCtrl numItems retained across Qt publication phases.
    UInt32 GetLoadedItemCount() const { return _loadedItemCount.value_or(0); }
    HRESULT GetItemIcon(unsigned index, QIcon &icon);
    void SaveListViewInfo(const CPropColumns &visibleColumns);
    const CPropColumns &GetColumns() const { return _columns; }
    const CPropColumns &GetVisibleColumns() const { return _visibleColumns; }
    bool HasSavedColumnLayout() const { return _listViewInfo.IsLoaded; }
    void
    AttachColumnDefaultsPresentation(std::function<void(const CPanel &, CPropColumns &)> defaults) {
        _columnDefaultsPresentation = std::move(defaults);
    }
    QString GetItemText(UInt32 itemIndex, const CPropColumn &property) const;
    bool IsItem_AltStream(unsigned itemIndex) const;
    HRESULT RefreshListCtrl(const CSelectedState &state);
    HRESULT RefreshListCtrl();
    // Qt publication completes the load stage before this source selection tail.
    HRESULT RestoreLoadedSelection(const CSelectedState &state);
    // Qt loads rows after synchronous source navigation. Transfer its requested
    // selection to that presentation stage instead of deriving it again there.
    std::optional<CSelectedState> TakeRefreshSelection() {
        auto state = std::move(_refreshSelection);
        _refreshSelection.reset();
        return state;
    }
    bool _lastFocusedIsList = true;
    CPanelItemTotals GetItemTotals(const CRecordVector<UInt32> &indices) const;
    QString GetItemsInfoString(const CRecordVector<UInt32> &indices,
                               const CPanelItemTotals *totals = nullptr) const;
    void Properties(const CRecordVector<UInt32> &operatedIndices, QtPropertyReport &message) const;
    void ShowProperties(QtPropertyReport report);
    void Properties();
    static void SetBookmark(CFastFolders &fastFolders, unsigned index,
                            const UString &currentFolderPrefix);
    static UString OpenBookmark(CFastFolders &fastFolders, unsigned index);

  private:
    static bool DoItemAlwaysStart(const UString &name);
    friend class CApp;
    friend class QtPanelLocationTransaction;
    friend class CMyComboBoxEdit;
    void OnItemChanged(unsigned index, bool oldSelected, bool newSelected);
    friend class CThreadFolderOperations;
    QPointer<QWidget> _mainWindow;
    QPointer<QWidget> _parentWindow;
    std::function<bool(CPanel &)> _createPresentation;
    std::function<HRESULT(const CSelectedState &)> _reloadPresentation;
    std::function<void(const QString &, const CPanel *)> _archivePublication;
    std::function<void(HRESULT)> _reloadCompletion;
    std::function<void()> _invalidatePresentation;
    std::function<void(const CPanelStatus &)> _statusPresentation;
    std::function<void(UInt32)> _viewModePresentation;
    std::function<void()> _addressFocusPresentation;
    std::function<void(unsigned)> _fileClipboardPresentation;
    bool _portableFolderChanged = false;
    CAppState *_appState = nullptr;
    UINT _baseID = 0;
    UINT _comboBoxID = 0;
    UINT _statusBarID = 0;
    CPanelCallback *_panelCallback = nullptr;
    IQtPanelDragServices *_dragServices = nullptr;
    std::optional<CSelectedState> _refreshSelection;
    std::optional<ArchivePassword> _archiveOpenPassword;
    bool _archiveOpenDialogs = true;
    bool _archiveContextOutdated = false;
    std::vector<QtPanelLocationTransaction *> _locationTransactions;
    IQtExternalFileServices *_externalFileServices = nullptr;
    std::unique_ptr<CExitEventLauncher> _exitEventLauncher;
    std::vector<std::weak_ptr<IQtExternalSessionOwner>> _externalSessions;
    HRESULT OpenItemInArchiveImpl(unsigned index, bool tryInternal, bool tryExternal,
                                  const wchar_t *type, bool materializeStream = false,
                                  UString *failure = nullptr);
    void StartExternalFileSession(NWindows::NFile::NDir::CTempDir &tempDirectory,
                                  const CTempFileInfo &tempFileInfo, const FString &tempDirNorm,
                                  bool usePassword, const UString &password);
    // Original Create initializes these to true; Qt panels exist before Create is ported.
    bool _processTimer = true;
    bool _processNotify = true;
    bool _enableItemChangeNotify = true;
    bool _processStatusBar = true;
    bool _dontShowMode = false;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPointer<QWidget> _listView;
    QPointer<QWidget> _headerComboBox;
    CMyComboBoxEdit _comboBoxEdit;
    UStringVector ComboBoxPaths;
    QtPanelList _selectionList;
    QMetaObject::Connection _itemChangedConnection;
    CRecordVector<bool> _selectedStatusVector;
    CMyComPtr<IFolderFolder> _folder;
    UInt64 _folderBindingRevision = 0;
    CMyComPtr<IFolderCompare> _folderCompare;
    CMyComPtr<IArchiveGetRawProps> _folderRawProps;
    CMyComPtr<IFolderAltStreams> _folderAltStreams;
    CMyComPtr<IFolderOperations> _folderOperations;
    CObjectVector<CFolderLink> _parentFolders;
    NWindows::NDLL::CLibrary _library;
    bool _flatMode = false;
    bool _flatModeForDisk = false;
    bool _flatModeForArc = false;
    bool _thereAreDeletedItems = false;
    bool _markDeletedItems = true;
    CMyComPtr<IFolderManager> _folderManager;
    QString _settingsPath;
    bool _needSaveInfo = false;
    UString _typeIDString;
    CListViewInfo _listViewInfo;
    std::function<void(const CPanel &, CPropColumns &)> _columnDefaultsPresentation;
    CPropColumns _columns;
    CPropColumns _visibleColumns;

  public:
    QWidget *GetParent() const;
    void MessageBox_Error(const UString &message) const;
    void MessageBox_Error_Caption(LPCWSTR message, LPCWSTR caption) const;
    void MessageBox_Error_HRESULT_Caption(HRESULT result, LPCWSTR caption) const;
    void MessageBox_Error_LangID(UINT resourceID) const;
    void MessageBox_Error_HRESULT(HRESULT result) const;
    void MessageBox_Error_2Lines_Message_HRESULT(LPCWSTR message, HRESULT errorCode) const;
    void MessageBox_LastError() const;
    void MessageBox_Error_UnsupportOperation() const;

  private:
    CMyComPtr<IFolderGetItemName> _folderGetItemName;
    CRecordVector<bool> _isDirVector;
    CRecordVector<Int32> _itemIconIndices;
    CRecordVector<bool> _itemSystemIcons;
    CMyComPtr<IFolderGetSystemIconIndex> _folderGetSystemIconIndex;
    bool _iconIsFSDrivesFolder = false;
    bool _iconIsArcFolder = false;
    // Retain the source early-return result across Qt row publication.
    HRESULT _listLoadResult = S_OK;
    std::optional<UInt32> _loadedItemCount;
    HRESULT LoadListItemsImpl(bool itemsLoaded, HRESULT loadedResult);
    HRESULT LoadItemIcon(UInt32 index);
    UString _currentFolderPrefix;
    bool GetItem_BoolProp(UInt32 index, PROPID propID) const;
    UString GetItemPrefix(unsigned index) const;
    unsigned GetRealItemIndex(int indexInListView) const;
    UInt32 GetFocusedFileIndex() const;
    UInt32 GetFolderItemCount() const;
    void EnsureSelectionVectorSize();
    void SetFocusedSelectedItem(int index, bool select);
    UInt64 GetItemSize(UInt32 index) const;
    UInt64 GetItem_UInt64Prop(UInt32 index, UInt32 propID) const;
};

UString GetFolderPath(IFolderFolder *folder);
