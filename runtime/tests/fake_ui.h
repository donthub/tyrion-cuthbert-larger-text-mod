// An in-memory stand-in for the game's UI, used by the unit tests.
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../src/core/ui.h"

namespace ltm::test {

struct Node {
    std::string name;
    Node *parent = nullptr;
    std::vector<Node *> children;
    bool active = true;
    int id = 0;
    bool destroyed = false;
    RectData rect;
    Vec2 scale{1, 1};
    std::map<Comp, std::map<Prop, double>> comps;
    Node *linked = nullptr;
    int text_version = 0;
    // Rendered text height per font-size unit (0: a fixed 40), to test fitting.
    double text_lines = 0;
    std::string text = "Text";  // what the game assigned
};

class FakeUi : public Ui {
  public:
    // Failure injection: throw EngineError on set() of this property.
    Prop fail_on = Prop::Count;
    int fail_after = 0;  // number of successful sets before failing
    int sets = 0;
    std::set<Handle> retained;

    Node *node(const std::string &name, Node *parent = nullptr) {
        auto n = std::make_unique<Node>();
        n->name = name;
        n->id = ++next_id_;
        n->rect.pivot = {0.5f, 0.5f};
        Node *raw = n.get();
        nodes_.push_back(std::move(n));
        if (parent) {
            raw->parent = parent;
            parent->children.push_back(raw);
        }
        return raw;
    }
    // Creates the nodes of a slash-separated path below root as needed.
    Node *path(Node *root, const std::string &p) {
        Node *cur = root;
        size_t start = 0;
        while (start <= p.size() && !p.empty()) {
            size_t slash = p.find('/', start);
            std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            Node *next = nullptr;
            for (Node *c : cur->children)
                if (c->name == part) next = c;
            if (!next) next = node(part, cur);
            cur = next;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return cur;
    }
    static Node *N(Handle h) { return static_cast<Node *>(h); }
    double &prop(Node *n, Comp c, Prop p) { return n->comps[c][p]; }

    // Ui ------------------------------------------------------------------
    Handle find(Handle root, const std::string &p) override {
        Node *cur = N(root);
        if (p.empty()) return cur;
        size_t start = 0;
        while (true) {
            size_t slash = p.find('/', start);
            std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            Node *next = nullptr;
            for (Node *c : cur->children)
                if (c->name == part && !c->destroyed) next = c;
            if (!next) return nullptr;
            cur = next;
            if (slash == std::string::npos) return cur;
            start = slash + 1;
        }
    }
    std::string name(Handle go) override { return N(go)->name; }
    void set_name(Handle go, const std::string &n) override { N(go)->name = n; }
    Handle parent(Handle go) override { return N(go)->parent; }
    int sibling_index(Handle go) override {
        Node *n = N(go);
        if (!n->parent) return 0;
        auto &v = n->parent->children;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == n) return static_cast<int>(i);
        return -1;
    }
    int child_count(Handle go) override { return static_cast<int>(N(go)->children.size()); }
    Handle child(Handle go, int i) override { return N(go)->children.at(static_cast<size_t>(i)); }
    void set_parent(Handle go, Handle np, int index) override {
        Node *n = N(go);
        if (n->parent) {
            auto &v = n->parent->children;
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == n) {
                    v.erase(v.begin() + static_cast<long>(i));
                    break;
                }
        }
        n->parent = N(np);
        auto &v = n->parent->children;
        if (index < 0 || index > static_cast<int>(v.size())) v.push_back(n);
        else v.insert(v.begin() + index, n);
    }
    bool active_self(Handle go) override { return N(go)->active; }
    bool active_in_hierarchy(Handle go) override {
        for (Node *n = N(go); n; n = n->parent)
            if (!n->active) return false;
        return true;
    }
    int instance_id(Handle go) override { return N(go)->id; }
    bool alive(Handle go) override { return !N(go)->destroyed; }
    void retain(Handle go) override { retained.insert(go); }
    void release(Handle go) override { retained.erase(go); }
    void set_active(Handle go, bool a) override { N(go)->active = a; }

    bool has(Handle go, Comp c) override { return N(go)->comps.count(c) > 0; }
    void add(Handle go, Comp c) override { N(go)->comps[c]; }
    void remove(Handle go, Comp c) override { N(go)->comps.erase(c); }
    double get(Handle go, Comp c, Prop p) override {
        Node *n = N(go);
        if (!n->comps.count(c)) throw EngineError("no component");
        if (p == Prop::TmpOverflowing) return overflowing.count(n) ? 1 : 0;
        if (p == Prop::TmpRenderedHeight) return n->text_lines > 0 ? n->comps[c][Prop::TmpFontSize] * n->text_lines : 40;
        if (p == Prop::TmpPreferredHeight) return n->comps[c][Prop::TmpFontSize] * 1.2;
        if (p == Prop::TmpLineHeight || p == Prop::TmpLinePitch) return n->comps[c][Prop::TmpFontSize] * 1.2;
        auto &props = n->comps[c];
        auto it = props.find(p);
        return it == props.end() ? 0.0 : it->second;
    }
    void set(Handle go, Comp c, Prop p, double v) override {
        Node *n = N(go);
        if (!n->comps.count(c)) throw EngineError("no component");
        if (p == fail_on && sets >= fail_after) throw EngineError("injected failure");
        ++sets;
        n->comps[c][p] = v;
    }
    RectData rect(Handle go) override { return N(go)->rect; }
    void set_rect(Handle go, const RectData &r) override { N(go)->rect = r; }
    Vec2 rect_size(Handle go) override {
        // Good enough for the tests: anchors span a 1000x500 parent.
        const RectData &r = N(go)->rect;
        return {(r.anchorMax.x - r.anchorMin.x) * 1000 + r.sizeDelta.x,
                (r.anchorMax.y - r.anchorMin.y) * 500 + r.sizeDelta.y};
    }
    Vec2 local_scale(Handle go) override { return N(go)->scale; }
    // Tests place nodes in one shared space via `bounds`; others sit at the
    // origin with their rect size. `ref` is ignored: only differences matter.
    Bounds bounds_in(Handle go, Handle) override {
        auto it = bounds.find(N(go));
        if (it != bounds.end()) return it->second;
        Vec2 s = rect_size(go);
        return {0, 0, s.x, s.y};
    }

    Handle create(const std::string &name, Handle parent) override { return node(name, N(parent)); }
    Handle clone(Handle original, Handle parent, const std::string &name) override {
        Node *n = copy(N(original), N(parent));
        n->name = name;
        return n;
    }
    // Deep copy, like Object.Instantiate.
    Node *copy(Node *src, Node *parent) {
        Node *n = node(src->name, parent);
        n->rect = src->rect;
        n->comps = src->comps;
        n->active = src->active;
        n->text = src->text;
        n->text_lines = src->text_lines;
        for (auto &e : std::map<std::pair<Node *, std::string>, std::string>(strings))
            if (e.first.first == src) strings[{n, e.first.second}] = e.second;
        for (auto &e : std::map<std::pair<Node *, std::string>, std::vector<std::string>>(string_lists))
            if (e.first.first == src) string_lists[{n, e.first.second}] = e.second;
        if (dropdown_options.count(src)) dropdown_options[n] = dropdown_options[src];
        for (Node *c : std::vector<Node *>(src->children))
            if (!c->destroyed) copy(c, n);
        return n;
    }
    void destroy(Handle go) override {
        Node *n = N(go);
        n->destroyed = true;
        if (n->parent) {
            auto &v = n->parent->children;
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == n) {
                    v.erase(v.begin() + static_cast<long>(i));
                    break;
                }
        }
    }
    Handle linked_text(Handle go) override { return N(go)->linked; }
    void set_linked_text(Handle go, Handle t) override { N(go)->linked = N(t); }
    void update_text(Handle go, Comp) override { ++N(go)->text_version; }
    const void *text_identity(Handle go, Comp) override { return reinterpret_cast<const void *>(N(go)->comps.size() + N(go)->text_version * 1000); }
    std::string text(Handle go, Comp) override { return N(go)->text; }
    void show_text_until(Handle go, double t) override { shown_until[N(go)] = t; }
    std::map<Node *, double> shown_until;
    int enum_value(Handle go, Comp c, Prop p, const std::string &member) override {
        if (!N(go)->comps.count(c)) throw EngineError("no component");
        // SuperTextMesh.BestFitMode in declaration order.
        static const char *const modes[] = {"Off", "Always", "OverLimit", "SquishAlways",
                                            "SquishOverLimit", "MultilineEven", "MultilineFull"};
        if (p == Prop::StmBestFit)
            for (int i = 0; i < 7; ++i)
                if (member == modes[i]) return i;
        throw Mismatch("no enum value " + member);
    }

    void set_text(Handle go, const std::string &t) override { N(go)->text = t; }
    void set_dropdown_options(Handle go, const std::vector<std::string> &labels) override {
        if (!N(go)->comps.count(Comp::Dropdown)) throw EngineError("no dropdown");
        dropdown_options[N(go)] = labels;
    }
    std::string get_string(Handle go, Comp c, const std::string &field) override {
        if (!N(go)->comps.count(c)) throw Mismatch("no component");
        auto it = strings.find({N(go), field});
        return it == strings.end() ? "" : it->second;
    }
    void set_string(Handle go, Comp c, const std::string &field, const std::string &v) override {
        if (!N(go)->comps.count(c)) throw Mismatch("no component");
        strings[{N(go), field}] = v;
    }
    void set_strings(Handle go, Comp c, const std::string &field, const std::vector<std::string> &v) override {
        if (!N(go)->comps.count(c)) throw Mismatch("no component");
        string_lists[{N(go), field}] = v;
    }
    Handle holder() override {
        if (!holder_) {
            holder_ = node("LTM Holder");
            holder_->active = false;
        }
        return holder_;
    }

    // Settings menu stand-in.
    struct Tab {
        std::string name;
        Node *prefab, *button;
    };
    std::map<Node *, std::vector<Tab>> menu_tabs;
    std::map<Node *, std::string> current_tab;
    std::map<Node *, Node *> loaded_tab;
    std::map<Node *, std::vector<Node *>> tab_options;
    std::vector<std::string> settings_tabs(Handle menu) override {
        std::vector<std::string> names;
        for (auto &t : menu_tabs[N(menu)]) names.push_back(t.name);
        return names;
    }
    Handle settings_tab_prefab(Handle menu, const std::string &tab) override {
        for (auto &t : menu_tabs[N(menu)])
            if (t.name == tab) return t.prefab;
        return nullptr;
    }
    void add_settings_tab(Handle menu, const std::string &tab, Handle prefab, Handle button) override {
        if (fail_add_tab) throw EngineError("injected failure");
        menu_tabs[N(menu)].push_back({tab, N(prefab), N(button)});
    }
    void remove_settings_tab(Handle menu, const std::string &tab) override {
        auto &v = menu_tabs[N(menu)];
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].name == tab) {
                v.erase(v.begin() + static_cast<long>(i));
                return;
            }
    }
    std::string current_settings_tab(Handle menu) override { return current_tab[N(menu)]; }
    Handle loaded_settings_tab(Handle menu) override { return loaded_tab[N(menu)]; }
    void set_settings_options(Handle tab, const std::vector<Handle> &options) override {
        auto &v = tab_options[N(tab)];
        v.clear();
        for (Handle h : options) v.push_back(N(h));
    }
    // What the game does when a tab button is selected.
    Node *load_tab(Node *menu, const std::string &name, Node *container) {
        for (auto &t : menu_tabs[menu])
            if (t.name == name) {
                Node *n = copy(t.prefab, container);
                n->name = t.prefab->name + "(Clone)";
                n->active = true;
                current_tab[menu] = name;
                loaded_tab[menu] = n;
                return n;
            }
        return nullptr;
    }
    bool fail_add_tab = false;

    std::map<std::pair<Node *, std::string>, std::string> strings;
    std::map<std::pair<Node *, std::string>, std::vector<std::string>> string_lists;
    std::map<Node *, std::vector<std::string>> dropdown_options;

    std::set<Node *> overflowing;
    std::map<Node *, Bounds> bounds;

  private:
    Node *holder_ = nullptr;
    std::vector<std::unique_ptr<Node>> nodes_;
    int next_id_ = 0;
};

}  // namespace ltm::test
