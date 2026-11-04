#pragma once
#include "filemanager/panels/Panel.h"
#include <array>
#include <memory>

// The native application still addresses exactly two panel slots. A tab owns
// a stable panel object; changing the slot never moves a QObject or live guard.
class QtPanelSlots {
    std::array<std::shared_ptr<CPanel>, 2> slots_{std::make_shared<CPanel>(),
                                                  std::make_shared<CPanel>()};

  public:
    CPanel &operator[](unsigned index) const { return *slots_[index]; }
    std::shared_ptr<CPanel> Share(unsigned index) const { return slots_[index]; }
    void Bind(unsigned index, std::shared_ptr<CPanel> panel) { slots_[index] = std::move(panel); }
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
