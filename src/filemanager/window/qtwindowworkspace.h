#pragma once
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanellocationtransaction.h"
#include "filemanager/window/qtsavedlocation.h"
#include "ui/theme/qtcommandicons.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <array>
#include <optional>
#include <vector>

class MainWindow;
class QAbstractItemView;
class QVBoxLayout;
class QToolBar;
class QListView;
class QPushButton;
class QtFavoriteStore;
class QtFastFoldersStore;
class QtNativePlaces;
class QtRecentPlacesStore;
struct QtRecentPlace;
struct QtFavorite;
struct QtAddressDraft;
class QtArchiveMountManager;

// CApp owns the two panel bindings. The workspace keeps navigation state and
// each panel’s mount manager.
class QtWindowWorkspace final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(QtWindowWorkspace)
  public:
    explicit QtWindowWorkspace(MainWindow &window);
    void AttachPanel(unsigned panel, QVBoxLayout &layout);
    void Refresh();
    void RefreshChrome();
    QJsonObject Save();
    void Restore(const QJsonObject &state, std::function<void()> finished = {},
                 std::function<void()> canceled = {}, const QtArchiveLocation &firstArchive = {});
    void EditLocation();
    HRESULT OpenFolderInPeer(unsigned source, std::optional<UInt32> item = {});
    QString CurrentLocation();
    void AttachContent(unsigned panel, QVBoxLayout &layout);
    void InvalidateArchive(const QString &path, int skipPanel = -1);
    bool RefreshPublishedArchive(unsigned panel, bool interactive = false,
                                 bool checkSource = false);
    bool CanRefreshPublishedArchives() const;
    bool CanRefreshPublishedOutput(unsigned panel) const;
    void RequestInitialFocus(unsigned panel);
    void RequestListFocus(unsigned panel, bool switchingPane = false);
    void CompleteListFocusIfReady();
    void CancelListFocus();
    void RetainListFocus(unsigned panel);
    quint64 InteractionRevision() const { return interactionRevision_; }
    HRESULT NavigateToList(const std::function<HRESULT()> &body);
    void FocusListAfterLocationOpen(unsigned panel, quint64 interaction);
    void RestoreAddressFocus(unsigned panel, const QtAddressDraft &draft);
    void ExitExternalWorkers(bool hardExit);
    void Dispose();
    QtArchiveMountManager *CurrentMountManager(bool create);
    bool CanCloseMounts();
    QWidget *MountCloseBlocker() const;
    bool PreparePanelForShow(unsigned panel);
    void WhenReady(std::function<void()> body, std::function<void()> canceled = {},
                   bool presentingResult = false);
    enum class NavigationHistory { Record, Skip };
    HRESULT Navigate(const std::function<HRESULT()> &body,
                     std::function<void(HRESULT)> finished = {},
                     NavigationHistory history = NavigationHistory::Record);
    void ShowRecentPlaces(unsigned panel, QWidget &anchor);
    void NavigateBreadcrumb(unsigned panel, const QString &path);
    void RecordVisit(const QString &path);
    void SetBookmark(unsigned index);
    void OpenBookmark(unsigned index);
    CFastFolders BookmarkDisplayPaths();
    void ProtectNavigation(const QString &path = {});
    void RetainNavigationFolder();
    std::optional<HRESULT> NavigationPublication() const;
    void NavigationFinished(HRESULT result);
    void ReportNavigationFailure(const QString &message);
    void NavigationCanceled();
    void QuiesceNavigation();
    bool CanTravel(unsigned panel, int direction) const;
    void Travel(int direction);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    friend class QtWorkspaceApplication;
    struct Visit {
        QString path;
        QString nativePlaceIdentity;
        QtArchiveLocation archiveLocation;
        CSelectedState selection;
        QPoint scroll;
        UInt32 viewMode = 3;
        bool diskFolder = false;
        std::optional<std::pair<PROPID, bool>> cursorColumn = {};
        bool SameLocation(const Visit &other) const;
    };
    struct Navigation {
        std::shared_ptr<CPanel> source;
        std::unique_ptr<QtPanelLocationTransaction> transaction;
        CMyComPtr<IFolderFolder> previousFolder;
        unsigned previousDepth = 0;
        Visit origin;
        NavigationHistory history = NavigationHistory::Record;
        std::optional<Visit> unavailableOrigin;
        int destination = -1;
        unsigned depth = 0;
        std::optional<HRESULT> completed;
        std::function<void(HRESULT)> finished;
        QString failureMessage;
    };
    struct Location {
        QString path;
        QString nativePlaceIdentity;
        QtArchiveLocation archiveLocation;
        std::vector<Visit> visits;
        int cursor = -1;
        CSelectedState selection;
        QPoint scroll;
        int viewMode = 3;
        PROPID sort = 0;
        bool ascending = true;
        bool unavailable = false;
        bool diskFolder = false;
        quint64 archiveRevision = 0;
        quint64 archiveApplied = 0;
        quint64 archiveAttempted = 0;
    };
    struct Panel {
        Location location;
        quint64 archiveRetryRequest = 0;
        QPointer<QtArchiveMountManager> mounts;
        QToolBar *actions = nullptr;
        QWidget *actionRow = nullptr;
        QPushButton *retry = nullptr;
    };
    bool Ready(bool allowSourceCall = false) const;
    void Capture(unsigned panel, bool retainView = true);
    Visit CaptureVisit(unsigned panel);
    void RestoreViewPosition(CPanel &source, QAbstractItemView &view, const Visit &visit);
    void FinishNavigation(unsigned panel, HRESULT result);
    struct ReopenLayer {
        QString format;
        std::optional<ArchivePassword> password;
    };
    HRESULT OpenArchiveLocation(unsigned panel, const QtArchiveLocation &location,
                                const std::vector<ReopenLayer> *layers = nullptr,
                                bool interactive = true, QString *failure = nullptr);
    HRESULT OpenArchiveDirectory(unsigned panel, const QtArchiveLocation &location);
    HRESULT OpenPanelLocation(unsigned panel, QString *failure = nullptr);
    HRESULT OpenRecordedLocation(unsigned panel, const QString &path, const QString &nativeIdentity,
                                 const QtArchiveLocation &archive, QString *failure = nullptr);
    bool LocationMatches(unsigned panel, const QString &path);
    void CreateActions(unsigned panel, QVBoxLayout &layout);
    void ShowArchiveChangeNotice(unsigned panel);
    void AddFavorite();
    void OpenFavorite(const QtFavorite &favorite);
    void OpenRecentPlace(unsigned panel, const QtRecentPlace &place);
    void RestorePanel(unsigned panel, const QJsonObject &saved, std::function<void()> finished,
                      std::function<void()> canceled, const QtArchiveLocation &archive = {});
    void CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result,
                              const QString &failure = {});
    void RetryLocation(unsigned panel, bool parent);
    MainWindow &window_;
    std::array<Panel, 2> panels_;
    bool changing_ = false;
    QtFavoriteStore *favorites_ = nullptr;
    QtFastFoldersStore *fastFolders_ = nullptr;
    QtNativePlaces *nativePlaces_ = nullptr;
    QtRecentPlacesStore *recentPlaces_ = nullptr;
    std::optional<unsigned> pendingListFocus_;
    bool switchingPane_ = false;
    quint64 focusRequest_ = 0;
    quint64 interactionRevision_ = 0;
    std::array<std::unique_ptr<Navigation>, 2> navigation_;
    bool restoringNavigation_ = false;
};
