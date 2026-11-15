// Unit tests for the runtime core. No game files needed.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../src/core/config.h"
#include "../src/core/framework.h"
#include "../src/core/layout.h"
#include "fake_ui.h"

using namespace ltm;
using namespace ltm::test;

// --- minimal test harness ----------------------------------------------------

static int g_failures = 0, g_checks = 0;
static std::vector<std::pair<const char *, std::function<void()>>> &tests() {
    static std::vector<std::pair<const char *, std::function<void()>>> t;
    return t;
}
struct Register {
    Register(const char *n, std::function<void()> f) { tests().emplace_back(n, std::move(f)); }
};
#define TEST(name)                                  \
    static void name();                             \
    static Register reg_##name(#name, name);        \
    static void name()
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(cond)) {                                                                   \
            ++g_failures;                                                                \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                                \
    } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) < 0.01)

struct MemLog : Log {
    std::vector<std::string> lines;
    void write(Level, const std::string &m) override { lines.push_back(m); }
    bool has(const std::string &s) const {
        for (auto &l : lines)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
    int count(const std::string &s) const {
        int n = 0;
        for (auto &l : lines)
            if (l.find(s) != std::string::npos) ++n;
        return n;
    }
};

static RectData R(Vec2 amin, Vec2 amax, Vec2 pos, Vec2 size, Vec2 pivot) { return RectData{amin, amax, pos, size, pivot}; }

// --- stand-in prefabs, with the values of the verified game build -------------

static void tmp(FakeUi &ui, Node *n, double size, bool autosize, double mn, double mx) {
    auto &c = n->comps[Comp::Tmp];
    c[Prop::TmpFontSize] = size;
    c[Prop::TmpAutoSize] = autosize;
    c[Prop::TmpFontSizeMin] = mn;
    c[Prop::TmpFontSizeMax] = mx;
    c[Prop::TmpCharacterSpacing] = 4;
    (void)ui;
}

enum class Box { Dialogue, Narrator, Testimony };

static Node *make_box(FakeUi &ui, Box kind) {
    Node *root = ui.node(kind == Box::Narrator ? "Narrator Box Variant(Clone)" : kind == Box::Testimony ? "Testimony GUI" : "Dialogue Box(Clone)");
    root->comps[Comp::DialogueBox];
    if (kind == Box::Testimony) root->comps[Comp::TestimonyPrompt];
    Node *c = ui.path(root, "Container");
    c->comps[Comp::RawImage][Prop::GraphicAlpha] = 1;
    c->rect = kind == Box::Narrator ? R({0, 0}, {1, 0}, {0, 50}, {0, 240}, {0.5f, 0})
                                    : R({0.5f, 0}, {0.5f, 0}, {0, 50}, {1554, 240}, {0.5f, 0});
    Node *info = ui.path(root, "Container/Character Info");
    info->rect = R({0, 1}, {1, 1}, {267.1f, -40}, {-704.2f, 25.5f}, {0.5f, 1});
    for (const char *l : {"Occupation", "Age", "Arcane Art"}) {
        Node *n = ui.path(info, l);
        n->rect = R({0, 1}, {0, 1}, {382, -12.75f}, {0, 25.5f}, {0.5f, 0.5f});
        tmp(ui, n, 20, false, 18, 72);
    }
    Node *np = ui.path(root, "Container/Nameplate");
    np->rect = R({0, 1}, {0, 1}, {176, 0}, {358.4f, 57.1f}, {0, 1});
    tmp(ui, ui.path(np, "Text"), 36, true, 18, 42);
    Node *d = ui.path(root, "Container/Dialogue");
    d->rect = R({0, 0.5f}, {1, 0.5f}, {185, -10}, {-370, 70}, {0, 0.5f});
    d->comps[Comp::Stm][Prop::StmSize] = 26;
    d->comps[Comp::Stm][Prop::StmCharacterSpacing] = 0.025;
    Node *b = ui.path(root, "Container/Buttons");
    b->rect = R({0, 0}, {1, 0}, {362.5f, 22.5f}, {-1025, 70}, {0.5f, 0.5f});
    if (kind == Box::Testimony) {
        for (const char *s : {"Press", "Present"}) {
            Node *p = ui.path(root, std::string("Container/") + s);
            p->comps[Comp::Button];
            p->comps[Comp::AspectFitter][Prop::AspectMode] = 2;
            p->comps[Comp::AspectFitter][Prop::Enabled] = 1;
            p->rect = R({0, 1}, {0, 1}, {0, 40}, {0, 292}, {0.33f, 0});
        }
        Node *l = ui.path(root, "Container/Left");
        l->comps[Comp::Button];
        l->rect = R({0, 0}, {0, 1}, {-60, 0}, {180, -20}, {0.5f, 0.5f});
        l->scale = {-1, 1};
        Node *r = ui.path(root, "Container/Right");
        r->comps[Comp::Button];
        r->rect = R({1, 0}, {1, 1}, {60, 0}, {180, -20}, {0.5f, 0.5f});
    }
    return root;
}

static Node *make_image(FakeUi &ui) {
    Node *root = ui.node("Image Display(Clone)");
    root->comps[Comp::ImageDisplay];
    Node *img = ui.path(root, "Image");
    img->rect = R({0, 0}, {1, 1}, {0, 125}, {0, -350}, {0.5f, 0.5f});
    img->comps[Comp::AspectFitter][Prop::AspectMode] = 2;
    img->comps[Comp::AspectFitter][Prop::Enabled] = 1;
    return root;
}

static Node *make_log(FakeUi &ui) {
    Node *root = ui.node("Text Log(Clone)");
    root->comps[Comp::TextLogGUI];
    ui.path(root, "Scroll View")->rect = R({0.125f, 0.165f}, {0.875f, 0.835f}, {0, 0}, {0, 0}, {0.5f, 0.5f});
    Node *content = ui.path(root, "Scroll View/Viewport/Content");
    content->comps[Comp::VLayout][Prop::LgSpacing] = 25;
    Node *entry = ui.path(content, "Text Entry");
    entry->comps[Comp::VLayout][Prop::LgSpacing] = 15;
    entry->active = false;
    for (const char *l : {"Char Name", "Text", "Text 2", "Large Text"}) {
        Node *n = ui.path(entry, l);
        n->comps[Comp::SizeFitter][Prop::FitVertical] = 2;
        n->rect = R({0, 1}, {0, 1}, {714, 0}, {1428, 40}, {0.5f, 1});
        if (std::string(l) == "Text" || std::string(l) == "Text 2") {
            n->comps[Comp::Stm][Prop::StmSize] = 25;
            n->comps[Comp::Stm][Prop::StmCharacterSpacing] = 0.2;
        } else {
            tmp(ui, n, std::string(l) == "Char Name" ? 36 : 56, false, 18, 72);
        }
    }
    ui.path(content, "Divider");
    return root;
}

static Node *make_menu(FakeUi &ui) {
    Node *root = ui.node("Item Menu(Clone)");
    root->comps[Comp::ItemMenu];
    Node *base = ui.path(root, "Container/Right/Container");
    base->rect = R({0, 0}, {1, 1}, {0, 0}, {0, 0}, {0.5f, 0.5f});
    Node *top = ui.path(base, "Top");
    top->comps[Comp::HLayout][Prop::LgSpacing] = 10;
    top->comps[Comp::HLayout][Prop::LgExpandWidth] = 1;
    top->rect = R({0, 0.66f}, {1, 1}, {0, 0}, {0, 0}, {0.5f, 0.5f});
    Node *icon = ui.path(top, "Icon");
    icon->comps[Comp::AspectFitter][Prop::AspectMode] = 2;
    icon->rect = R({0, 0}, {0, 0}, {0, 0}, {170, 0}, {0, 0.5f});
    Node *info = ui.path(top, "Item Info");
    info->rect = R({0, 0}, {0, 0}, {0, 0}, {471.6f, 170}, {0.5f, 0.5f});  // as the row lays it out
    Node *name = ui.path(info, "Item Name");
    name->rect = R({0, 1}, {1, 1}, {0, 15}, {0, 65}, {0.5f, 1});
    tmp(ui, name, 47, true, 18, 47);
    Node *fields = ui.path(info, "Fields");
    fields->rect = R({0, 1}, {1, 1}, {0, -90}, {0, 80}, {0.5f, 1});
    fields->comps[Comp::VLayout][Prop::LgSpacing] = 0;
    for (const char *row : {"Item Field", "Item Field (1)"}) {
        Node *r = ui.path(fields, row);
        r->rect.sizeDelta.y = 40;
        tmp(ui, ui.path(r, "Item Field Names"), 30, true, 18, 30);
        tmp(ui, ui.path(r, "Item Field Values"), 30, true, 18, 30);
    }
    Node *view = ui.path(info, "View Button");
    view->comps[Comp::Button];
    view->rect = R({0, 0}, {0, 0}, {100, 25}, {200, 50}, {0.5f, 0.5f});
    Node *desc = ui.path(base, "Description");
    desc->rect = R({0, 0}, {1, 0.66f}, {0, -12.5f}, {0, -25}, {0.5f, 0.5f});
    tmp(ui, desc, 24, true, 18, 24);
    return root;
}

static Node *make_popup(FakeUi &ui) {
    Node *root = ui.node("Add Item Popup(Clone)");
    root->comps[Comp::AddItemDisplay];
    Node *w = ui.path(root, "Window");
    w->rect = R({0.165f, 0.825f}, {0.835f, 0.825f}, {2000, 0}, {0, 0}, {0.5f, 1});
    w->comps[Comp::VLayout][Prop::LgSpacing] = 25;
    w->comps[Comp::VLayout][Prop::LgControlHeight] = 0;
    w->comps[Comp::SizeFitter][Prop::FitVertical] = 1;
    Node *h = ui.path(w, "Header");
    h->rect = R({0, 1}, {1, 1}, {-25, 0}, {-50, 100}, {0.5f, 0});
    tmp(ui, h, 71.21, false, 18, 72);
    Node *top = ui.path(w, "Top");
    top->rect = R({0, 0}, {0, 0}, {0, 0}, {0, 270}, {0.5f, 1});
    ui.path(top, "Icon")->rect = R({0, 1}, {0, 1}, {0, 0}, {270, 270}, {0, 1});
    Node *info = ui.path(top, "Item Info");
    info->rect = R({0, 0}, {1, 1}, {150, 0}, {-300, 0}, {0.5f, 1});
    Node *name = ui.path(info, "Item Name");
    name->rect = R({0, 1}, {1, 1}, {0, 0}, {0, 50}, {0.5f, 1});
    tmp(ui, name, 47, true, 18, 47);
    Node *fields = ui.path(info, "Item Fields");
    fields->rect = R({0, 1}, {1, 1}, {0, -60}, {0, 135}, {0.5f, 1});
    fields->comps[Comp::VLayout][Prop::LgSpacing] = 0;
    for (const char *row : {"Item Field", "Item Field (1)", "Item Field (2)"}) {
        Node *r = ui.path(fields, row);
        r->rect.sizeDelta.y = 45;
        tmp(ui, ui.path(r, "Item Field Names"), 30, true, 18, 30);
        tmp(ui, ui.path(r, "Item Field Values"), 30, true, 18, 30);
    }
    Node *desc = ui.path(w, "Description");
    desc->rect = R({0, 1}, {0, 1}, {643, -345}, {1186, 0}, {0.5f, 0.5f});
    tmp(ui, desc, 24, false, 12, 28);
    desc->comps[Comp::SizeFitter][Prop::FitVertical] = 2;
    desc->comps[Comp::SizeFitter][Prop::Enabled] = 1;
    return root;
}

static Settings settings(double scale = 1.5, double opacity = 0.99) {
    Settings s;
    s.scale = scale;
    s.opacity = opacity;
    return s;
}

// Snapshot of every node's rect and properties, to detect any change.
static std::string snapshot(Node *n) {
    std::string out = n->name + ":" + std::to_string(n->active);
    const RectData &r = n->rect;
    for (float v : {r.anchorMin.x, r.anchorMin.y, r.anchorMax.x, r.anchorMax.y, r.anchoredPosition.x,
                    r.anchoredPosition.y, r.sizeDelta.x, r.sizeDelta.y, r.pivot.x, r.pivot.y})
        out += "," + std::to_string(v);
    for (auto &c : n->comps)
        for (auto &p : c.second) out += "|" + std::to_string(static_cast<int>(p.second * 1000));
    out += n->linked ? "L" : "";
    out += "{";
    for (Node *ch : n->children) out += snapshot(ch) + ";";
    return out + "}";
}

// --- config -------------------------------------------------------------------

TEST(config_defaults_when_empty) {
    ConfigResult r = parse_config("");
    CHECK(r.settings.enabled);
    CHECK_NEAR(r.settings.scale, 1.5);
    CHECK_NEAR(r.settings.opacity, 0.99);
    for (const char *k : kFeatureKeys) CHECK(r.settings.feature(k));
    CHECK(r.warnings.size() == 2);  // scale and opacity missing
}

TEST(config_valid_values) {
    ConfigResult r = parse_config(
        "\xEF\xBB\xBF; comment\n[general]\nenabled = no\nscale = 1.3  ; inline\nopacity=0\n"
        "[features]\nText_Log = off\nstory_images = 1\n[other]\nfoo = bar\n");
    CHECK(!r.settings.enabled);
    CHECK_NEAR(r.settings.scale, 1.3);
    CHECK_NEAR(r.settings.opacity, 0.0);
    CHECK(!r.settings.feature("text_log"));
    CHECK(r.settings.feature("story_images"));
    CHECK(r.warnings.empty());
}

TEST(config_out_of_range_and_malformed) {
    ConfigResult r = parse_config("[general]\nscale = 2.5\nopacity = abc\nenabled = maybe\n[features]\ndialogue_box = perhaps\nbroken line\n[unterminated\n");
    CHECK_NEAR(r.settings.scale, 1.5);
    CHECK_NEAR(r.settings.opacity, 0.99);
    CHECK(r.settings.enabled);
    CHECK(r.settings.feature("dialogue_box"));
    CHECK(r.warnings.size() == 6);
    ConfigResult limits = parse_config("[general]\nscale = 1.1\nopacity = 1\n");
    CHECK_NEAR(limits.settings.scale, 1.1);
    CHECK_NEAR(limits.settings.opacity, 1.0);
    ConfigResult nan = parse_config("[general]\nscale = nan\nopacity = inf\n");
    CHECK_NEAR(nan.settings.scale, 1.5);
    CHECK_NEAR(nan.settings.opacity, 0.99);
    ConfigResult comma = parse_config("[general]\nscale = 1,2\nopacity = 0.5\n");
    CHECK_NEAR(comma.settings.scale, 1.2);
}

TEST(config_unknown_keys_ignored) {
    ConfigResult r = parse_config("[general]\nscale = 2\nopacity = 0.8\nfuture_option = 7\n[features]\nnew_feature = false\n");
    CHECK(r.warnings.empty());
    CHECK_NEAR(r.settings.scale, 2.0);
}

// --- layout -------------------------------------------------------------------

TEST(layout_dialogue_heights_at_several_scales) {
    layout::DialoguePanel p;
    CHECK_NEAR(layout::dialogue_text_height(p, 1.1), 90);
    CHECK_NEAR(layout::dialogue_panel_height(p, 1.1), 288);
    CHECK_NEAR(layout::dialogue_text_height(p, 1.3), 120);
    CHECK_NEAR(layout::dialogue_panel_height(p, 1.3), 334);
    CHECK_NEAR(layout::dialogue_text_height(p, 1.5), 160);
    CHECK_NEAR(layout::dialogue_panel_height(p, 1.5), 390);
    CHECK_NEAR(layout::dialogue_text_height(p, 2.0), 280);
    CHECK_NEAR(layout::dialogue_panel_height(p, 2.0), 580);  // the info-row minimum wins
    for (double s : {1.1, 1.3, 1.5, 1.7, 2.0}) {
        // Height grows at least with the extra wrapping (scale squared).
        CHECK(layout::dialogue_text_height(p, s) >= p.text_height * s * s - 0.01);
        CHECK(layout::dialogue_panel_height(p, s) >= p.panel_height);
    }
}

TEST(layout_margins_keep_proportions) {
    RectData child = R({0, 0.5f}, {1, 0.5f}, {185, -10}, {-370, 70}, {0, 0.5f});
    RectData r = layout::stretch_horizontally(child, 1554);
    CHECK_NEAR(r.anchorMin.x, 185.0 / 1554);
    CHECK_NEAR(r.anchorMax.x, 1 - 185.0 / 1554);
    CHECK_NEAR(r.sizeDelta.x, 0);
    CHECK_NEAR(r.anchoredPosition.x, 0);
    CHECK_NEAR(r.anchoredPosition.y, -10);
    RectData point = R({0, 1}, {0, 1}, {176, 0}, {358.4f, 57.1f}, {0, 1});
    RectData q = layout::stretch_horizontally(point, 1554);
    CHECK_NEAR(q.anchorMin.x, 176.0 / 1554);
    CHECK_NEAR(q.anchorMax.x, (176.0 + 358.4) / 1554);
}

TEST(layout_image_clearance) {
    RectData img = R({0, 0}, {1, 1}, {0, 125}, {0, -350}, {0.5f, 0.5f});
    CHECK_NEAR(layout::image_bottom(img), 300);
    for (double s : {1.1, 1.5, 2.0}) {
        float top = layout::dialogue_panel_top(layout::DialoguePanel{}, s);
        RectData r = layout::clear_image_above(img, top);
        CHECK(layout::image_bottom(r) >= top + layout::kImageGap - 0.01);
        CHECK_NEAR(-r.anchoredPosition.y - r.sizeDelta.y / 2, 50);  // top margin unchanged
    }
    RectData low = layout::clear_image_above(img, 100);
    CHECK_NEAR(low.sizeDelta.y, -350);  // nothing to clear
}

TEST(layout_text_sizes) {
    layout::TmpSize auto_size{36, 18, 42, true};
    layout::TmpSize s = layout::scale_tmp(auto_size, 1.5, false);
    CHECK_NEAR(s.font_size, 63);
    CHECK_NEAR(s.max, 63);
    CHECK_NEAR(s.min, 27);
    layout::TmpSize k = layout::scale_tmp(auto_size, 1.5, true);
    CHECK_NEAR(k.min, 18);
    layout::TmpSize h = layout::scale_heading(layout::TmpSize{71.21f, 18, 72, false}, 2.0);
    CHECK(h.auto_size);
    CHECK_NEAR(h.min, 71.21);
    CHECK_NEAR(h.max, 142.42);
    CHECK_NEAR(layout::heading_height(100, 1.5, 0.825f), 150);
    CHECK_NEAR(layout::heading_height(100, 2.0, 0.825f), 175);
}

TEST(layout_shrink_to_fit) {
    layout::TmpSize fixed{20, 18, 72, false};
    CHECK_NEAR(layout::fit_floor(fixed), 20);  // never below the original size
    layout::TmpSize f = layout::fit_tmp(fixed, 1.5);
    CHECK(f.auto_size);
    CHECK_NEAR(f.font_size, 30);
    CHECK_NEAR(f.max, 30);
    CHECK_NEAR(f.min, 20);
    layout::TmpSize autos{36, 18, 42, true};
    CHECK_NEAR(layout::fit_floor(autos), 18);  // the game's own lower bound
    CHECK_NEAR(layout::fit_tmp(autos, 2.0).max, 84);
    CHECK_NEAR(layout::fit_tmp(autos, 2.0).min, 18);

    int calls = 0;
    auto fits_below = [&](float limit) {
        return [&calls, limit](float s) { ++calls; return s <= limit; };
    };
    CHECK_NEAR(layout::largest_fitting(36, 18, fits_below(40)), 36);  // fits: unchanged
    CHECK(calls == 1);
    float r = layout::largest_fitting(36, 18, fits_below(20.3f));
    CHECK(r <= 20.3f && r > 20.3f - 0.5f);
    CHECK_NEAR(layout::largest_fitting(36, 18, fits_below(10)), 18);  // floor
    CHECK_NEAR(layout::largest_fitting(18, 18, fits_below(10)), 18);

    // Top pivot at 82.5 % of the screen: grows down to the bottom margin.
    CHECK_NEAR(layout::max_panel_height(0.825f, 0, 1), 891 - 14);
    // Bottom pivot: grows up to the top margin.
    CHECK_NEAR(layout::max_panel_height(0, 50, 0), 1080 - 14 - 50);
    // Centre pivot: limited by the nearer edge.
    CHECK_NEAR(layout::max_panel_height(0.5f, 300, 0.5f), 2 * (1080 - 14 - 840));
}

// --- framework ----------------------------------------------------------------

TEST(dialogue_box_applies_once_and_never_compounds) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(1.5, 0.8), log);
    Node *box = make_box(ui, Box::Dialogue);
    rt.on_instance(ui, Target::DialogueBox, box);
    Node *c = ui.path(box, "Container");
    CHECK_NEAR(c->rect.sizeDelta.y, 390);
    CHECK_NEAR(c->rect.anchorMin.x, 0.025);
    CHECK_NEAR(c->rect.anchorMax.x, 0.975);
    CHECK_NEAR(c->comps[Comp::RawImage][Prop::GraphicAlpha], 0.8);
    Node *d = ui.path(box, "Container/Dialogue");
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmSize], 39);
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmRememberReadPosition], 1);
    CHECK_NEAR(d->rect.sizeDelta.y, 160);
    CHECK_NEAR(ui.path(box, "Container/Nameplate/Text")->comps[Comp::Tmp][Prop::TmpFontSize], 63);
    CHECK_NEAR(ui.path(box, "Container/Buttons")->rect.anchoredPosition.y, 37.5);
    CHECK_NEAR(ui.path(box, "Container/Character Info/Age")->comps[Comp::Tmp][Prop::TmpFontSize], 30);
    // Long names and labels shrink to fit, down to the game's own sizes.
    CHECK_NEAR(ui.path(box, "Container/Nameplate/Text")->comps[Comp::Tmp][Prop::TmpFontSizeMin], 18);
    CHECK_NEAR(ui.path(box, "Container/Character Info/Age")->comps[Comp::Tmp][Prop::TmpAutoSize], 1);
    CHECK_NEAR(ui.path(box, "Container/Character Info/Age")->comps[Comp::Tmp][Prop::TmpFontSizeMin], 20);
    // Long dialogue lines shrink to fit the text area (Super Text Mesh best fit).
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmBestFit], 6);
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmUiLimit], 1);
    CHECK(log.has("FEATURE dialogue_text_fit applied"));
    CHECK(log.has("FEATURE dialogue_box applied"));
    CHECK(log.has("FEATURE text_log_line_restore applied"));

    std::string once = snapshot(box);
    rt.on_instance(ui, Target::DialogueBox, box);  // re-activation, same instance
    CHECK(snapshot(box) == once);

    // A copy of the modified object is a new instance: same result, no compounding.
    Node *copy = make_box(ui, Box::Dialogue);
    // Pretend it was cloned after modification.
    ui.path(copy, "Container")->rect = c->rect;
    ui.path(copy, "Container/Dialogue")->comps = d->comps;
    rt.on_instance(ui, Target::DialogueBox, copy);
    CHECK_NEAR(ui.path(copy, "Container")->rect.sizeDelta.y, 390);
    CHECK(log.count("FEATURE dialogue_box applied") == 1);
}

TEST(narrator_and_testimony_variants) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(2.0), log);
    Node *nar = make_box(ui, Box::Narrator);
    rt.on_instance(ui, Target::DialogueBox, nar);
    CHECK(log.has("FEATURE narrator_box applied"));
    CHECK(!log.has("FEATURE dialogue_box applied"));
    CHECK_NEAR(ui.path(nar, "Container")->rect.anchorMin.x, 0);  // already full width
    CHECK_NEAR(ui.path(nar, "Container")->rect.sizeDelta.y, 580);

    Node *t = make_box(ui, Box::Testimony);
    Node *press = ui.path(t, "Container/Press");
    Node *left = ui.path(t, "Container/Left");
    rt.on_instance(ui, Target::DialogueBox, t);
    CHECK(log.has("FEATURE cross_examination applied"));
    CHECK(press->parent == t);
    CHECK_NEAR(press->rect.anchorMin.x, 0);
    CHECK_NEAR(press->rect.anchorMin.y, 1);
    CHECK(ui.path(t, "Present")->rect.anchorMin.x == 1.0f);
    CHECK_NEAR(left->rect.sizeDelta.y, 220);  // original height kept
    CHECK_NEAR(left->rect.sizeDelta.x, 180);
    CHECK_NEAR(left->rect.anchorMin.y, 1);
    CHECK_NEAR(left->scale.x, -1);  // still mirrored
    CHECK_NEAR(ui.path(t, "Container")->rect.anchorMin.x, 0.025);
}

TEST(mismatch_skips_only_that_feature_without_partial_changes) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *box = make_box(ui, Box::Dialogue);
    ui.path(box, "Container/Nameplate")->name = "Renamed";
    std::string before = snapshot(box);
    rt.on_instance(ui, Target::DialogueBox, box);
    CHECK(log.has("FEATURE dialogue_box skipped: missing 'Container/Nameplate'"));
    // The line-restore and text-fit units still work; undo them to compare the rest.
    Node *d = ui.path(box, "Container/Dialogue");
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmRememberReadPosition], 1);
    CHECK_NEAR(d->comps[Comp::Stm][Prop::StmBestFit], 6);
    for (Prop p : {Prop::StmRememberReadPosition, Prop::StmBestFit, Prop::StmUiLimit}) d->comps[Comp::Stm].erase(p);
    CHECK(snapshot(box) == before);
    // The skipped feature stays off for the session.
    Node *good = make_box(ui, Box::Dialogue);
    rt.on_instance(ui, Target::DialogueBox, good);
    CHECK_NEAR(ui.path(good, "Container")->rect.sizeDelta.y, 240);
    // Other features are unaffected.
    Node *log_ui = make_log(ui);
    rt.on_instance(ui, Target::TextLog, log_ui);
    CHECK(log.has("FEATURE text_log applied"));
}

TEST(engine_error_rolls_back_and_disables_only_that_feature) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *box = make_box(ui, Box::Dialogue);
    std::string before = snapshot(box);
    ui.fail_on = Prop::TmpFontSize;  // fails half-way through the dialogue box
    ui.fail_after = 0;
    rt.on_instance(ui, Target::DialogueBox, box);
    CHECK(log.has("FEATURE dialogue_box failed: injected failure"));
    ui.fail_on = Prop::Count;
    Node *d = ui.path(box, "Container/Dialogue");
    for (Prop p : {Prop::StmRememberReadPosition, Prop::StmBestFit, Prop::StmUiLimit}) d->comps[Comp::Stm].erase(p);
    CHECK(snapshot(box) == before);
    Node *img = make_image(ui);
    rt.on_instance(ui, Target::ImageDisplay, img);
    CHECK(log.has("FEATURE story_images applied"));
    // dialogue_box failed, so the panels keep their size and the image too.
    CHECK_NEAR(ui.path(img, "Image")->rect.sizeDelta.y, -350);
}

TEST(master_switch_off_changes_nothing) {
    FakeUi ui;
    MemLog log;
    Settings s = settings();
    s.enabled = false;
    Runtime rt(s, log);
    rt.report_disabled();
    std::vector<Node *> roots = {make_box(ui, Box::Dialogue), make_log(ui), make_menu(ui), make_popup(ui), make_image(ui)};
    std::vector<std::string> before;
    for (Node *r : roots) before.push_back(snapshot(r));
    rt.on_instance(ui, Target::DialogueBox, roots[0]);
    rt.on_instance(ui, Target::TextLog, roots[1]);
    rt.on_instance(ui, Target::ItemMenu, roots[2]);
    rt.on_instance(ui, Target::Notification, roots[3]);
    rt.on_instance(ui, Target::ImageDisplay, roots[4]);
    rt.on_frame(ui);
    for (size_t i = 0; i < roots.size(); ++i) CHECK(snapshot(roots[i]) == before[i]);
    CHECK(!rt.global_enabled("pause_when_unfocused"));
    CHECK(log.has("FEATURE dialogue_box disabled"));
}

TEST(feature_switch_off_changes_only_that_feature) {
    FakeUi ui;
    MemLog log;
    Settings s = settings();
    s.features["text_log"] = false;
    Runtime rt(s, log);
    Node *lg = make_log(ui);
    std::string before = snapshot(lg);
    rt.on_instance(ui, Target::TextLog, lg);
    CHECK(snapshot(lg) == before);
    Node *box = make_box(ui, Box::Dialogue);
    rt.on_instance(ui, Target::DialogueBox, box);
    CHECK_NEAR(ui.path(box, "Container/Dialogue")->comps[Comp::Stm][Prop::StmRememberReadPosition], 0);
    CHECK_NEAR(ui.path(box, "Container")->rect.sizeDelta.y, 390);
}

TEST(dialogue_text_fit_variants) {
    FakeUi ui;
    MemLog log;
    Settings s = settings();
    s.features["cross_examination"] = false;
    Runtime rt(s, log);
    Node *nar = make_box(ui, Box::Narrator);
    rt.on_instance(ui, Target::DialogueBox, nar);
    CHECK_NEAR(ui.path(nar, "Container/Dialogue")->comps[Comp::Stm][Prop::StmBestFit], 6);
    // Cross-examination off: its text is not touched either.
    Node *t = make_box(ui, Box::Testimony);
    rt.on_instance(ui, Target::DialogueBox, t);
    CHECK_NEAR(ui.path(t, "Container/Dialogue")->comps[Comp::Stm][Prop::StmBestFit], 0);
    CHECK(log.has("FEATURE cross_examination_text_fit disabled") || !log.has("cross_examination_text_fit applied"));

    // A game that already fits or pages its text: only the fitting is skipped.
    FakeUi ui2;
    MemLog log2;
    Runtime rt2(settings(), log2);
    Node *box = make_box(ui2, Box::Dialogue);
    ui2.path(box, "Container/Dialogue")->comps[Comp::Stm][Prop::StmVerticalLimitMode] = 3;
    rt2.on_instance(ui2, Target::DialogueBox, box);
    CHECK(log2.has("FEATURE dialogue_text_fit skipped: dialogue text uses a vertical limit mode"));
    CHECK(log2.has("FEATURE dialogue_box applied"));
    CHECK_NEAR(ui2.path(box, "Container/Dialogue")->comps[Comp::Stm][Prop::StmBestFit], 0);
    CHECK_NEAR(ui2.path(box, "Container/Dialogue")->comps[Comp::Stm][Prop::StmUiLimit], 0);
    CHECK_NEAR(ui2.path(box, "Container")->rect.sizeDelta.y, 390);
}

TEST(story_image_clears_the_taller_panel) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(2.0), log);
    rt.on_instance(ui, Target::DialogueBox, make_box(ui, Box::Dialogue));
    Node *img = make_image(ui);
    rt.on_instance(ui, Target::ImageDisplay, img);
    RectData r = ui.path(img, "Image")->rect;
    CHECK_NEAR(layout::image_bottom(r), 50 + 580 + 10);
    CHECK_NEAR(-r.anchoredPosition.y - r.sizeDelta.y / 2, 50);
}

TEST(text_log_entries) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(1.5), log);
    Node *lg = make_log(ui);
    rt.on_instance(ui, Target::TextLog, lg);
    Node *entry = ui.path(lg, "Scroll View/Viewport/Content/Text Entry");
    CHECK_NEAR(ui.path(entry, "Char Name")->comps[Comp::Tmp][Prop::TmpFontSize], 54);
    CHECK_NEAR(ui.path(entry, "Char Name")->comps[Comp::Tmp][Prop::TmpCharacterSpacing], 0);
    CHECK_NEAR(ui.path(entry, "Text 2")->comps[Comp::Stm][Prop::StmSize], 37.5);
    CHECK_NEAR(ui.path(entry, "Text 2")->comps[Comp::Stm][Prop::StmCharacterSpacing], 0);
    CHECK_NEAR(ui.path(entry, "Text")->rect.sizeDelta.y, 60);
    CHECK_NEAR(entry->comps[Comp::VLayout][Prop::LgSpacing], 22.5);
    CHECK_NEAR(ui.path(lg, "Scroll View/Viewport/Content")->comps[Comp::VLayout][Prop::LgSpacing], 37.5);
    CHECK_NEAR(ui.path(lg, "Scroll View")->rect.anchorMin.x, 0.075);
}

TEST(text_log_requires_preferred_height_fitting) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *lg = make_log(ui);
    ui.path(lg, "Scroll View/Viewport/Content/Text Entry/Text")->comps[Comp::SizeFitter][Prop::FitVertical] = 0;
    std::string before = snapshot(lg);
    rt.on_instance(ui, Target::TextLog, lg);
    CHECK(log.has("FEATURE text_log skipped"));
    CHECK(snapshot(lg) == before);
}

TEST(evidence_menu_layout_and_overflow) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(1.5), log);
    Node *menu = make_menu(ui);
    // FR-UI-27a: the container draws the panel background; the picture sits
    // 20 units inside its right edge, so that is the picture's padding.
    Node *panel = ui.path(menu, "Container/Right/Container");
    panel->comps[Comp::Graphic];
    ui.bounds[panel] = {0, 0, 1000, 500};
    ui.bounds[ui.path(panel, "Top/Icon")] = {810, 310, 980, 480};
    rt.on_instance(ui, Target::ItemMenu, menu);
    CHECK(log.has("FEATURE evidence_menu applied"));
    Node *base = ui.path(menu, "Container/Right/Container");
    Node *info = ui.path(base, "Top/Item Info");
    Node *head = ui.path(info, "Description");
    CHECK(head->parent == info);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 36);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpAutoSize], 0);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpOverflowMode], kOverflowLinked);
    // The continuation and the measuring probe sit in the text column, after
    // the description; nothing else is added below the picture row.
    CHECK(ui.find(base, "LTM Evidence Lower") == nullptr);
    Node *cont = ui.path(info, "LTM Description Continuation");
    Node *probe = ui.path(info, "LTM Line Probe");
    CHECK(cont != nullptr && probe != nullptr);
    CHECK(cont == info->children.back());
    CHECK(head->linked == cont);
    CHECK_NEAR(cont->comps[Comp::Tmp][Prop::TmpFontSize], 36);
    CHECK_NEAR(cont->comps[Comp::Tmp][Prop::TmpOverflowMode], kOverflowOverflow);
    CHECK_NEAR(probe->comps[Comp::Tmp][Prop::GraphicAlpha], 0);
    CHECK_NEAR(probe->comps[Comp::Tmp][Prop::TmpOverflowMode], kOverflowOverflow);
    Node *fields = ui.path(info, "Fields");
    Node *view = ui.path(info, "View Button");
    CHECK(fields != nullptr && view != nullptr);
    Node *top = ui.path(base, "Top");
    CHECK(top->children.back()->name == "Icon");  // picture on the right

    // No overflow: the continuation is hidden.
    rt.on_frame(ui);
    CHECK(!cont->active);
    // Overflow: shown and sized to the rendered text.
    ui.overflowing.insert(head);
    head->comps[Comp::LayoutElement];  // changes the text identity in the fake
    rt.on_frame(ui);
    CHECK(cont->active);
    CHECK_NEAR(cont->rect.sizeDelta.y, 40);
    // Rows take their preferred height (30 * 1.2).
    CHECK_NEAR(ui.path(fields, "Item Field")->rect.sizeDelta.y, 36);
    CHECK_NEAR(fields->rect.sizeDelta.y, 72);
    // Stacked below the name (bottom at -50) with gaps of 10: details at -60,
    // View (keeping its width and height) at -142, description at -202.
    CHECK_NEAR(fields->rect.anchorMin.y, 1);
    CHECK_NEAR(fields->rect.anchoredPosition.y, -60);
    CHECK_NEAR(view->rect.anchorMin.y, 1);
    CHECK_NEAR(view->rect.anchoredPosition.y, -142);
    CHECK_NEAR(view->rect.sizeDelta.x, 200);
    CHECK_NEAR(view->rect.sizeDelta.y, 50);
    CHECK_NEAR(head->rect.anchoredPosition.y, -202);
    // Item info fills the space left of the picture and its padding, which is
    // also the row's spacing (was 10).
    CHECK_NEAR(info->rect.sizeDelta.x, 1000 - 170 - 20);
    CHECK_NEAR(top->comps[Comp::HLayout][Prop::LgSpacing], 20);
    // FR-UI-22: with detail rows, no description line runs beside the
    // picture. Here the details already reach below the picture and its
    // padding (190), so the continuation starts right below them, at the full
    // row width (column + picture + padding).
    CHECK_NEAR(head->rect.sizeDelta.y, 0);
    CHECK_NEAR(cont->rect.anchorMin.y, 1);
    CHECK_NEAR(cont->rect.pivot.y, 1);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -202);
    CHECK_NEAR(cont->rect.sizeDelta.x, 170 + 20);
    CHECK_NEAR(cont->rect.anchoredPosition.x, 0);
    CHECK_NEAR(cont->rect.pivot.x, 0);

    // One detail row: the View button ends at -156, beside the picture, but
    // the description still starts below the picture's padding, at -190.
    for (const char *label : {"Item Field Names", "Item Field Values"})
        ui.path(fields, std::string("Item Field (1)/") + label)->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    head->text_version++;  // new text
    rt.on_frame(ui);
    CHECK_NEAR(fields->rect.sizeDelta.y, 36);
    CHECK_NEAR(view->rect.anchoredPosition.y, -106);
    CHECK_NEAR(head->rect.anchoredPosition.y, -190);
    CHECK_NEAR(head->rect.sizeDelta.y, 0);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -190);
    CHECK_NEAR(cont->rect.sizeDelta.x, 170 + 20);

    // No detail rows: they take no height, the View button moves up, and the
    // description starts below it, beside the picture.
    for (const char *label : {"Item Field Names", "Item Field Values"})
        ui.path(fields, std::string("Item Field/") + label)->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    head->text_version++;  // new text
    rt.on_frame(ui);
    CHECK_NEAR(fields->rect.sizeDelta.y, 0);
    CHECK_NEAR(view->rect.anchoredPosition.y, -60);
    CHECK_NEAR(head->rect.anchoredPosition.y, -120);
    // FR-UI-22/27a: 50 units are left beside the picture and 20 more beside
    // its bottom padding. Lines are 43.2 apart (fake: 1.2 * size), so two
    // lines start there; the head holds exactly those, and the continuation
    // starts on the next line of the same grid, below the padding.
    CHECK_NEAR(head->rect.sizeDelta.y, 43.2 + 43.2 + 0.5);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -120 - 2 * 43.2);

    // FR-UI-0d: a long description shrinks so the text ends above where the
    // original description ended: 170 (picture row) + 330 (below it) = 500
    // from the column's top. With two lines beside the picture (70 units at
    // lines up to 1.2 * 30.62 apart), that is
    // 120 + 2 * 1.2 * size + ceil(10 * size) <= 500.5, so about 30.62, below
    // the enlarged 36 and above the game's own minimum of 18.
    cont->text_lines = 10;
    head->comps[Comp::Button];  // new text
    rt.on_frame(ui);
    double fitted = head->comps[Comp::Tmp][Prop::TmpFontSize];
    CHECK(fitted <= 30.63 && fitted > 30.62 - 0.5);
    CHECK_NEAR(cont->comps[Comp::Tmp][Prop::TmpFontSize], fitted);  // same size in both parts
    CHECK_NEAR(probe->comps[Comp::Tmp][Prop::TmpFontSize], fitted);
    CHECK_NEAR(cont->rect.sizeDelta.y, std::ceil(10 * fitted));
    CHECK_NEAR(cont->rect.anchoredPosition.y, -120 - 2 * 1.2 * fitted);
    CHECK(120 + 2 * 1.2 * fitted + std::ceil(10 * fitted) <= 500.5);
    // Far too long: never below the game's own minimum.
    cont->text_lines = 40;
    head->comps[Comp::AspectFitter];
    rt.on_frame(ui);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 18);
    // Short again: back to the enlarged size.
    cont->text_lines = 2;
    head->comps.erase(Comp::AspectFitter);
    rt.on_frame(ui);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 36);

    // Re-opening the same menu doesn't apply again.
    std::string once = snapshot(menu);
    rt.on_instance(ui, Target::ItemMenu, menu);
    CHECK(snapshot(menu) == once);
    CHECK(ui.retained.count(menu) == 1);
}

TEST(evidence_notification_layout) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(2.0), log);
    Node *p = make_popup(ui);
    // FR-UI-27a: the panel's padding: the picture sits 25 units inside its
    // right edge.
    Node *panel = ui.path(p, "Window");
    panel->comps[Comp::Graphic];
    ui.bounds[panel] = {0, 0, 1600, 1000};
    ui.bounds[ui.path(panel, "Top/Icon")] = {1305, 705, 1575, 975};
    rt.on_instance(ui, Target::Notification, p);
    CHECK(log.has("FEATURE evidence_notification applied"));
    Node *w = ui.path(p, "Window");
    CHECK_NEAR(w->rect.anchorMin.x, 0.075);
    CHECK_NEAR(w->comps[Comp::SizeFitter][Prop::FitVertical], 2);
    Node *h = ui.path(w, "Header");
    CHECK_NEAR(h->comps[Comp::Tmp][Prop::TmpAutoSize], 1);
    CHECK_NEAR(h->comps[Comp::Tmp][Prop::TmpFontSizeMin], 71.21);
    CHECK_NEAR(h->rect.sizeDelta.y, 175);
    Node *icon = ui.path(w, "Top/Icon");
    CHECK_NEAR(icon->rect.anchorMin.x, 1);
    CHECK_NEAR(icon->rect.sizeDelta.x, 270);
    // Order: Header, Top. FR-UI-33/34: the details stay beside the picture,
    // and the description, its continuation and the probe are in the text
    // column.
    CHECK(w->children.size() == 2);
    Node *head = ui.path(w, "Top/Item Info/Description");
    CHECK(head != nullptr);
    Node *cont = ui.path(w, "Top/Item Info/LTM Description Continuation");
    CHECK(cont != nullptr && cont == cont->parent->children.back());
    CHECK(head->linked == cont);
    CHECK(ui.path(w, "Top/Item Info/LTM Line Probe") != nullptr);
    CHECK_NEAR(head->comps[Comp::SizeFitter][Prop::Enabled], 0);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 48);
    Node *fields = ui.path(w, "Top/Item Info/Item Fields");
    CHECK(fields != nullptr);
    CHECK_NEAR(ui.path(fields, "Item Field/Item Field Values")->comps[Comp::Tmp][Prop::TmpFontSize], 60);
    // Below the enlarged name (bottom at -100): three rows of 72 at -110, the
    // description at -336. The picture row grows from 270 to fit them.
    Node *top = ui.path(w, "Top");
    Node *info = ui.path(w, "Top/Item Info");
    rt.on_frame(ui);
    // FR-UI-27a: the text column ends the padding (25, was 30) left of the
    // picture.
    CHECK_NEAR(info->rect.sizeDelta.x, -(270 + 25));
    CHECK_NEAR(info->rect.anchoredPosition.x, -(270 + 25) / 2.0);
    CHECK_NEAR(fields->rect.anchorMin.y, 1);
    CHECK_NEAR(fields->rect.anchoredPosition.y, -110);
    CHECK_NEAR(fields->rect.sizeDelta.y, 216);
    CHECK_NEAR(head->rect.anchoredPosition.y, -336);
    CHECK_NEAR(top->rect.sizeDelta.y, 336);
    CHECK_NEAR(head->rect.sizeDelta.y, 0);  // no line beside the picture
    CHECK_NEAR(cont->rect.anchoredPosition.y, -336);
    CHECK_NEAR(cont->rect.sizeDelta.x, 270 + 25);  // across the picture and its padding
    // Empty rows take no height; hidden rows are skipped. With one detail
    // row left (ending at -182, beside the picture), the description starts
    // below the picture's padding, at -295. The picture row goes back to the
    // picture's height while there is no text below it.
    ui.path(fields, "Item Field (2)")->active = false;
    ui.path(fields, "Item Field (1)/Item Field Names")->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    ui.path(fields, "Item Field (1)/Item Field Values")->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    rt.on_frame(ui);
    CHECK_NEAR(ui.path(fields, "Item Field (1)")->rect.sizeDelta.y, 0);
    CHECK_NEAR(fields->rect.sizeDelta.y, 72);
    CHECK_NEAR(head->rect.anchoredPosition.y, -295);
    CHECK_NEAR(head->rect.sizeDelta.y, 0);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -295);
    CHECK_NEAR(top->rect.sizeDelta.y, 270);

    // No detail rows: the description starts below the name and wraps
    // around the picture. 160 units beside the picture plus 25 of padding,
    // lines 57.6 apart: four lines beside it.
    Node *row0 = ui.path(fields, "Item Field");
    ui.path(row0, "Item Field Names")->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    ui.path(row0, "Item Field Values")->comps[Comp::Tmp][Prop::TmpFontSize] = 0;
    head->text_version++;  // new text
    rt.on_frame(ui);
    CHECK_NEAR(fields->rect.sizeDelta.y, 0);
    CHECK_NEAR(head->rect.anchoredPosition.y, -110);
    CHECK_NEAR(head->rect.sizeDelta.y, 4 * 57.6 + 0.5);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -110 - 4 * 57.6);
    CHECK_NEAR(top->rect.sizeDelta.y, 270);

    // FR-UI-35/0d: with a detail row again, the panel grows down from 82.5 %
    // of the screen and has 891 - 14 = 877 units, all of it the picture row,
    // which holds the text below the picture and its padding:
    // 295 + ceil(13 * size) <= 877.5 up to a size of about 44.77.
    ui.path(row0, "Item Field Names")->comps[Comp::Tmp][Prop::TmpFontSize] = 60;
    ui.path(row0, "Item Field Values")->comps[Comp::Tmp][Prop::TmpFontSize] = 60;
    ui.overflowing.insert(head);
    cont->text_lines = 13;
    head->comps[Comp::Button];  // new text
    rt.on_frame(ui);
    CHECK(cont->active);
    CHECK_NEAR(fields->rect.sizeDelta.y, 72);
    double fitted = head->comps[Comp::Tmp][Prop::TmpFontSize];
    CHECK(fitted <= 44.77 && fitted > 44.77 - 0.5);
    CHECK_NEAR(cont->comps[Comp::Tmp][Prop::TmpFontSize], fitted);
    CHECK_NEAR(cont->rect.anchoredPosition.y, -295);
    CHECK_NEAR(top->rect.sizeDelta.y, 295 + std::ceil(13 * fitted));
    CHECK(top->rect.sizeDelta.y <= 877.5);
    // Short text keeps the enlarged size; far too long stops at the original 24.
    cont->text_lines = 2;
    head->comps[Comp::AspectFitter];
    rt.on_frame(ui);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 48);
    cont->text_lines = 100;
    head->comps.erase(Comp::AspectFitter);
    rt.on_frame(ui);
    CHECK_NEAR(head->comps[Comp::Tmp][Prop::TmpFontSize], 24);
}

TEST(evidence_without_details_wraps_around_the_picture) {
    // FR-UI-22/26: rows that are hidden, or whose texts are empty, blank or
    // only tags, are no detail rows: the description starts below the name
    // and View button and wraps around the picture (fallback gap 10).
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(1.5), log);
    Node *menu = make_menu(ui);
    rt.on_instance(ui, Target::ItemMenu, menu);
    Node *info = ui.path(menu, "Container/Right/Container/Top/Item Info");
    Node *fields = ui.path(info, "Fields");
    Node *head = ui.path(info, "Description");
    Node *cont = ui.path(info, "LTM Description Continuation");
    auto check_wraps = [&] {
        head->text_version++;  // new entry
        rt.on_frame(ui);
        CHECK_NEAR(fields->rect.sizeDelta.y, 0);
        CHECK_NEAR(ui.path(fields, "Item Field")->rect.sizeDelta.y, 0);
        CHECK_NEAR(head->rect.anchoredPosition.y, -120);
        // 50 units beside the picture, 10 of padding, lines 43.2 apart.
        CHECK_NEAR(head->rect.sizeDelta.y, 2 * 43.2 + 0.5);
        CHECK_NEAR(cont->rect.anchoredPosition.y, -120 - 2 * 43.2);
    };
    auto set_texts = [&](const std::string &t) {
        for (const char *row : {"Item Field", "Item Field (1)"})
            for (const char *label : {"Item Field Names", "Item Field Values"})
                ui.path(fields, std::string(row) + "/" + label)->text = t;
    };
    // With details first, so the rows have stale heights.
    rt.on_frame(ui);
    CHECK_NEAR(fields->rect.sizeDelta.y, 72);
    // The whole details area hidden, rows keeping their old texts.
    fields->active = false;
    check_wraps();
    fields->active = true;
    for (const char *t : {"", "  \n", "<color=#FFF></color>", "\xC2\xA0"}) {
        set_texts(t);
        check_wraps();
    }
    // Details again: below the picture.
    set_texts("Found");
    head->text_version++;
    rt.on_frame(ui);
    CHECK_NEAR(fields->rect.sizeDelta.y, 72);
    CHECK_NEAR(head->rect.sizeDelta.y, 0);
}

TEST(picture_padding_falls_back_to_the_game_gap) {
    // FR-UI-27a: no container with a background (or an implausible gap):
    // the game's own gap between the text column and the picture is kept.
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(1.5), log);
    Node *menu = make_menu(ui);
    rt.on_instance(ui, Target::ItemMenu, menu);
    rt.on_frame(ui);
    Node *top = ui.path(menu, "Container/Right/Container/Top");
    CHECK_NEAR(top->comps[Comp::HLayout][Prop::LgSpacing], 10);
    CHECK_NEAR(ui.path(top, "Item Info")->rect.sizeDelta.x, 1000 - 170 - 10);

    Node *p = make_popup(ui);
    Node *w = ui.path(p, "Window");
    w->comps[Comp::Graphic];
    ui.bounds[w] = {0, 0, 1600, 1000};
    ui.bounds[ui.path(w, "Top/Icon")] = {1000, 705, 1270, 975};  // 330: wider than half the picture
    rt.on_instance(ui, Target::Notification, p);
    rt.on_frame(ui);
    CHECK_NEAR(ui.path(w, "Top/Item Info")->rect.sizeDelta.x, -(270 + 30));
    CHECK_NEAR(ui.path(w, "Top/Item Info/LTM Description Continuation")->rect.sizeDelta.x, 270 + 30);
}

TEST(tick_errors_disable_only_the_feature) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *menu = make_menu(ui);
    rt.on_instance(ui, Target::ItemMenu, menu);
    ui.path(menu, "Container/Right/Container/Top/Item Info/LTM Description Continuation")->name = "Gone";
    rt.on_frame(ui);
    CHECK(log.has("FEATURE evidence_menu skipped: evidence menu structure changed at runtime"));
    CHECK(ui.retained.empty());
    Node *p = make_popup(ui);
    rt.on_instance(ui, Target::Notification, p);
    rt.on_frame(ui);
    CHECK(log.has("FEATURE evidence_notification applied"));
}

TEST(destroyed_instances_are_released) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *menu = make_menu(ui);
    rt.on_instance(ui, Target::ItemMenu, menu);
    CHECK(rt.wants_frames());
    menu->destroyed = true;
    rt.on_frame(ui);
    CHECK(!rt.wants_frames());
    CHECK(ui.retained.empty());
}

TEST(line_restored_after_text_log_closes) {
    FakeUi ui;
    MemLog log;
    Runtime rt(settings(), log);
    Node *box = make_box(ui, Box::Narrator);
    Node *text = ui.path(box, "Container/Dialogue");
    auto &stm = text->comps[Comp::Stm];
    stm[Prop::StmReading] = 0;
    stm[Prop::StmReadTime] = 1.98;
    stm[Prop::StmTotalReadTime] = 1.98;  // the line is fully revealed
    rt.on_instance(ui, Target::DialogueBox, box);
    Node *lg = make_log(ui);
    rt.on_frame(ui);  // records the completed line
    rt.on_instance(ui, Target::TextLog, lg);
    rt.on_frame(ui);  // log open
    CHECK(ui.shown_until.empty());
    lg->active = false;  // log closed
    rt.on_frame(ui);
    CHECK(ui.shown_until.count(text) == 1);
    CHECK_NEAR(ui.shown_until[text], 1.98);

    // A new line (different text) or a line still being revealed is left alone.
    ui.shown_until.clear();
    ++text->text_version;
    stm[Prop::StmReadTime] = 0.5;
    stm[Prop::StmReading] = 1;
    lg->active = true;
    rt.on_frame(ui);
    lg->active = false;
    rt.on_frame(ui);
    CHECK(ui.shown_until.empty());
    // Several frames later nothing more happens.
    for (int i = 0; i < 10; ++i) rt.on_frame(ui);
    CHECK(ui.shown_until.empty());
}

TEST(global_feature_reporting) {
    MemLog log;
    Runtime rt(settings(), log);
    CHECK(rt.global_enabled("pause_when_unfocused"));
    rt.global_applied("pause_when_unfocused");
    rt.global_applied("pause_when_unfocused");
    CHECK(log.count("FEATURE pause_when_unfocused applied") == 1);
    rt.global_failed("pause_when_unfocused", "icall missing");
    CHECK(!rt.global_enabled("pause_when_unfocused"));
    CHECK(log.has("FEATURE pause_when_unfocused failed: icall missing"));
}

int main() {
    for (auto &t : tests()) {
        int before = g_failures;
        t.second();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.first);
    }
    std::printf("\n%zu tests, %d checks, %d failures\n", tests().size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
