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

    Handle create(const std::string &name, Handle parent) override { return node(name, N(parent)); }
    Handle clone(Handle original, Handle parent, const std::string &name) override {
        Node *src = N(original);
        Node *n = node(name, N(parent));
        n->rect = src->rect;
        n->comps = src->comps;
        n->active = src->active;
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

    std::set<Node *> overflowing;

  private:
    std::vector<std::unique_ptr<Node>> nodes_;
    int next_id_ = 0;
};

}  // namespace ltm::test
