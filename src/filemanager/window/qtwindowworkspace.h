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
class QTabBar;
class QVBoxLayout;
class QToolBar;
class QListView;
class QtFavoriteStore;
class QtFastFoldersStore;
class QtNativePlaces;
class QtRecentPlacesStore;
class QtRecentPlacesDialog;
struct QtRecentPlace;
struct QtTabRuntime;

// Tab runtimes own stable sources; CApp projects two active slots. Persistent
// sessions contain navigation values and never live handles or passwords.
class QtWindowWorkspace final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(QtWindowWorkspace)
  public:
    explicit QtWindowWorkspace(MainWindow &window);
    void AttachPanel(unsigned panel, QVBoxLayout &layout);
    void Refresh();
    void NewTab(unsigned panel, const QString &path = {});
    void CloseTab(unsigned panel, int tab);
    QJsonObject Save();
    void Restore(const QJsonObject &state, std::function<void()> finished = {},
                 std::function<void()> canceled = {});
    void EditLocation();
    QString CurrentLocation();
    void AttachContent(unsigned panel, QVBoxLayout &layout, QWidget &status);
    bool WithSource(CPanel &source, const std::function<bool()> &body);
    bool SourceIsActive(const CPanel &source) const;
    void MarkDirty(CPanel &source);
    void RestoreViewPosition(CPanel &source, QAbstractItemView &view, const QPoint &position);
    void RetainListFocus(unsigned panel);
    void ExitExternalWorkers(bool hardExit);
    bool HasExternalWorkers() const;
    void QuiesceInactive();
    void Dispose();
    // Acceptance may precede opening a restored tab; finished reports the actual result.
    bool TransferTab(const QString &id, QtWindowWorkspace &target, unsigned targetPanel,
                     std::function<void(bool)> finished = {});
    bool PreparePanelForShow(unsigned panel);
    HRESULT Navigate(const std::function<HRESULT()> &body,
                     std::function<void(HRESULT)> finished = {});
    void ShowRecentPlaces();
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
    struct Visit {
        QString path;
        QString nativePlaceIdentity;
        QtArchiveLocation archiveLocation;
        CSelectedState selection;
        QPoint scroll;
        bool SameLocation(const Visit &other) const;
    };
    struct Navigation {
        QString tab;
        std::shared_ptr<CPanel> source;
        std::unique_ptr<QtPanelLocationTransaction> transaction;
        CMyComPtr<IFolderFolder> previousFolder;
        unsigned previousDepth = 0;
        Visit origin;
        int destination = -1;
        unsigned depth = 0;
        std::optional<HRESULT> completed;
        std::function<void(HRESULT)> finished;
        QPointer<QObject> requester;
        bool canceled = false;
    };
    struct Tab {
        QString id;
        QString path;
        QString nativePlaceIdentity;
        QtArchiveLocation archiveLocation;
        QtCommandIcon symbol = QtCommandIcon::Folder;
        std::vector<Visit> visits;
        int cursor = -1;
        CSelectedState selection;
        QPoint scroll;
        int viewMode = 3;
        PROPID sort = 0;
        bool ascending = true;
        bool unavailable = false;
        std::shared_ptr<QtTabRuntime> runtime;
    };
    struct Panel {
        QTabBar *bar = nullptr;
        std::vector<Tab> tabs;
        int current = 0;
        QToolBar *actions = nullptr;
        QWidget *sidebar = nullptr;
        std::shared_ptr<QtTabRuntime> bound;
    };
    bool Ready() const;
    struct TabPosition {
        unsigned panel;
        int index;
    };
    std::optional<TabPosition> FindTab(const QString &id) const;
    void Capture(unsigned panel, bool retainView = true);
    Visit CaptureVisit(unsigned panel);
    void FinishNavigation(unsigned panel, HRESULT result);
    void CreateTab(unsigned panel, const QString &path, const QString &nativeIdentity,
                   const QtArchiveLocation &archive = {});
    HRESULT OpenArchiveLocation(unsigned panel, const QtArchiveLocation &location);
    HRESULT OpenArchiveDirectory(unsigned panel, const QtArchiveLocation &location);
    HRESULT OpenTabLocation(unsigned panel);
    bool LocationMatches(unsigned panel, const QString &path);
    QString TabTitle(const Tab &tab) const;
    void Activate(unsigned panel, int tab);
    void Suspend(unsigned panel);
    void Rebuild(unsigned panel);
    void TabMenu(unsigned panel, const QPoint &point);
    void DetachTab(const QString &id);
    void CreateActions(unsigned panel, QVBoxLayout &layout);
    void RefreshChrome();
    QWidget *CreateSidebar(unsigned panel);
    void RefreshSidebar(unsigned panel);
    void AddFavorite();
    void OpenFavorite(const QString &path, int target);
    void OpenRecentPlace(const QString &id, const QtRecentPlace &place,
                         QPointer<QtRecentPlacesDialog> target);
    void WhenReady(std::function<void()> body, std::function<void()> canceled = {});
    void RestoreTabFocus(unsigned panel, const QString &id, quint64 request);
    void RestoreListFocus(unsigned panel, const QString &id, quint64 request,
                          QPointer<QWidget> widget);
    void RestorePanel(unsigned panel, const QJsonObject &saved, std::function<void()> finished,
                      std::function<void()> canceled);
    void CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result);
    void RetryLocation(unsigned panel, bool parent);
    MainWindow &window_;
    std::array<Panel, 2> panels_;
    bool changing_ = false;
    QtFavoriteStore *favorites_ = nullptr;
    QtFastFoldersStore *fastFolders_ = nullptr;
    QtNativePlaces *nativePlaces_ = nullptr;
    QtRecentPlacesStore *recentPlaces_ = nullptr;
    QPointer<QtRecentPlacesDialog> recentDialog_;
    bool sidebarVisible_ = true;
    quint64 focusRequest_ = 0;
    std::array<std::unique_ptr<Navigation>, 2> navigation_;
    bool restoringNavigation_ = false;
};
