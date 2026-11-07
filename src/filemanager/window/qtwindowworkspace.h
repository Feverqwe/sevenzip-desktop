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
class QtFavoriteStore;
class QtFastFoldersStore;
class QtNativePlaces;
class QtRecentPlacesStore;
struct QtRecentPlace;
struct QtFavorite;
class QtArchiveMountManager;

// CApp owns the two panel bindings. The workspace keeps navigation state and
// each panel’s mount manager.
class QtWindowWorkspace final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(QtWindowWorkspace)
  public:
    explicit QtWindowWorkspace(MainWindow &window);
    void AttachPanel(unsigned panel, QVBoxLayout &layout);
    void Refresh();
    QJsonObject Save();
    void Restore(const QJsonObject &state, std::function<void()> finished = {},
                 std::function<void()> canceled = {}, const QtArchiveLocation &firstArchive = {});
    void EditLocation();
    QString CurrentLocation();
    void AttachContent(unsigned panel, QVBoxLayout &layout);
    void InvalidateArchive(const QString &path);
    bool RefreshPublishedArchive(unsigned panel, bool interactive = false);
    bool CanRefreshPublishedArchives() const;
    void RestoreViewPosition(CPanel &source, QAbstractItemView &view, const QPoint &position);
    void RetainListFocus(unsigned panel);
    void RestoreAddressFocus(unsigned panel, const QString &draft, int cursor);
    void ExitExternalWorkers(bool hardExit);
    void Dispose();
    QtArchiveMountManager *CurrentMountManager(bool create);
    bool CanCloseMounts();
    QWidget *MountCloseBlocker() const;
    bool PreparePanelForShow(unsigned panel);
    enum class NavigationHistory { Record, Skip };
    HRESULT Navigate(const std::function<HRESULT()> &body,
                     std::function<void(HRESULT)> finished = {},
                     NavigationHistory history = NavigationHistory::Record);
    void ShowRecentPlaces(unsigned panel, QWidget &anchor);
    void RecordVisit(const QString &path);
    void SetBookmark(unsigned index, const UString &path);
    void ProtectNavigation(const QString &path = {});
    void RetainNavigationFolder();
    std::optional<HRESULT> NavigationPublication() const;
    void NavigationFinished(HRESULT result);
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
        quint64 archiveRevision = 0;
        quint64 archiveApplied = 0;
        quint64 archiveAttempted = 0;
    };
    struct Panel {
        Location location;
        QPointer<QtArchiveMountManager> mounts;
        QToolBar *actions = nullptr;
        QWidget *actionRow = nullptr;
    };
    bool Ready() const;
    void Capture(unsigned panel, bool retainView = true);
    Visit CaptureVisit(unsigned panel);
    void FinishNavigation(unsigned panel, HRESULT result);
    struct ReopenLayer {
        QString format;
        std::optional<ArchivePassword> password;
    };
    HRESULT OpenArchiveLocation(unsigned panel, const QtArchiveLocation &location,
                                const std::vector<ReopenLayer> *layers = nullptr,
                                bool interactive = true);
    HRESULT OpenArchiveDirectory(unsigned panel, const QtArchiveLocation &location);
    HRESULT OpenPanelLocation(unsigned panel);
    HRESULT OpenRecordedLocation(unsigned panel, const QString &path, const QString &nativeIdentity,
                                 const QtArchiveLocation &archive);
    bool LocationMatches(unsigned panel, const QString &path);
    void CreateActions(unsigned panel, QVBoxLayout &layout);
    void RefreshChrome();
    void ShowArchiveChangeNotice(unsigned panel);
    void AddFavorite();
    void OpenFavorite(const QtFavorite &favorite);
    void OpenRecentPlace(unsigned panel, const QtRecentPlace &place);
    void WhenReady(std::function<void()> body, std::function<void()> canceled = {});
    void RestoreListFocus(unsigned panel, quint64 request, QPointer<QWidget> widget);
    void RestorePanel(unsigned panel, const QJsonObject &saved, std::function<void()> finished,
                      std::function<void()> canceled, const QtArchiveLocation &archive = {});
    void CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result);
    void RetryLocation(unsigned panel, bool parent);
    MainWindow &window_;
    std::array<Panel, 2> panels_;
    bool changing_ = false;
    QtFavoriteStore *favorites_ = nullptr;
    QtFastFoldersStore *fastFolders_ = nullptr;
    QtNativePlaces *nativePlaces_ = nullptr;
    QtRecentPlacesStore *recentPlaces_ = nullptr;
    quint64 focusRequest_ = 0;
    std::array<std::unique_ptr<Navigation>, 2> navigation_;
    bool restoringNavigation_ = false;
};
