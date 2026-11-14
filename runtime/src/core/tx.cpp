#include "tx.h"

namespace ltm {

Tx::~Tx() {
    if (!committed_) rollback();
}

void Tx::set(Handle go, Comp c, Prop p, double value) {
    double old = ui_.get(go, c, p);
    if (old == value) return;
    ui_.set(go, c, p, value);
    undo_.push_back([this, go, c, p, old] { ui_.set(go, c, p, old); });
}

void Tx::set_rect(Handle go, const RectData &r) {
    RectData old = ui_.rect(go);
    ui_.set_rect(go, r);
    undo_.push_back([this, go, old] { ui_.set_rect(go, old); });
}

void Tx::set_parent(Handle go, Handle parent, int sibling_index) {
    Handle old_parent = ui_.parent(go);
    int old_index = ui_.sibling_index(go);
    ui_.set_parent(go, parent, sibling_index);
    undo_.push_back([this, go, old_parent, old_index] { ui_.set_parent(go, old_parent, old_index); });
}

void Tx::add(Handle go, Comp c) {
    ui_.add(go, c);
    undo_.push_back([this, go, c] { ui_.remove(go, c); });
}

Handle Tx::create(const std::string &name, Handle parent) {
    Handle go = ui_.create(name, parent);
    undo_.push_back([this, go] { ui_.destroy(go); });
    return go;
}

Handle Tx::clone(Handle original, Handle parent, const std::string &name) {
    Handle go = ui_.clone(original, parent, name);
    undo_.push_back([this, go] { ui_.destroy(go); });
    return go;
}

void Tx::set_linked_text(Handle go, Handle target) {
    Handle old = ui_.linked_text(go);
    ui_.set_linked_text(go, target);
    undo_.push_back([this, go, old] { ui_.set_linked_text(go, old); });
}

int Tx::rollback() {
    int failures = 0;
    while (!undo_.empty()) {
        auto undo = std::move(undo_.back());
        undo_.pop_back();
        try {
            undo();
        } catch (...) {
            ++failures;
        }
    }
    return failures;
}

}  // namespace ltm
