#pragma once
#include "filemanager/panels/Panel.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <array>
#include <vector>

class MainWindow;
class QAbstractItemView;
class QTabBar;
class QVBoxLayout;
class QToolBar;
class QListView;
class QtFavoriteStore;
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
    void Restore(const QJsonObject &state, std::function<void()> finished = {});
    void EditLocation();
    QString CurrentLocation();
    void AttachContent(unsigned panel, QVBoxLayout &layout, QWidget &status);
    bool WithSource(CPanel &source, const std::function<bool()> &body);
    bool SourceIsActive(const CPanel &source) const;
    void MarkDirty(CPanel &source);
    void RestoreViewPosition(CPanel &source, QAbstractItemView &view, const QPoint &position);
    void ExitExternalWorkers(bool hardExit);
    void QuiesceInactive();
    void Dispose();
    bool TransferTab(const QString &id, QtWindowWorkspace &target, unsigned targetPanel);
    bool PreparePanelForShow(unsigned panel);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct Tab {
        QString id;
        QString path;
        QStringList history;
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
    void Capture(unsigned panel, bool retainView = true);
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
    void WhenReady(std::function<void()> body);
    void RestoreTabFocus(unsigned panel, const QString &id, quint64 request);
    void RestorePanel(unsigned panel, const QJsonObject &saved, std::function<void()> finished);
    void CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result);
    void RetryLocation(unsigned panel, bool parent);
    MainWindow &window_;
    std::array<Panel, 2> panels_;
    bool changing_ = false;
    QtFavoriteStore *favorites_ = nullptr;
    bool sidebarVisible_ = true;
    quint64 focusRequest_ = 0;
};
