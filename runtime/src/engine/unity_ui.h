// The game implementation of ltm::Ui, backed by IL2CPP.
#pragma once
#include <cstdint>
#include <map>
#include <vector>

#include "../core/ui.h"
#include "unity.h"

namespace ltm::engine {

struct UnityClasses {
    Class *Object = nullptr, *GameObject = nullptr, *Component = nullptr, *Transform = nullptr,
          *RectTransform = nullptr, *Behaviour = nullptr;
    Class *comp[static_cast<int>(Comp::Count)] = {};
    // Resolves everything; returns the names of missing classes.
    std::vector<std::string> resolve();
};

class UnityUi : public Ui {
  public:
    explicit UnityUi(const UnityClasses &classes) : k_(classes) {}

    // Handles returned to the core are pinned until end_call(), so the garbage
    // collector can't free a wrapper the core still refers to.
    void end_call();

    Handle find(Handle root, const std::string &path) override;
    std::string name(Handle go) override;
    Handle parent(Handle go) override;
    int sibling_index(Handle go) override;
    int child_count(Handle go) override;
    Handle child(Handle go, int index) override;
    void set_parent(Handle go, Handle new_parent, int sibling_index) override;
    bool active_self(Handle go) override;
    bool active_in_hierarchy(Handle go) override;
    int instance_id(Handle go) override;
    bool alive(Handle go) override;
    void retain(Handle go) override;
    void release(Handle go) override;
    void set_active(Handle go, bool active) override;

    bool has(Handle go, Comp c) override;
    void add(Handle go, Comp c) override;
    void remove(Handle go, Comp c) override;
    double get(Handle go, Comp c, Prop p) override;
    void set(Handle go, Comp c, Prop p, double value) override;

    RectData rect(Handle go) override;
    void set_rect(Handle go, const RectData &r) override;
    Vec2 rect_size(Handle go) override;
    Vec2 local_scale(Handle go) override;
    Bounds bounds_in(Handle go, Handle ref) override;

    Handle create(const std::string &name, Handle parent) override;
    Handle clone(Handle original, Handle parent, const std::string &name) override;
    void destroy(Handle go) override;

    Handle linked_text(Handle go) override;
    void set_linked_text(Handle go, Handle target) override;
    void update_text(Handle go, Comp c) override;
    const void *text_identity(Handle go, Comp c) override;
    std::string text(Handle go, Comp c) override;
    void show_text_until(Handle go, double read_time) override;
    int enum_value(Handle go, Comp c, Prop p, const std::string &member) override;

    // The GameObject a component belongs to.
    Handle game_object_of(Object *component);
    bool has_component(Object *game_object, Comp c) { return has(game_object, c); }

  private:
    Handle pin(Object *go);
    Object *transform(Handle go);
    Object *rect_transform(Handle go);
    Object *component(Handle go, Comp c);  // throws when missing
    Class *klass(Comp c);

    const UnityClasses &k_;
    std::vector<il2cpp::GCHandle> temp_;
    std::map<Handle, std::pair<il2cpp::GCHandle, int>> retained_;
};

}  // namespace ltm::engine
