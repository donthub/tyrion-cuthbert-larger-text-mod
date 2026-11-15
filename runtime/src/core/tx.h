// A transaction over Ui changes: every mutation records how to undo it, so a
// feature that fails half-way can be rolled back without leaving partial
// changes (FR-RT-6, FR-RT-7).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui.h"

namespace ltm {

class Tx {
  public:
    explicit Tx(Ui &ui) : ui_(ui) {}
    Tx(const Tx &) = delete;
    Tx &operator=(const Tx &) = delete;
    ~Tx();  // rolls back unless committed

    Ui &ui() { return ui_; }

    void set(Handle go, Comp c, Prop p, double value);
    void set_rect(Handle go, const RectData &r);
    void set_parent(Handle go, Handle parent, int sibling_index);
    void add(Handle go, Comp c);
    Handle create(const std::string &name, Handle parent);
    Handle clone(Handle original, Handle parent, const std::string &name);
    void set_linked_text(Handle go, Handle target);
    void set_string(Handle go, Comp c, const std::string &field, const std::string &value);
    void add_settings_tab(Handle menu, const std::string &tab, Handle prefab, Handle button);

    void commit() { undo_.clear(); committed_ = true; }
    // Undoes everything recorded so far, newest first. Errors during rollback
    // are collected, not thrown; the return value is the number of failures.
    int rollback();
    size_t changes() const { return undo_.size(); }

  private:
    Ui &ui_;
    std::vector<std::function<void()>> undo_;
    bool committed_ = false;
};

}  // namespace ltm
