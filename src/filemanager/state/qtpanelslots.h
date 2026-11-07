#pragma once
#include "filemanager/panels/Panel.h"
#include <array>
#include <memory>

// CApp owns two stable source panels. Work in flight can retain its panel.
class QtPanelSlots {
    std::array<std::shared_ptr<CPanel>, 2> slots_{std::make_shared<CPanel>(),
                                                  std::make_shared<CPanel>()};

  public:
    CPanel &operator[](unsigned index) const { return *slots_[index]; }
    std::shared_ptr<CPanel> Share(unsigned index) const { return slots_[index]; }
    struct Iterator {
        std::shared_ptr<CPanel> *position;
        CPanel &operator*() const { return **position; }
        Iterator &operator++() {
            ++position;
            return *this;
        }
        bool operator!=(const Iterator &other) const { return position != other.position; }
    };
    Iterator begin() { return {slots_.data()}; }
    Iterator end() { return {slots_.data() + slots_.size()}; }
};
