#pragma once

#include <memory>

class CPanel;

// Preserve the live source while probing a different physical archive.
// Navigation within an existing container keeps the normal native route.
class QtPanelLocationTransaction final {
  public:
    explicit QtPanelLocationTransaction(CPanel &panel);
    ~QtPanelLocationTransaction();
    QtPanelLocationTransaction(const QtPanelLocationTransaction &) = delete;
    QtPanelLocationTransaction &operator=(const QtPanelLocationTransaction &) = delete;
    void Commit();
    void Rollback();

  private:
    struct Location;
    void Exchange();
    CPanel &panel_;
    std::unique_ptr<Location> previous_;
    bool finished_ = false;
};
