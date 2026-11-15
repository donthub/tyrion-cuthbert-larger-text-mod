#include "features.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace ltm {

using layout::TmpSize;

// ---------------------------------------------------------------------------
// Helpers

namespace {

Handle need(Ui &ui, Handle root, const std::string &path) {
    Handle go = ui.find(root, path);
    if (!go) throw Mismatch("missing '" + path + "'");
    return go;
}

Handle need(Ui &ui, Handle root, const std::string &path, std::initializer_list<Comp> comps) {
    Handle go = need(ui, root, path);
    for (Comp c : comps) {
        if (!ui.has(go, c)) throw Mismatch("'" + path + "' has no " + comp_name(c));
    }
    return go;
}

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

void expect(bool condition, const std::string &what) {
    if (!condition) throw Mismatch(what);
}

float sane_height(double h) {
    // TMP reports huge or negative values when nothing is rendered.
    if (!(h > 0.0) || h > 100000.0) return 0.0f;
    return static_cast<float>(h);
}

}  // namespace

TmpSize read_tmp(Ui &ui, Handle go) {
    TmpSize t;
    t.font_size = static_cast<float>(ui.get(go, Comp::Tmp, Prop::TmpFontSize));
    t.auto_size = ui.get(go, Comp::Tmp, Prop::TmpAutoSize) != 0.0;
    t.min = static_cast<float>(ui.get(go, Comp::Tmp, Prop::TmpFontSizeMin));
    t.max = static_cast<float>(ui.get(go, Comp::Tmp, Prop::TmpFontSizeMax));
    return t;
}

void write_tmp(Tx &tx, Handle go, const TmpSize &size) {
    tx.set(go, Comp::Tmp, Prop::TmpAutoSize, size.auto_size ? 1 : 0);
    // Max before min so the range stays valid while it changes.
    tx.set(go, Comp::Tmp, Prop::TmpFontSizeMax, size.max);
    tx.set(go, Comp::Tmp, Prop::TmpFontSizeMin, size.min);
    tx.set(go, Comp::Tmp, Prop::TmpFontSize, size.font_size);
}

// ---------------------------------------------------------------------------
// Dialogue, narrator and cross-examination boxes (FR-UI-1 ... FR-UI-11)

namespace {

const char *const kInfoLabels[] = {"Occupation", "Age", "Arcane Art"};

enum class BoxKind { Dialogue, Narrator, Testimony };

// Which kind of box an instance is. A box without a container counts as a
// dialogue box, so the dialogue unit's validation reports it.
BoxKind box_kind(Ui &ui, Handle root) {
    if (ui.has(root, Comp::TestimonyPrompt)) return BoxKind::Testimony;
    Handle container = ui.find(root, "Container");
    if (!container) return BoxKind::Dialogue;
    RectData r = ui.rect(container);
    bool full_width = r.anchorMin.x == 0.0f && r.anchorMax.x == 1.0f;
    return full_width ? BoxKind::Narrator : BoxKind::Dialogue;
}

struct BoxOriginals {
    RectData container, dialogue, nameplate, info, buttons;
    RectData labels[3];
    TmpSize nameplate_text, label_text[3];
    float stm_size = 0;
    // Cross-examination only.
    RectData left, right, press, present;
};

class DialogueBoxUnit : public Unit {
  public:
    DialogueBoxUnit(const char *key, const char *config_key, BoxKind kind)
        : Unit(key, config_key, Target::DialogueBox), kind_(kind) {}

    bool matches(Ui &ui, Handle root) override { return box_kind(ui, root) == kind_; }

    void validate(Ui &ui, Handle root) override {
        Handle container = need(ui, root, "Container", {Comp::RawImage});
        Handle dialogue = need(ui, root, "Container/Dialogue", {Comp::Stm});
        Handle nameplate = need(ui, root, "Container/Nameplate");
        Handle name_text = need(ui, root, "Container/Nameplate/Text", {Comp::Tmp});
        Handle info = need(ui, root, "Container/Character Info");
        Handle buttons = need(ui, root, "Container/Buttons");
        Handle labels[3];
        for (int i = 0; i < 3; ++i)
            labels[i] = need(ui, root, std::string("Container/Character Info/") + kInfoLabels[i], {Comp::Tmp});

        RectData c = ui.rect(container);
        expect(c.anchorMin.y == 0.0f && c.anchorMax.y == 0.0f && c.pivot.y == 0.0f,
               "dialogue panel is not anchored to the bottom");
        RectData d = ui.rect(dialogue);
        expect(d.anchorMin.y == 0.5f && d.anchorMax.y == 0.5f, "dialogue text is not centred vertically");
        expect(c.sizeDelta.y > 1.0f && d.sizeDelta.y > 1.0f, "unexpected panel size");
        if (kind_ != BoxKind::Narrator) {
            expect(c.anchorMin.x == c.anchorMax.x && c.sizeDelta.x > 100.0f, "unexpected panel width");
        }
        Handle press = nullptr, present = nullptr, left = nullptr, right = nullptr;
        if (kind_ == BoxKind::Testimony) {
            if (ui.find(root, "Press") && !ui.find(root, "Container/Press")) throw AlreadyApplied();
            press = need(ui, root, "Container/Press", {Comp::Button, Comp::AspectFitter});
            present = need(ui, root, "Container/Present", {Comp::Button, Comp::AspectFitter});
            left = need(ui, root, "Container/Left", {Comp::Button});
            right = need(ui, root, "Container/Right", {Comp::Button});
        }
        if (orig_) return;  // originals come from the first (unmodified) instance
        BoxOriginals o;
        o.container = c;
        o.dialogue = d;
        o.nameplate = ui.rect(nameplate);
        o.info = ui.rect(info);
        o.buttons = ui.rect(buttons);
        for (int i = 0; i < 3; ++i) {
            o.labels[i] = ui.rect(labels[i]);
            o.label_text[i] = read_tmp(ui, labels[i]);
        }
        o.nameplate_text = read_tmp(ui, name_text);
        o.stm_size = static_cast<float>(ui.get(dialogue, Comp::Stm, Prop::StmSize));
        expect(o.stm_size > 1.0f, "unexpected dialogue font size");
        if (kind_ == BoxKind::Testimony) {
            o.press = ui.rect(press);
            o.present = ui.rect(present);
            o.left = ui.rect(left);
            o.right = ui.rect(right);
        }
        orig_ = o;
    }

    layout::DialoguePanel panel() const {
        layout::DialoguePanel p;
        p.panel_height = orig_->container.sizeDelta.y;
        p.panel_bottom = orig_->container.anchoredPosition.y;
        p.text_height = orig_->dialogue.sizeDelta.y;
        p.text_offset_y = orig_->dialogue.anchoredPosition.y;
        p.info_top_offset = orig_->info.anchoredPosition.y;
        p.info_height = orig_->info.sizeDelta.y;
        return p;
    }

    void apply(Tx &tx, Handle root, const Context &ctx) override {
        Ui &ui = tx.ui();
        const BoxOriginals &o = *orig_;
        const double s = ctx.settings.scale;
        const layout::DialoguePanel p = panel();
        if (kind_ == BoxKind::Dialogue) ctx.shared.dialogue_panel = p;
        if (kind_ == BoxKind::Narrator) ctx.shared.narrator_panel = p;
        const float text_h = layout::dialogue_text_height(p, s);
        const float panel_h = layout::dialogue_panel_height(p, s);
        const float stretch = panel_h / p.panel_height;
        const bool widen = kind_ != BoxKind::Narrator;
        const float width = o.container.sizeDelta.x;

        Handle container = ui.find(root, "Container");
        // FR-UI-8: only the background image's alpha changes.
        tx.set(container, Comp::RawImage, Prop::GraphicAlpha, ctx.settings.opacity);

        RectData c = o.container;
        c.sizeDelta.y = panel_h;
        if (widen) {  // FR-UI-3
            c.anchorMin.x = layout::kDialogueSideMargin;
            c.anchorMax.x = 1.0f - layout::kDialogueSideMargin;
            c.anchoredPosition.x = 0;
            c.sizeDelta.x = 0;
        }
        tx.set_rect(container, c);

        // FR-UI-4: children keep their place on the artwork, by rect, not scale.
        auto horizontal = [&](const RectData &r) { return widen ? layout::stretch_horizontally(r, width) : r; };

        Handle dialogue = ui.find(root, "Container/Dialogue");
        RectData d = horizontal(o.dialogue);
        d.sizeDelta.y = text_h;  // FR-UI-2
        tx.set_rect(dialogue, d);
        tx.set(dialogue, Comp::Stm, Prop::StmSize, layout::scaled_font(o.stm_size, s));  // FR-UI-1

        // FR-UI-5: the nameplate background is part of the panel artwork.
        RectData n = horizontal(o.nameplate);
        n.sizeDelta.y = o.nameplate.sizeDelta.y * stretch;
        tx.set_rect(ui.find(root, "Container/Nameplate"), n);
        // FR-UI-0d: long names and labels shrink to fit their rects.
        write_tmp(tx, ui.find(root, "Container/Nameplate/Text"), layout::fit_tmp(o.nameplate_text, s));

        // FR-UI-6
        RectData info = horizontal(o.info);
        info.anchoredPosition.y = o.info.anchoredPosition.y * stretch;
        info.sizeDelta.y = o.info.sizeDelta.y * static_cast<float>(s);
        tx.set_rect(ui.find(root, "Container/Character Info"), info);
        for (int i = 0; i < 3; ++i) {
            Handle label = ui.find(root, std::string("Container/Character Info/") + kInfoLabels[i]);
            write_tmp(tx, label, layout::fit_tmp(o.label_text[i], s));
            RectData l = o.labels[i];
            l.sizeDelta.y = o.labels[i].sizeDelta.y * static_cast<float>(s);
            tx.set_rect(label, l);
        }

        // FR-UI-7: the toolbar moves as one unit.
        RectData b = horizontal(o.buttons);
        b.anchoredPosition.y = o.buttons.anchoredPosition.y + layout::kToolbarRaise;
        tx.set_rect(ui.find(root, "Container/Buttons"), b);

        if (kind_ == BoxKind::Testimony) apply_testimony(tx, root);
    }

  private:
    void apply_testimony(Tx &tx, Handle root) {
        Ui &ui = tx.ui();
        const BoxOriginals &o = *orig_;
        const float margin = 20.0f;
        // FR-UI-10: Press top-left, Present top-right of the screen. Moving them
        // to the full-screen root keeps their fitters, glyphs and listeners.
        Handle press = ui.find(root, "Container/Press");
        Handle present = ui.find(root, "Container/Present");
        tx.set_parent(press, root, -1);
        tx.set_parent(present, root, -1);
        RectData pr = o.press;
        pr.anchorMin = pr.anchorMax = {0, 1};
        pr.pivot = {0, 1};
        pr.anchoredPosition = {margin, -margin};
        tx.set_rect(press, pr);
        RectData pt = o.present;
        pt.anchorMin = pt.anchorMax = {1, 1};
        pt.pivot = {1, 1};
        pt.anchoredPosition = {-margin, -margin};
        tx.set_rect(present, pt);

        // FR-UI-11: arrows just above the panel, original size, left still mirrored
        // (its negative x scale is untouched; the centre pivot keeps it in place).
        const float panel_h = o.container.sizeDelta.y;
        auto arrow = [&](const RectData &r, bool left_side) {
            float w = r.sizeDelta.x + (r.anchorMax.x - r.anchorMin.x) * o.container.sizeDelta.x;
            float h = r.sizeDelta.y + (r.anchorMax.y - r.anchorMin.y) * panel_h;
            RectData a = r;
            a.anchorMin = a.anchorMax = {left_side ? 0.0f : 1.0f, 1.0f};
            a.pivot = {0.5f, 0.0f};
            a.sizeDelta = {w, h};
            a.anchoredPosition = {left_side ? w / 2.0f : -w / 2.0f, layout::kImageGap};
            return a;
        };
        tx.set_rect(ui.find(root, "Container/Left"), arrow(o.left, true));
        tx.set_rect(ui.find(root, "Container/Right"), arrow(o.right, false));
    }

    BoxKind kind_;
    std::optional<BoxOriginals> orig_;
};

// FR-UI-0d: dialogue text that doesn't fit the enlarged text area shrinks
// until it does. Super Text Mesh does this itself with its multiline best fit,
// against the rect height (uiLimit). It works on the whole line when the line
// is built, so the reveal, its timing and its events are unchanged. Text that
// fits stays at the enlarged size. A separate unit, so a Super Text Mesh change
// only loses the fitting, not the larger boxes.
class DialogueFitUnit : public Unit {
  public:
    // The best-fit mode that shrinks wrapped text to the rect's width and height.
    static constexpr const char *kMode = "MultilineFull";

    DialogueFitUnit(const char *key, const char *config_key, std::initializer_list<BoxKind> kinds)
        : Unit(key, config_key, Target::DialogueBox), kinds_(kinds) {}

    bool matches(Ui &ui, Handle root) override {
        BoxKind k = box_kind(ui, root);
        return std::find(kinds_.begin(), kinds_.end(), k) != kinds_.end();
    }

    void validate(Ui &ui, Handle root) override {
        Handle text = need(ui, root, "Container/Dialogue", {Comp::Stm});
        int mode = ui.enum_value(text, Comp::Stm, Prop::StmBestFit, kMode);
        int current = static_cast<int>(ui.get(text, Comp::Stm, Prop::StmBestFit));
        if (current == mode && ui.get(text, Comp::Stm, Prop::StmUiLimit) != 0.0) throw AlreadyApplied();
        expect(current == 0, "dialogue text already uses best fit mode " + std::to_string(current));
        // With a vertical limit mode the game pages or cuts long text itself.
        expect(ui.get(text, Comp::Stm, Prop::StmVerticalLimitMode) == 0.0, "dialogue text uses a vertical limit mode");
        mode_ = mode;
    }

    void apply(Tx &tx, Handle root, const Context &) override {
        Handle text = tx.ui().find(root, "Container/Dialogue");
        tx.set(text, Comp::Stm, Prop::StmUiLimit, 1);
        tx.set(text, Comp::Stm, Prop::StmBestFit, mode_);
    }

  private:
    std::vector<BoxKind> kinds_;
    int mode_ = 0;
};

// FR-UI-19: while the text log is open the dialogue text gets rebuilt (e.g.
// when the shared font atlas grows). Super Text Mesh then draws a line that
// was already fully revealed in its "not yet revealed" state, so it is blank
// when the log closes. Lines still being revealed carry on by themselves. This
// unit remembers which line was fully revealed and redraws exactly that line,
// as revealed, for a few frames after the log closes. It never starts, stops
// or re-times reading and fires no events.
class LineRestoreUnit : public Unit {
  public:
    LineRestoreUnit() : Unit("text_log_line_restore", "text_log", Target::DialogueBox) {}
    bool matches(Ui &, Handle) override { return true; }
    bool wants_tick() const override { return true; }
    void validate(Ui &ui, Handle root) override {
        Handle text = need(ui, root, "Container/Dialogue", {Comp::Stm});
        ui.get(text, Comp::Stm, Prop::StmReading);  // the fields this unit relies on
        ui.get(text, Comp::Stm, Prop::StmReadTime);
        ui.get(text, Comp::Stm, Prop::StmTotalReadTime);
    }
    void apply(Tx &tx, Handle root, const Context &) override {
        tx.set(tx.ui().find(root, "Container/Dialogue"), Comp::Stm, Prop::StmRememberReadPosition, 1);
    }
    void tick(Ui &ui, Handle root, const Context &ctx) override {
        Handle text = ui.find(root, "Container/Dialogue");
        if (!text) throw Mismatch("dialogue text missing at runtime");
        int id = ui.instance_id(root);
        const void *identity = ui.text_identity(text, Comp::Stm);
        bool reading = ui.get(text, Comp::Stm, Prop::StmReading) != 0.0;
        double now = ui.get(text, Comp::Stm, Prop::StmReadTime);
        double total = ui.get(text, Comp::Stm, Prop::StmTotalReadTime);
        if (identity && !reading && total > 0.0 && now >= total - 1e-3) {
            completed_[id] = identity;
        } else if (completed_.count(id) && completed_[id] != identity) {
            completed_.erase(id);
        }
        auto it = completed_.find(id);
        if (ctx.shared.text_log_closed_frames > 0 && it != completed_.end() && it->second == identity && !reading) {
            ui.show_text_until(text, now > total ? now : total);
        }
    }
  private:
    std::map<int, const void *> completed_;
};

// ---------------------------------------------------------------------------
// Story images (FR-UI-12 ... FR-UI-14)

class StoryImageUnit : public Unit {
  public:
    StoryImageUnit() : Unit("story_images", "story_images", Target::ImageDisplay) {}
    bool matches(Ui &, Handle) override { return true; }
    void validate(Ui &ui, Handle root) override {
        Handle image = need(ui, root, "Image", {Comp::AspectFitter});
        RectData r = ui.rect(image);
        expect(r.anchorMin.y == 0.0f && r.anchorMax.y == 1.0f && r.pivot.y == 0.5f,
               "story image is not stretched vertically");
        expect(ui.get(image, Comp::AspectFitter, Prop::Enabled) != 0.0 &&
                   static_cast<int>(ui.get(image, Comp::AspectFitter, Prop::AspectMode)) == kAspectHeightControlsWidth,
               "story image aspect fitter changed");
        if (!orig_) orig_ = r;
    }
    void apply(Tx &tx, Handle root, const Context &ctx) override {
        float top = 0;
        if (ctx.shared.panels_enlarged) {
            // Panels not seen yet use the values of the verified game build.
            layout::DialoguePanel d = ctx.shared.dialogue_panel.value_or(layout::DialoguePanel{});
            layout::DialoguePanel n = ctx.shared.narrator_panel.value_or(layout::DialoguePanel{});
            top = std::max(layout::dialogue_panel_top(d, ctx.settings.scale),
                           layout::dialogue_panel_top(n, ctx.settings.scale));
        } else {
            top = layout::DialoguePanel{}.panel_bottom + layout::DialoguePanel{}.panel_height;
        }
        // FR-UI-13: the aspect fitter keeps the ratio; the top margin stays.
        tx.set_rect(tx.ui().find(root, "Image"), layout::clear_image_above(*orig_, top));
    }

  private:
    std::optional<RectData> orig_;
};

// ---------------------------------------------------------------------------
// Text log (FR-UI-15 ... FR-UI-18)

class TextLogUnit : public Unit {
  public:
    TextLogUnit() : Unit("text_log", "text_log", Target::TextLog) {}
    bool matches(Ui &, Handle) override { return true; }
    // Tracked so the runtime notices when the log closes (FR-UI-19).
    bool wants_tick() const override { return true; }

    static constexpr const char *kContent = "Scroll View/Viewport/Content";
    static constexpr const char *kEntry = "Scroll View/Viewport/Content/Text Entry";

    void validate(Ui &ui, Handle root) override {
        Handle view = need(ui, root, "Scroll View");
        Handle content = need(ui, root, kContent, {Comp::VLayout});
        Handle entry = need(ui, root, kEntry, {Comp::VLayout});
        Handle labels[4];
        for (int i = 0; i < 4; ++i) {
            labels[i] = need(ui, entry, kLabels[i].name, {kLabels[i].comp, Comp::SizeFitter});
            // FR-UI-18: entries must keep fitting their preferred text height.
            expect(static_cast<int>(ui.get(labels[i], Comp::SizeFitter, Prop::FitVertical)) == kFitPreferred,
                   std::string("text log '") + kLabels[i].name + "' lost its preferred-height fitter");
        }
        if (orig_) return;
        Originals o;
        o.view = ui.rect(view);
        o.content_spacing = static_cast<float>(ui.get(content, Comp::VLayout, Prop::LgSpacing));
        o.entry_spacing = static_cast<float>(ui.get(entry, Comp::VLayout, Prop::LgSpacing));
        for (int i = 0; i < 4; ++i) {
            o.rects[i] = ui.rect(labels[i]);
            if (kLabels[i].comp == Comp::Tmp) o.tmp[i] = read_tmp(ui, labels[i]);
            else o.stm[i] = static_cast<float>(ui.get(labels[i], Comp::Stm, Prop::StmSize));
        }
        orig_ = o;
    }

    void apply(Tx &tx, Handle root, const Context &ctx) override {
        Ui &ui = tx.ui();
        const Originals &o = *orig_;
        const double s = ctx.settings.scale;
        Handle content = ui.find(root, kContent);
        tx.set(content, Comp::VLayout, Prop::LgSpacing, o.content_spacing * s);
        // The template and any entries the game already created from it.
        for (int i = 0, n = ui.child_count(content); i < n; ++i) {
            Handle entry = ui.child(content, i);
            if (!ui.has(entry, Comp::VLayout)) continue;  // dividers
            bool complete = true;
            for (const auto &l : kLabels) complete = complete && ui.find(entry, l.name);
            if (!complete) continue;
            apply_entry(tx, entry, s);
        }
        // FR-UI-17: a wider viewport (originally 75 % of the screen).
        RectData v = o.view;
        v.anchorMin.x = 0.075f;
        v.anchorMax.x = 0.925f;
        tx.set_rect(ui.find(root, "Scroll View"), v);
    }

  private:
    struct Label {
        const char *name;
        Comp comp;
    };
    static constexpr Label kLabels[4] = {
        {"Char Name", Comp::Tmp}, {"Text", Comp::Stm}, {"Text 2", Comp::Stm}, {"Large Text", Comp::Tmp}};

    struct Originals {
        RectData view;
        float content_spacing = 0, entry_spacing = 0;
        RectData rects[4];
        TmpSize tmp[4];
        float stm[4] = {};
    };

    void apply_entry(Tx &tx, Handle entry, double s) {
        Ui &ui = tx.ui();
        const Originals &o = *orig_;
        tx.set(entry, Comp::VLayout, Prop::LgSpacing, o.entry_spacing * s);
        for (int i = 0; i < 4; ++i) {
            Handle label = ui.find(entry, kLabels[i].name);
            if (kLabels[i].comp == Comp::Tmp) {
                write_tmp(tx, label, layout::scale_tmp(o.tmp[i], s, false));
                tx.set(label, Comp::Tmp, Prop::TmpCharacterSpacing, 0);  // FR-UI-16
            } else {
                tx.set(label, Comp::Stm, Prop::StmSize, layout::scaled_font(o.stm[i], s));
                tx.set(label, Comp::Stm, Prop::StmCharacterSpacing, 0);  // FR-UI-16
            }
            RectData r = ui.rect(label);
            r.sizeDelta.y = o.rects[i].sizeDelta.y * static_cast<float>(s);
            tx.set_rect(label, r);
        }
    }

    std::optional<Originals> orig_;
};

// ---------------------------------------------------------------------------
// Description that floats around the picture (FR-UI-22 ... FR-UI-24, FR-UI-33).
// TextMesh Pro can't wrap one text box around a region, so linked overflow
// does the split: the game keeps writing the localized text to its own
// component (the head, beside the picture), and the continuation only ever
// shows what doesn't fit there. Both parts sit on one line grid: the head holds
// exactly the lines that start beside the picture, and the continuation starts
// one line pitch below the head's last line, at the full row width. So the text
// reads as one block, like CSS `float: right`.

const char *const kContinuationName = "LTM Description Continuation";
// An invisible copy of the description, used only to measure line metrics at
// the current font and size, so the measuring never touches displayed text.
const char *const kProbeName = "LTM Line Probe";

// Shows the continuation only while there is overflow and sizes it to the
// rendered overflow text. Returns its height (0 when hidden).
float sync_continuation(Ui &ui, Handle head, Handle continuation) {
    ui.update_text(head, Comp::Tmp);
    bool overflow = ui.get(head, Comp::Tmp, Prop::TmpOverflowing) != 0.0;
    if (ui.active_self(continuation) != overflow) ui.set_active(continuation, overflow);
    if (!overflow) return 0.0f;
    ui.update_text(continuation, Comp::Tmp);
    float h = std::ceil(sane_height(ui.get(continuation, Comp::Tmp, Prop::TmpRenderedHeight)));
    RectData r = ui.rect(continuation);
    if (!near(r.sizeDelta.y, h, 0.5f)) {
        r.sizeDelta.y = h;
        ui.set_rect(continuation, r);
    }
    return h;
}

// Detail rows take their text's preferred height, or none when empty.
// Returns the total height of the rows.
float sync_rows(Ui &ui, Handle fields, const char *names, const char *values, float spacing) {
    float total = 0;
    int shown = 0;
    for (int i = 0, n = ui.child_count(fields); i < n; ++i) {
        Handle row = ui.child(fields, i);
        if (!ui.active_self(row)) continue;
        Handle a = ui.find(row, names);
        Handle b = ui.find(row, values);
        if (!a || !b) continue;
        float h = std::ceil(std::max(sane_height(ui.get(a, Comp::Tmp, Prop::TmpPreferredHeight)),
                                     sane_height(ui.get(b, Comp::Tmp, Prop::TmpPreferredHeight))));
        RectData r = ui.rect(row);
        if (!near(r.sizeDelta.y, h, 0.5f)) {
            r.sizeDelta.y = h;
            ui.set_rect(row, r);
        }
        total += h;
        ++shown;
    }
    if (shown > 1) total += spacing * static_cast<float>(shown - 1);
    return total;
}

void set_height(Ui &ui, Handle go, float h) {
    RectData r = ui.rect(go);
    if (!near(r.sizeDelta.y, h, 0.5f)) {
        r.sizeDelta.y = h;
        ui.set_rect(go, r);
    }
}

// Gap between the item name, the detail rows, the View button and the
// description beside the picture.
constexpr float kInfoGap = 10.0f;

// Anchors a rect to the top of its parent at `y` with height `h`, keeping its
// horizontal layout.
RectData top_anchored(RectData r, float y, float h) {
    r.anchorMin.y = r.anchorMax.y = 1.0f;
    r.pivot.y = 1.0f;
    r.anchoredPosition.y = y;
    r.sizeDelta.y = h;
    return r;
}

// Moves a top-anchored rect to `y` with height `h`.
void place_top(Ui &ui, Handle go, float y, float h) {
    RectData r = ui.rect(go);
    if (!near(r.anchoredPosition.y, y, 0.5f) || !near(r.sizeDelta.y, h, 0.5f)) {
        r.anchoredPosition.y = y;
        r.sizeDelta.y = h;
        ui.set_rect(go, r);
    }
}

// A text in the text column: from its left edge, `extra_width` wider than the
// column (0 for the column itself, the picture and its gap for the full row),
// top-anchored at `y` with height `h`.
RectData flow_rect(float y, float h, float extra_width) {
    RectData d;
    d.anchorMin = {0, 1};
    d.anchorMax = {1, 1};
    d.pivot = {0, 1};
    d.anchoredPosition = {0, y};
    d.sizeDelta = {extra_width, h};
    return d;
}

// Makes `probe` an invisible measuring copy of the description.
void setup_probe(Tx &tx, Handle probe) {
    Ui &ui = tx.ui();
    tx.set_rect(probe, flow_rect(0, 0, 0));
    tx.set(probe, Comp::Tmp, Prop::TmpOverflowMode, kOverflowOverflow);
    tx.set(probe, Comp::Tmp, Prop::GraphicAlpha, 0);
    if (ui.has(probe, Comp::SizeFitter)) tx.set(probe, Comp::SizeFitter, Prop::Enabled, 0);
}

// Lays the description out around a picture at the top right of the text
// column (FR-UI-22). `y` is where the description starts and `picture_bottom`
// where the picture ends, both measured down from the column's top;
// `extra_width` is the picture's width plus its gap. Lines that start beside
// the picture stay in the column's width; as with a CSS float, the last of
// them may reach a little below the picture. Returns how far below the
// column's top the text ends.
float flow_description(Ui &ui, Handle head, Handle continuation, Handle probe, float y, float picture_bottom,
                       float extra_width) {
    float single = sane_height(ui.get(probe, Comp::Tmp, Prop::TmpLineHeight));
    float pitch = sane_height(ui.get(probe, Comp::Tmp, Prop::TmpLinePitch));
    if (!(pitch > 0.0f) || !(single > 0.0f)) throw Mismatch("could not measure the description lines");
    float avail = picture_bottom + y;
    int lines = avail > 0.5f ? static_cast<int>(std::ceil((avail - 0.5f) / pitch)) : 0;
    // Exactly `lines` lines fit; the next one moves to the continuation.
    float head_h = lines > 0 ? single + pitch * static_cast<float>(lines - 1) + 0.5f : 0.0f;
    place_top(ui, head, y, head_h);
    float cont_y = y - pitch * static_cast<float>(lines);
    RectData r = ui.rect(continuation);
    if (!near(r.anchoredPosition.y, cont_y, 0.5f) || !near(r.sizeDelta.x, extra_width, 0.5f)) {
        r.anchoredPosition.y = cont_y;
        r.sizeDelta.x = extra_width;
        ui.set_rect(continuation, r);
    }
    float cont_h = sync_continuation(ui, head, continuation);
    if (cont_h > 0.0f) return -cont_y + cont_h;
    if (lines == 0) return -y;
    return -y + std::min(head_h, std::ceil(sane_height(ui.get(head, Comp::Tmp, Prop::TmpPreferredHeight))));
}

void set_font_size(Ui &ui, Handle go, float size) {
    if (!near(static_cast<float>(ui.get(go, Comp::Tmp, Prop::TmpFontSize)), size)) {
        ui.set(go, Comp::Tmp, Prop::TmpFontSize, size);
    }
}

// FR-UI-0d for the flowed description: finds the largest size in
// [floor, target] at which the height `layout()` needs fits into `available`,
// and leaves all parts laid out at that size (same size in both, FR-UI-23).
void fit_description(Ui &ui, std::initializer_list<Handle> texts, float target, float floor, float available,
                     const std::function<float()> &layout) {
    std::vector<Handle> parts(texts);
    auto measure = [&](float size) {
        for (Handle t : parts) set_font_size(ui, t, size);
        return layout();
    };
    // No usable measure yet (e.g. before the first layout): keep the enlarged size.
    if (!(available > 0.0f)) {
        measure(target);
        return;
    }
    float size = layout::largest_fitting(target, floor, [&](float s) { return measure(s) <= available + 0.5f; });
    measure(size);
}

// Bottom edge of a rect, measured up from its parent's bottom (parent height `h`).
float bottom_edge(const RectData &r, float h) {
    return r.anchorMin.y * h + r.anchoredPosition.y - r.sizeDelta.y * r.pivot.y;
}

// ---------------------------------------------------------------------------
// Evidence menu: Clues, Spells and People share one display (FR-UI-20 ... 28)

class EvidenceMenuUnit : public Unit {
  public:
    EvidenceMenuUnit() : Unit("evidence_menu", "evidence_menu", Target::ItemMenu) {}
    bool matches(Ui &, Handle) override { return true; }
    bool wants_tick() const override { return true; }

    static constexpr const char *kBase = "Container/Right/Container";

    void validate(Ui &ui, Handle root) override {
        Handle base = need(ui, root, kBase);
        Handle top = need(ui, base, "Top", {Comp::HLayout});
        Handle icon = need(ui, base, "Top/Icon", {Comp::AspectFitter});
        Handle info = need(ui, base, "Top/Item Info");
        need(ui, base, "Top/Item Info/Item Name", {Comp::Tmp});
        Handle fields = need(ui, base, "Top/Item Info/Fields", {Comp::VLayout});
        Handle view = need(ui, base, "Top/Item Info/View Button", {Comp::Button});
        if (ui.find(info, kContinuationName)) throw AlreadyApplied();
        Handle description = need(ui, base, "Description", {Comp::Tmp});
        expect(ui.parent(icon) == top && ui.parent(info) == top, "evidence picture layout changed");
        if (orig_) return;
        Originals o;
        o.description = ui.rect(description);
        o.description_text = read_tmp(ui, description);
        o.fields_spacing = static_cast<float>(ui.get(fields, Comp::VLayout, Prop::LgSpacing));
        o.fields = ui.rect(fields);
        o.view = ui.rect(view);
        o.view_height = ui.rect_size(view).y;
        RectData name = ui.rect(ui.find(base, "Top/Item Info/Item Name"));
        o.name_bottom = name.anchoredPosition.y - name.sizeDelta.y;  // top-anchored, top pivot
        expect(name.anchorMin.y == 1.0f && name.anchorMax.y == 1.0f && name.pivot.y == 1.0f,
               "evidence item name is not top-anchored");
        orig_ = o;
    }

    void apply(Tx &tx, Handle root, const Context &ctx) override {
        Ui &ui = tx.ui();
        const Originals &o = *orig_;
        Handle base = ui.find(root, kBase);
        Handle top = ui.find(base, "Top");
        Handle icon = ui.find(base, "Top/Icon");
        Handle info = ui.find(base, "Top/Item Info");
        Handle fields = ui.find(base, "Top/Item Info/Fields");
        Handle view = ui.find(base, "Top/Item Info/View Button");
        Handle description = ui.find(base, "Description");

        // FR-UI-20/23: larger text, no automatic shrinking, same size in both parts.
        TmpSize t = layout::scale_tmp(o.description_text, ctx.settings.scale, true);
        t.auto_size = false;
        write_tmp(tx, description, t);
        if (ui.has(description, Comp::SizeFitter)) tx.set(description, Comp::SizeFitter, Prop::Enabled, 0);

        // FR-UI-25: details and View stay beside the picture, below the name;
        // the per-frame tick stacks them by their current heights.
        float y = o.name_bottom - kInfoGap;
        float fields_h = o.fields.sizeDelta.y;
        tx.set_rect(fields, top_anchored(o.fields, y, fields_h));
        y -= fields_h + kInfoGap;
        tx.set_rect(view, top_anchored(o.view, y, o.view_height));
        y -= o.view_height + kInfoGap;

        // FR-UI-22: the description starts left of the picture, below them, and
        // flows on below the picture. The probe and the continuation are copies
        // of the description (same font, size, localization font switcher).
        tx.set_parent(description, info, -1);
        Handle probe = tx.clone(description, info, kProbeName);
        setup_probe(tx, probe);
        Handle continuation = tx.clone(description, info, kContinuationName);
        tx.set(continuation, Comp::Tmp, Prop::TmpOverflowMode, kOverflowOverflow);
        tx.set_rect(continuation, flow_rect(y, 0, 0));
        tx.set_rect(description, flow_rect(y, 0, 0));
        tx.set(description, Comp::Tmp, Prop::TmpOverflowMode, kOverflowLinked);
        tx.set_linked_text(description, continuation);

        // FR-UI-21: picture on the right. FR-UI-27: it keeps its square size.
        tx.set_parent(icon, top, -1);
        tx.set(top, Comp::HLayout, Prop::LgExpandWidth, 0);
    }

    void tick(Ui &ui, Handle root, const Context &ctx) override {
        if (!ui.active_in_hierarchy(root)) return;
        Handle base = ui.find(root, kBase);
        Handle top = ui.find(base, "Top");
        Handle icon = ui.find(base, "Top/Icon");
        Handle info = ui.find(base, "Top/Item Info");
        Handle head = ui.find(base, "Top/Item Info/Description");
        Handle fields = ui.find(base, "Top/Item Info/Fields");
        Handle view = ui.find(base, "Top/Item Info/View Button");
        Handle continuation = info ? ui.find(info, kContinuationName) : nullptr;
        Handle probe = info ? ui.find(info, kProbeName) : nullptr;
        if (!top || !icon || !info || !head || !fields || !view || !continuation || !probe)
            throw Mismatch("evidence menu structure changed at runtime");

        // The text column fills the space left of the square picture.
        float spacing = static_cast<float>(ui.get(top, Comp::HLayout, Prop::LgSpacing));
        float picture_w = ui.rect_size(icon).x;
        float width = std::max(0.0f, ui.rect_size(top).x - picture_w - spacing);
        RectData i = ui.rect(info);
        if (!near(i.sizeDelta.x, width, 0.5f)) {
            i.sizeDelta.x = width;
            ui.set_rect(info, i);
        }

        // Re-measure only when the text or the available width changed.
        const void *id = ui.text_identity(head, Comp::Tmp);
        if (id != last_text_ || !near(width, last_width_, 0.5f) || ++frames_ >= 30) {
            last_text_ = id;
            last_width_ = width;
            frames_ = 0;
            const Originals &orig = *orig_;
            // FR-UI-25/26: details and View stack below the name, beside the
            // picture; the description starts below them.
            float fields_h = sync_rows(ui, fields, "Item Field Names", "Item Field Values", orig.fields_spacing);
            float y = orig.name_bottom - kInfoGap;
            place_top(ui, fields, y, fields_h);
            if (fields_h > 0.0f) y -= fields_h + kInfoGap;
            place_top(ui, view, y, orig.view_height);
            if (ui.active_self(view) && orig.view_height > 0.0f) y -= orig.view_height + kInfoGap;
            // FR-UI-22: the picture fills the row's height. FR-UI-27/0d: the
            // text may reach down to where the original description ended,
            // so the panel keeps its bounds; when it doesn't fit, it shrinks.
            float row_h = ui.rect_size(info).y;
            float base_h = ui.rect_size(base).y;
            float below = std::max(0.0f, bottom_edge(ui.rect(top), base_h) - bottom_edge(orig.description, base_h));
            float extra = picture_w + spacing;
            const TmpSize &o = orig.description_text;
            fit_description(ui, {head, continuation, probe}, layout::scale_tmp(o, ctx.settings.scale, true).font_size,
                            layout::fit_floor(o), row_h + below,
                            [&] { return flow_description(ui, head, continuation, probe, y, row_h, extra); });
        }
    }

  private:
    struct Originals {
        RectData description;
        TmpSize description_text;
        RectData fields, view;
        float fields_spacing = 0;
        float view_height = 0;
        float name_bottom = 0;
    };
    std::optional<Originals> orig_;
    const void *last_text_ = nullptr;
    float last_width_ = -1;
    int frames_ = 0;
};

// ---------------------------------------------------------------------------
// "Obtained evidence!" / "Evidence updated!" (FR-UI-29 ... FR-UI-35)

class NotificationUnit : public Unit {
  public:
    NotificationUnit() : Unit("evidence_notification", "evidence_notification", Target::Notification) {}
    bool matches(Ui &, Handle) override { return true; }
    bool wants_tick() const override { return true; }

    void validate(Ui &ui, Handle root) override {
        Handle window = need(ui, root, "Window", {Comp::VLayout, Comp::SizeFitter});
        Handle header = need(ui, root, "Window/Header", {Comp::Tmp});
        Handle top = need(ui, root, "Window/Top");
        Handle icon = need(ui, root, "Window/Top/Icon");
        need(ui, root, "Window/Top/Item Info");
        Handle name = need(ui, root, "Window/Top/Item Info/Item Name", {Comp::Tmp});
        Handle fields = need(ui, root, "Window/Top/Item Info/Item Fields", {Comp::VLayout});
        if (ui.find(root, std::string("Window/Top/Item Info/") + kContinuationName)) throw AlreadyApplied();
        Handle description = need(ui, root, "Window/Description", {Comp::Tmp});
        expect(ui.get(window, Comp::VLayout, Prop::LgControlHeight) == 0.0, "notification layout changed");
        RectData wr = ui.rect(window);
        expect(wr.anchorMin.y == wr.anchorMax.y, "notification panel is stretched vertically");
        if (orig_) return;
        Originals o;
        o.window = ui.rect(window);
        o.header = ui.rect(header);
        o.header_text = read_tmp(ui, header);
        o.top = ui.rect(top);
        o.icon = ui.rect(icon);
        o.name = ui.rect(name);
        o.name_text = read_tmp(ui, name);
        o.description_text = read_tmp(ui, description);
        o.fields_spacing = static_cast<float>(ui.get(fields, Comp::VLayout, Prop::LgSpacing));
        o.fields = ui.rect(fields);
        for (int i = 0, n = ui.child_count(fields); i < n; ++i) {
            Handle row = ui.child(fields, i);
            for (const char *label : {"Item Field Names", "Item Field Values"}) {
                Handle l = ui.find(row, label);
                if (l && ui.has(l, Comp::Tmp)) o.row_text[label] = read_tmp(ui, l);
            }
        }
        expect(o.row_text.size() == 2, "notification detail rows changed");
        expect(o.icon.sizeDelta.x > 1.0f && o.icon.sizeDelta.y > 1.0f, "unexpected notification picture size");
        orig_ = o;
    }

    void apply(Tx &tx, Handle root, const Context &ctx) override {
        Ui &ui = tx.ui();
        const Originals &o = *orig_;
        const double s = ctx.settings.scale;
        Handle window = ui.find(root, "Window");
        Handle header = ui.find(root, "Window/Header");
        Handle top = ui.find(root, "Window/Top");
        Handle icon = ui.find(root, "Window/Top/Icon");
        Handle info = ui.find(root, "Window/Top/Item Info");
        Handle name = ui.find(root, "Window/Top/Item Info/Item Name");
        Handle fields = ui.find(root, "Window/Top/Item Info/Item Fields");
        Handle description = ui.find(root, "Window/Description");

        // FR-UI-30: wider panel. FR-UI-35: the height fits the contents.
        RectData w = o.window;
        w.anchorMin.x = 0.075f;
        w.anchorMax.x = 0.925f;
        tx.set_rect(window, w);
        tx.set(window, Comp::SizeFitter, Prop::FitVertical, kFitPreferred);

        // FR-UI-31
        write_tmp(tx, header, layout::scale_heading(o.header_text, s));
        RectData h = o.header;
        h.sizeDelta.y = layout::heading_height(o.header.sizeDelta.y, s, o.window.anchorMax.y);
        tx.set_rect(header, h);

        // FR-UI-32
        write_tmp(tx, name, layout::scale_tmp(o.name_text, s, true));
        RectData n = o.name;
        n.sizeDelta.y = o.name.sizeDelta.y * static_cast<float>(s);
        tx.set_rect(name, n);
        for (int i = 0, count = ui.child_count(fields); i < count; ++i) {
            Handle row = ui.child(fields, i);
            for (const auto &entry : o.row_text) {
                Handle l = ui.find(row, entry.first);
                if (l && ui.has(l, Comp::Tmp)) write_tmp(tx, l, layout::scale_tmp(entry.second, s, true));
            }
        }
        TmpSize t = layout::scale_tmp(o.description_text, s, false);
        t.auto_size = false;
        write_tmp(tx, description, t);
        // The parent layout now sizes the text; its own fitter would fight it.
        if (ui.has(description, Comp::SizeFitter)) tx.set(description, Comp::SizeFitter, Prop::Enabled, 0);

        // FR-UI-33: fixed square picture on the right, text to its left.
        const float gap = kPictureGap;
        RectData ic = o.icon;
        ic.anchorMin = ic.anchorMax = {1, 1};
        ic.pivot = {1, 1};
        ic.anchoredPosition = {0, 0};
        tx.set_rect(icon, ic);
        RectData in;
        in.anchorMin = {0, 0};
        in.anchorMax = {1, 1};
        in.pivot = {0.5f, 1};
        in.sizeDelta = {-(o.icon.sizeDelta.x + gap), 0};
        in.anchoredPosition = {-(o.icon.sizeDelta.x + gap) / 2.0f, 0};
        tx.set_rect(info, in);
        RectData tp = o.top;
        tp.sizeDelta.y = std::max(o.top.sizeDelta.y, o.icon.sizeDelta.y);
        tx.set_rect(top, tp);

        // FR-UI-34: details stay beside the picture, below the name; the
        // description starts below them. The tick keeps this up to date.
        float y = name_bottom(s) - kInfoGap;
        float fields_h = o.fields.sizeDelta.y;
        tx.set_rect(fields, top_anchored(o.fields, y, fields_h));
        y -= fields_h + kInfoGap;

        // FR-UI-33: the description flows around the picture. The picture row
        // grows to hold all of it, so the continuation lives in the text column
        // and reaches across the picture's width below it.
        tx.set_parent(description, info, -1);
        Handle probe = tx.clone(description, info, kProbeName);
        setup_probe(tx, probe);
        Handle continuation = tx.clone(description, info, kContinuationName);
        tx.set(continuation, Comp::Tmp, Prop::TmpOverflowMode, kOverflowOverflow);
        tx.set_rect(continuation, flow_rect(y, 0, picture_width()));
        tx.set_rect(description, flow_rect(y, 0, 0));
        tx.set(description, Comp::Tmp, Prop::TmpOverflowMode, kOverflowLinked);
        tx.set_linked_text(description, continuation);
    }

    void tick(Ui &ui, Handle root, const Context &ctx) override {
        if (!ui.active_in_hierarchy(root)) return;
        Handle window = ui.find(root, "Window");
        Handle header = ui.find(root, "Window/Header");
        Handle top = ui.find(root, "Window/Top");
        Handle head = ui.find(root, "Window/Top/Item Info/Description");
        Handle continuation = ui.find(root, std::string("Window/Top/Item Info/") + kContinuationName);
        Handle probe = ui.find(root, std::string("Window/Top/Item Info/") + kProbeName);
        Handle fields = ui.find(root, "Window/Top/Item Info/Item Fields");
        if (!window || !top || !head || !continuation || !probe || !fields)
            throw Mismatch("notification structure changed at runtime");
        const void *id = ui.text_identity(head, Comp::Tmp);
        if (id != last_text_ || ++frames_ >= 30) {
            last_text_ = id;
            frames_ = 0;
            const Originals &o = *orig_;
            // FR-UI-34: details below the name, beside the picture; the
            // description starts below them.
            float fields_h = sync_rows(ui, fields, "Item Field Names", "Item Field Values", o.fields_spacing);
            float y = name_bottom(ctx.settings.scale) - kInfoGap;
            place_top(ui, fields, y, fields_h);
            if (fields_h > 0.0f) y -= fields_h + kInfoGap;
            // FR-UI-33/35: the picture row is as tall as the picture, the name
            // and details, and the description flowing around the picture.
            // FR-UI-0d: when that would take the panel off the screen, the
            // description shrinks. The heading sits above the panel and has
            // its own limit (FR-UI-31).
            float min_top = std::max(std::max(o.top.sizeDelta.y, o.icon.sizeDelta.y), -y);
            float spacing = static_cast<float>(ui.get(window, Comp::VLayout, Prop::LgSpacing));
            auto panel_height = [&] {
                float top_h = std::max(min_top, flow_description(ui, head, continuation, probe, y,
                                                                 o.icon.sizeDelta.y, picture_width()));
                set_height(ui, top, top_h);
                float total = 0;
                int shown = 0;
                for (int i = 0, n = ui.child_count(window); i < n; ++i) {
                    Handle c = ui.child(window, i);
                    if (c == header) continue;
                    float h = c == top ? top_h : ui.active_self(c) ? ui.rect_size(c).y : 0.0f;
                    if (h <= 0.0f) continue;
                    total += h;
                    ++shown;
                }
                return total + (shown > 1 ? spacing * static_cast<float>(shown - 1) : 0.0f);
            };
            float available = layout::max_panel_height(o.window.anchorMin.y, o.window.anchoredPosition.y, o.window.pivot.y);
            fit_description(ui, {head, continuation, probe},
                            layout::scale_tmp(o.description_text, ctx.settings.scale, false).font_size,
                            layout::fit_floor(o.description_text), available, panel_height);
        }
    }

  private:
    struct Originals {
        RectData window, header, top, icon, name, fields;
        TmpSize header_text, name_text, description_text;
        std::map<std::string, TmpSize> row_text;
        float fields_spacing = 0;
    };

    // Bottom edge of the enlarged item name, measured from the top of the text
    // column (the name is top-anchored, as in the game).
    float name_bottom(double scale) const {
        return orig_->name.anchoredPosition.y - orig_->name.sizeDelta.y * static_cast<float>(scale);
    }

    // The picture and its gap: how much wider than the text column the
    // description is below the picture.
    float picture_width() const { return orig_->icon.sizeDelta.x + kPictureGap; }
    static constexpr float kPictureGap = 30.0f;

    std::optional<Originals> orig_;
    const void *last_text_ = nullptr;
    int frames_ = 0;
};

}  // namespace

std::vector<std::unique_ptr<Unit>> make_units() {
    std::vector<std::unique_ptr<Unit>> units;
    units.push_back(std::make_unique<DialogueBoxUnit>("dialogue_box", "dialogue_box", BoxKind::Dialogue));
    units.push_back(std::make_unique<DialogueBoxUnit>("narrator_box", "dialogue_box", BoxKind::Narrator));
    units.push_back(std::make_unique<DialogueBoxUnit>("cross_examination", "cross_examination", BoxKind::Testimony));
    units.push_back(std::make_unique<DialogueFitUnit>(
        "dialogue_text_fit", "dialogue_box", std::initializer_list<BoxKind>{BoxKind::Dialogue, BoxKind::Narrator}));
    units.push_back(std::make_unique<DialogueFitUnit>(
        "cross_examination_text_fit", "cross_examination", std::initializer_list<BoxKind>{BoxKind::Testimony}));
    units.push_back(std::make_unique<LineRestoreUnit>());
    units.push_back(std::make_unique<StoryImageUnit>());
    units.push_back(std::make_unique<TextLogUnit>());
    units.push_back(std::make_unique<EvidenceMenuUnit>());
    units.push_back(std::make_unique<NotificationUnit>());
    return units;
}

}  // namespace ltm
