#pragma once
#include "archive/password/archivepassword.h"
#include <optional>
struct QtOperationResult;

#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "devices/deviceselectiondialog.h"
#include "dialogs/combo/alias.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/copy/alias.h"
#include "dialogs/extract/alias.h"
#include "filemanager/App.h"
#include "filemanager/providers/qtfolderprojection.h"
#include "filemanager/qtfilemanagersplitter.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtguioutputrefresh.h"
#include "fileops/scanner/filesystemscanner.h"
#include "fileops/transfer/filetransferoperation.h"
#include "fileops/volumes/volumeoperations.h"
#include "settings/dialog_alias.h"
#include "ui/progress/qtprogresstransport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringView>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class QtFileListHover;
class CProgressDialog;
class IQtTrashService;
struct QtFileManagerStartup;

class MainWindow : public QMainWindow {
  public:
    explicit MainWindow(ArchiveBackend *backend = nullptr,
                        std::function<bool(const QUrl &)> fileOpener = {},
                        IQtTrashService *trashService = nullptr,
                        const QtFileManagerStartup *startup = nullptr,
                        DeviceSelectionServices deviceServices = DeviceSelectionServices::system());

    void openArchive(const QString &path);
    void openPath(const QString &path);

    void extractTo(const QString &destination, bool onlySelected = false,
                   const QString &password = {}, const ExtractionOptions &options = {});

    void testArchive();

  private:
    struct WindowProgress {
        QPointer<CProgressDialog> dialog;
        std::unique_ptr<IQtProgressTransport> transport;
    };
    DeviceSelectionServices deviceServices_;
    QLabel *deviceReadOnly_ = nullptr;
    friend class QtAppPresentation;
    friend class QtVolumeWindowServices;
    friend class QtFileManagerWindowServices;
    friend class QtFileManagerCommands;
    friend class QtFileManagerDrag;
    friend class QtFileManagerShellMenu;
    friend class QtProgramFileMenu;
    friend class QtPanelContextMenu;
    friend class QtFileManagerInitServices;
    friend class QtRenameController;
    void initializeFileManagerWindow(const QtFileManagerStartup *startup);
    void createFileManagerWindow(const QtFileManagerStartup *startup, bool panelsDefined,
                                 UInt32 splitterPos);
    HRESULT openArchiveAs(const QString &path, const QString &type);
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void reloadLangItems();
    void refreshToolbarTexts();
    void quiesceOperations();
    bool canHandleExternalUpdate() const;
    bool runExternalArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body);
    QMenu *createMenusAndToolbar();
    void createFileActions(QMenu *menu, QToolBar *toolbar);
    void createFileExtras(QMenu *menu, QToolBar *toolbar);
    void createViewModes(QMenu *viewMenu);
    void createFavoritesMenu(QMenu *favoritesMenu);
    void createViewOptions(QMenu *viewMenu);
    void createToolbarOptions(QMenu *viewMenu);
    void rebuildSourceMenus();
    void activateSingleClickedRow(int row);
    void createFilePanel(QMenu *editMenu, unsigned index);
    void activatePanelUi(unsigned index);
    void withPanelProjection(unsigned index, const std::function<void()> &body);
    unsigned scanningPanelMask() const;
    unsigned panelIndexFor(QObject *object) const;
    void togglePanels();
    QString copyDefaultDestination() const;
    bool transferToArchivePanel(const QString &destination, bool move,
                                const CRecordVector<UInt32> &indices);
    bool transferBetweenArchivePanels(const QString &destination, bool move,
                                      const CRecordVector<UInt32> &indices);
    enum class ArchiveTransferReload { BoundFolder, ReopenArchive };
    void refreshArchiveTransferDestination(
        unsigned index, const CSelectedState &selection,
        ArchiveTransferReload reload = ArchiveTransferReload::BoundFolder);
    void transferFromArchive(const QString &destination, bool move, bool copyToSame,
                             unsigned sourceIndex, const CRecordVector<UInt32> &indices);
    void createFileTable(QWidget *central);
    void configureColumns();
    void updateIconColumn();
    void saveColumns();
    void redrawTimestampColumns();
    void redrawSourceProperties();
    void createPanelActions(QMenu *editMenu);
    void createIconList(QWidget *central);
    void connectFilesystemScanner();
    void cancelListPublication();
    void failFilesystemListing(HRESULT result);
    void connectBackend();
    void backendFinished(bool ok, const QString &message, HRESULT result,
                         bool messageDisplayed = false,
                         std::optional<QtArchiveExtractStatus> extraction = std::nullopt);
    bool extractWithSource(const QString &destination, bool onlySelected, const QString &password,
                           const ExtractionOptions &options, const QStringList &names,
                           const CRecordVector<UInt32> *explicitIndices = nullptr);
    void closeProgressDialog(bool finished = false);
    void beginWorkerProgress(const QString &label, const QString &detail,
                             std::function<void()> cancel);

    enum class State { Filesystem, Namespace, Loading, Viewing, Operating };
    void setViewMode(int mode);

  public:
    ~MainWindow() override;

  private:
    void startVolume(VolumeOperation::Mode mode);
    void launchVolume(VolumeOperation *operation, const UString &title, const QString &source,
                      UInt64 totalSize, std::function<void()> completion);
    void showItemContextMenu(unsigned owner, QAbstractItemView *view, const QPoint &pos);
    void applySortMode(bool enableSorting = false);
    void sortIndicatorChanged(unsigned panel, int column, Qt::SortOrder order,
                              bool headerClick = false);
    static QIcon toolbarIcon(int kind);
    static QString toolbarSettingsPath();
    void recordVisitedPath(const QString &path);
    void showFoldersHistory();
    void refreshBookmarks();
    void saveBookmark(int index);
    void openBookmark(int index);
    void updateToolbar();
    void applyFlatView(bool enabled);
    void showSettings();
    void applyFileListSettings();
    enum class PanelInputTarget { Other, Address, List, ListViewport };
    PanelInputTarget panelInputTarget(QObject *object, unsigned panel) const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool handlePanelKey(PanelInputTarget target, QKeyEvent *key);
    bool handlePanelListKey(QKeyEvent *key);
    bool overridePanelKey(const QKeyEvent &key) const;
    void configureShortcuts();
    QAction *panelShortcut(const QKeyEvent &key) const;
    bool reservedPanelKey(const QKeyEvent &key) const;
    bool isPanelEditorFocused() const;
    QList<QAction *> panelShortcutActions_;
    void updatePanelModifier(const QKeyEvent &key);
    void handleSelectionClick(int row);
    HRESULT showDirectory(const QString &path, bool remember, bool sourceAlreadyBound = false,
                          bool updateAddress = true);
    HRESULT renderRootFolder(bool updateAddress = true);
    HRESULT appendFilesystemEntry(const QString &baseDirectory, quint32 sourceIndex);
    void appendParentEntry();
    void completeListSelection();
    void refreshCurrent(const CSelectedState *preserved = nullptr);
    HRESULT reloadPanelPresentation(unsigned index, const CSelectedState &selection);
    HRESULT startFilesystemListing(bool sourceAlreadyBound, bool updateAddress);
    HRESULT finishSynchronousFilesystemListing(HRESULT result);
    HRESULT startFilesystemScan(IFolderFolder *folder, bool flat);
    struct FilesystemViewport {
        FilesystemViewport(QAbstractItemView *source, const QString &folder, const QString &top,
                           const QPoint &position)
            : view(source), directory(folder), topPath(top), scrollPosition(position) {}
        QPointer<QAbstractItemView> view;
        QString directory;
        QString topPath;
        QPoint scrollPosition;
    };
    HRESULT refreshFilesystem(const CSelectedState &selection, bool sourceAlreadyBound,
                              std::optional<FilesystemViewport> viewport = std::nullopt);
    void restoreListSelection(const CSelectedState &selection);
    struct FilesystemSignature;
    void pollAutoRefresh();
    void restoreAutoRefreshViewport();
    FilesystemSignature filesystemSignature(const QString &path) const;
    void SelectAll(bool select);
    void InvertSelection();
    void SelectSpec(bool select);
    void SelectByType(bool select);
    HRESULT navigateTo(const QString &path, bool sourceAddress = false);
    void activateRow(int row, Qt::KeyboardModifiers modifiers);
    void activateItems(Qt::KeyboardModifiers modifiers);
    void goBack();
    void goParent();
    enum class RootLocation { Namespace, Drives };
    void goRoot(bool remember = true, RootLocation location = RootLocation::Namespace);
    QString archiveDisplayPath() const;
    HRESULT renderBoundFolder(bool updateAddress = true);
    HRESULT renderArchiveFolder(bool updateAddress = true);
    HRESULT appendArchiveEntry(CAgentFolder &folder, unsigned index, const QString &prefix,
                               bool flat);
    void setState(State state);
    void refreshActions();
    void refreshPanelStatus();
    void showPanelStatus(unsigned panel, const CPanelStatus &status);
    void showSelectedProperties();
    void calculateHash(const QString &method);
    void begin(const QString &label, const QString &detail);
    std::unique_ptr<WindowProgress> createProgressDialog(const QString &label,
                                                         const QString &detail);
    CProgressDialog *progressDialog() const;
    void extractArchives();
    void runArchiveBatchCommand(bool test);
    HRESULT launchGuiArchiveTest(const UStringVector &paths, bool hashMode);
    bool canLaunchGuiFilesystemOperation();
    HRESULT launchGuiHash(const UStringVector &paths, const UString &method,
                          const UString &basePrefix = UString(), bool enterToDirs = true);
    HRESULT launchGuiArchiveCompress(const UString &folder, const UString &name,
                                     const UString &type, bool addExtension,
                                     const UStringVector &paths, bool email, bool showDialog);
    HRESULT launchGuiArchiveExtract(const UStringVector &paths, const UString &folder,
                                    bool showDialog, bool elimDup, UInt32 writeZone);
    HRESULT observeGuiArchiveOperation(QtOperationLauncher *operation, const QString &error);
    void refreshGuiOutputPanel(unsigned index, const QtGuiOutputRefresh::Outputs &output);
    void createDialog();
    void transferFiles(const QStringList &sources, const QString &destination, bool move,
                       bool directPath = false);
    void executeCopyDestination(bool move, bool copyToSame, unsigned sourceIndex,
                                const QString &destination, const CRecordVector<UInt32> &indices,
                                QtCopyRoute route);
    QStringList selectedArchivePaths();
    QStringList archivePathsForIndices(const CRecordVector<UInt32> &indices);
    CRecordVector<UInt32> sourceOperatedIndices();

    ArchiveBackend *backend_ = nullptr;
    std::unique_ptr<IQtTrashService> nativeTrashService_;
    IQtTrashService *trashService_ = nullptr;
    CMyComPtr<IFolderManager> folderManager_;
    std::unique_ptr<class QtAssociatedFileServices> associatedFileServices_;
    std::unique_ptr<class QtAppPresentation> appPresentation_;
    std::unique_ptr<class QtFileManagerDrag> drag_;
    std::unique_ptr<class QtFileManagerCommands> commands_;
    std::unique_ptr<class QtRenameController> renameController_;
    std::unique_ptr<class QtProgramFileMenu> programFileMenu_;
    std::function<bool(const QUrl &)> fileOpener_;
    CApp app_;
    QAction *saveBookmarks_[10]{};
    QAction *openBookmarks_[10]{};
    bool configuringColumns_ = false;
    QAction *sortActions_[5]{};
    QAction *timeActions_[5]{};
    QAction *utcTimeAction_ = nullptr;
    QAction *autoRefreshAction_ = nullptr;
    QAction *viewActions_[4]{};
    QAction *flatView_ = nullptr;
    QAction *twoPanelsAction_ = nullptr;
    struct FilesystemSignature {
        QDateTime modified;
        QDateTime metadataChanged;
        qint64 size = 0;
        bool exists = false;
        bool directory = false;
        bool operator==(const FilesystemSignature &other) const {
            return modified == other.modified && metadataChanged == other.metadataChanged &&
                   size == other.size && exists == other.exists && directory == other.directory;
        }
    };
    struct PanelUiState {
        QWidget *container = nullptr;
        QLineEdit *path = nullptr;
        QComboBox *pathCombo = nullptr;
        FileTable *table = nullptr;
        QListView *iconList = nullptr;
        QStackedWidget *viewStack = nullptr;
        std::array<QLabel *, 4> statusLabels{};
        QtFileListHover *tableHover = nullptr;
        QtFileListHover *iconHover = nullptr;
        QString directory;
        QString returnDirectory;
        QString archive;
        QString displayArchive;
        ArchivePassword archivePassword;
        QString archiveFolder;
        QStringList history;
        State state = State::Filesystem;
        bool scanningDisk = false;
        bool scanningArchive = false;
        FilesystemScanner *filesystemScanner = nullptr;
        QtFolderProjection *listProjection = nullptr;
        QHash<QString, FilesystemSignature> autoRefreshSnapshot;
        std::optional<FilesystemViewport> pendingViewport;
        std::optional<CSelectedState> pendingSelection;
    };
    std::array<PanelUiState, 2> panelUi_;
    QtFileManagerSplitter *panelSplitter_ = nullptr;
    unsigned projectedPanel_ = 0;
    unsigned panelProjectionDepth_ = 0;
    PanelUiState &projectedUi() { return panelUi_[projectedPanel_]; }
    const PanelUiState &projectedUi() const { return panelUi_[projectedPanel_]; }
    QTimer *autoRefreshTimer_ = nullptr;
    std::unique_ptr<QtGuiOutputRefresh> guiOutputRefresh_;
    State previousState_ = State::Filesystem;
    struct PendingArchiveMove {
        PendingArchiveMove(unsigned index, const CRecordVector<UInt32> &selected)
            : source(index), indices(selected) {}
        const unsigned source;
        const CRecordVector<UInt32> indices;
    };
    std::unique_ptr<PendingArchiveMove> archiveMove_;
    Qt::KeyboardModifiers selectionClickModifiers_ = Qt::NoModifier;
    bool selectionClickLeft_ = false;
    bool selectionShiftSequence_ = false;
    bool rightControlPressed_ = false;
    std::unique_ptr<WindowProgress> progress_;
    bool sourceArchiveProgress_ = false;
    QAction *toolbarAdd_ = nullptr;
    QAction *extract_ = nullptr;
    QAction *test_ = nullptr;
    QAction *infoAction_ = nullptr;
    QAction *back_ = nullptr;
    QAction *up_ = nullptr;
    QAction *root_ = nullptr;
    QAction *openItem_ = nullptr;
    QAction *openInside_ = nullptr;
    QAction *openInsideOne_ = nullptr;
    QAction *openInsideParser_ = nullptr;
    QAction *openOutside_ = nullptr;
    QAction *exitAction_ = nullptr;
    QAction *parent_ = nullptr;
    QAction *selectAll_ = nullptr;
    QAction *copy_ = nullptr;
    QAction *move_ = nullptr;
    QAction *remove_ = nullptr;
    QAction *rename_ = nullptr;
    QAction *split_ = nullptr;
    QAction *combine_ = nullptr;
    VolumeOperation *volumeOperation_ = nullptr;
    FileTransferOperation *fileTransfer_ = nullptr;
    QPointer<QDialog> overwriteDialog_;
    QAction *viewFile_ = nullptr;
    QAction *editFile_ = nullptr;
    QAction *diffFiles_ = nullptr;
    QAction *properties_ = nullptr;
    QHash<unsigned, QAction *> hashActions_;
    QAction *createFolder_ = nullptr;
    QAction *createFile_ = nullptr;
    QAction *refresh_ = nullptr;
    QAction *settingsAction_ = nullptr;
    CFmSettings fileListSettings_;
    QAction *foldersHistory_ = nullptr;
    QAction *deselectAll_ = nullptr;
    QAction *invertSelection_ = nullptr;
    QAction *selectMask_ = nullptr;
    QAction *deselectMask_ = nullptr;
    QAction *selectType_ = nullptr;
    QAction *deselectType_ = nullptr;
    QAction *archiveToolbarOption_ = nullptr;
    QAction *standardToolbarOption_ = nullptr;
    QAction *largeButtonsOption_ = nullptr;
    QAction *buttonTextOption_ = nullptr;
    QAction *toolbarGroupSeparator_ = nullptr;
};
