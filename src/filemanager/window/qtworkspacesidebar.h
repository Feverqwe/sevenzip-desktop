#pragma once
#include <QCoreApplication>
#include <QScrollArea>
#include <functional>

class QtFavoriteStore;
class QtFavoriteList;
class QtNativePlaces;
class QtPlaceProjection;
class QListView;
class QVBoxLayout;

enum class QtPlaceTarget { Current, NewTab, NewWindow };
struct QtPlaceActions {
    std::function<void()> add;
    std::function<void(const QString &, QtPlaceTarget)> open;
};

// Shared places presentation. Callbacks leave navigation and operation guards
// with the workspace; the favorites model retains persistence and identities.
class QtPlacesSidebar final : public QScrollArea {
    Q_DECLARE_TR_FUNCTIONS(QtWindowWorkspace)
  public:
    QtPlacesSidebar(QtFavoriteStore &favorites, QtNativePlaces &locations, QtPlaceActions actions,
                    unsigned panel, QWidget *parent = nullptr);
    void SetCompact(bool compact);
    void SetCurrentLocation(const QString &path, const QString &nativeIdentity = {});

  private:
    void CreateFavorites(QVBoxLayout &layout, unsigned panel);
    void ShowFavoriteMenu(const QPoint &point);
    void RefreshLocations();
    QtFavoriteStore &favorites_;
    QtNativePlaces &nativePlaces_;
    QtPlaceActions actions_;
    QtFavoriteList *favoritesView_ = nullptr;
    QListView *locations_ = nullptr;
    QtPlaceProjection *favoritePlaces_ = nullptr;
    QtPlaceProjection *locationPlaces_ = nullptr;
};
