#include "framework.h"

#include "tx.h"

namespace ltm {

const char *comp_name(Comp c) {
    static const char *const names[] = {"TMP_Text", "SuperTextMesh", "RawImage", "Graphic",
                                        "HorizontalLayoutGroup", "VerticalLayoutGroup", "LayoutElement",
                                        "ContentSizeFitter", "AspectRatioFitter", "Button", "DialogueBox",
                                        "TestimonyPrompt", "TextLogGUI", "ItemMenu", "AddItemDisplay", "ImageDisplay"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Comp::Count), "component names");
    return names[static_cast<size_t>(c)];
}

const char *prop_name(Prop p) {
    static const char *const names[] = {
        "enabled", "fontSize", "enableAutoSizing", "fontSizeMin", "fontSizeMax", "characterSpacing",
        "overflowMode", "preferredHeight", "renderedHeight", "overflowing", "size", "characterSpacing",
        "rememberReadPosition", "reading", "currentReadTime", "totalReadTime", "color.a", "spacing", "childAlignment", "childControlWidth",
        "childControlHeight", "childForceExpandWidth", "childForceExpandHeight", "ignoreLayout", "minWidth",
        "minHeight", "preferredWidth", "preferredHeight", "flexibleWidth", "flexibleHeight", "horizontalFit",
        "verticalFit", "aspectMode"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Prop::Count), "property names");
    return names[static_cast<size_t>(p)];
}

Runtime::Runtime(const Settings &settings, Log &log) : Runtime(settings, log, make_units()) {}

Runtime::Runtime(const Settings &settings, Log &log, std::vector<std::unique_ptr<Unit>> units)
    : settings_(settings), log_(log) {
    for (auto &u : units) {
        Slot slot;
        slot.key = u->key();
        slot.config_key = u->config_key();
        slot.unit = std::move(u);
        slots_.push_back(std::move(slot));
    }
    Slot focus;
    focus.key = focus.config_key = "pause_when_unfocused";
    slots_.push_back(std::move(focus));
    for (Slot &slot : slots_) {
        if (!settings_.enabled || !settings_.feature(slot.config_key)) slot.state = UnitState::Disabled;
    }
}

Runtime::Slot *Runtime::find(const std::string &key) {
    for (Slot &s : slots_)
        if (s.key == key) return &s;
    return nullptr;
}

const Runtime::Slot *Runtime::find(const std::string &key) const {
    for (const Slot &s : slots_)
        if (s.key == key) return &s;
    return nullptr;
}

void Runtime::report_disabled() {
    for (const Slot &s : slots_)
        if (s.state == UnitState::Disabled) log_.write(Level::Info, "FEATURE " + s.key + " disabled");
}

Context Runtime::context() {
    const Slot *d = find("dialogue_box");
    shared_.panels_enlarged = d && (d->state == UnitState::Pending || d->state == UnitState::Applied);
    return Context{settings_, shared_};
}

void Runtime::on_instance(Ui &ui, Target target, Handle root) {
    if (!root) return;
    for (Slot &slot : slots_) {
        if (!slot.unit || slot.unit->target() != target) continue;
        if (slot.state != UnitState::Pending && slot.state != UnitState::Applied) continue;
        apply_to(ui, slot, root);
    }
}

void Runtime::apply_to(Ui &ui, Slot &slot, Handle root) {
    int id = 0;
    try {
        id = ui.instance_id(root);
        if (slot.instances.count(id)) return;  // FR-UI-0b: once per instance
        if (!slot.unit->matches(ui, root)) return;
    } catch (const std::exception &e) {
        disable(ui, slot, UnitState::Failed, e.what());
        return;
    }

    Tx tx(ui);
    try {
        slot.unit->validate(ui, root);  // FR-RT-6: check before changing anything
        slot.unit->apply(tx, root, context());
        tx.commit();
    } catch (const AlreadyApplied &) {
        slot.instances.insert(id);
        log_.write(Level::Debug, slot.key + ": instance " + std::to_string(id) + " already carries the changes");
        return;
    } catch (const Mismatch &e) {
        int failed = tx.rollback();
        disable(ui, slot, UnitState::Skipped,
                std::string(e.what()) + (failed ? " (rollback incomplete)" : ""));
        return;
    } catch (const std::exception &e) {
        int failed = tx.rollback();
        disable(ui, slot, UnitState::Failed, std::string(e.what()) + (failed ? " (rollback incomplete)" : ""));
        return;
    } catch (...) {
        int failed = tx.rollback();
        disable(ui, slot, UnitState::Failed, std::string("unknown error") + (failed ? " (rollback incomplete)" : ""));
        return;
    }
    slot.instances.insert(id);
    if (slot.state != UnitState::Applied) {
        slot.state = UnitState::Applied;
        log_.write(Level::Info, "FEATURE " + slot.key + " applied");
    }
    log_.write(Level::Debug, slot.key + ": applied to instance " + std::to_string(id));
    if (slot.unit->wants_tick()) {
        ui.retain(root);
        slot.tracked.push_back(root);
    }
}

void Runtime::disable(Ui &ui, Slot &slot, UnitState state, const std::string &reason) {
    slot.state = state;
    slot.reason = reason;
    for (Handle h : slot.tracked) ui.release(h);
    slot.tracked.clear();
    log_.write(state == UnitState::Skipped ? Level::Warn : Level::Error,
               "FEATURE " + slot.key + (state == UnitState::Skipped ? " skipped: " : " failed: ") + reason);
}

void Runtime::on_frame(Ui &ui) {
    // Notice the text log closing (FR-UI-19).
    bool open = false;
    for (Slot &slot : slots_) {
        if (!slot.unit || slot.unit->target() != Target::TextLog) continue;
        for (Handle root : slot.tracked) {
            try {
                if (ui.alive(root) && ui.active_in_hierarchy(root)) open = true;
            } catch (...) {
            }
        }
    }
    if (text_log_open_ && !open) shared_.text_log_closed_frames = 5;
    text_log_open_ = open;
    for (Slot &slot : slots_) {
        if (!slot.unit || slot.state != UnitState::Applied || slot.tracked.empty()) continue;
        for (size_t i = 0; i < slot.tracked.size();) {
            Handle root = slot.tracked[i];
            try {
                if (!ui.alive(root)) {
                    ui.release(root);
                    slot.tracked.erase(slot.tracked.begin() + static_cast<long>(i));
                    continue;
                }
                slot.unit->tick(ui, root, context());
                ++i;
            } catch (const Mismatch &e) {
                disable(ui, slot, UnitState::Skipped, e.what());
                break;
            } catch (const std::exception &e) {
                disable(ui, slot, UnitState::Failed, e.what());
                break;
            } catch (...) {
                disable(ui, slot, UnitState::Failed, "unknown error");
                break;
            }
        }
    }
    if (shared_.text_log_closed_frames > 0) --shared_.text_log_closed_frames;
}

void Runtime::target_unavailable(Target target, const std::string &reason) {
    for (Slot &slot : slots_) {
        if (!slot.unit || slot.unit->target() != target) continue;
        if (slot.state != UnitState::Pending && slot.state != UnitState::Applied) continue;
        slot.state = UnitState::Failed;
        slot.reason = reason;
        log_.write(Level::Error, "FEATURE " + slot.key + " failed: " + reason);
    }
}

bool Runtime::global_enabled(const std::string &key) const {
    const Slot *s = find(key);
    return s && (s->state == UnitState::Pending || s->state == UnitState::Applied);
}

void Runtime::global_applied(const std::string &key) {
    Slot *s = find(key);
    if (!s || s->state != UnitState::Pending) return;
    s->state = UnitState::Applied;
    log_.write(Level::Info, "FEATURE " + key + " applied");
}

void Runtime::global_failed(const std::string &key, const std::string &reason) {
    Slot *s = find(key);
    if (!s || s->state == UnitState::Failed) return;
    s->state = UnitState::Failed;
    s->reason = reason;
    log_.write(Level::Error, "FEATURE " + key + " failed: " + reason);
}

std::vector<UnitStatus> Runtime::statuses() const {
    std::vector<UnitStatus> out;
    for (const Slot &s : slots_) out.push_back({s.key, s.state, s.reason, s.instances.size()});
    return out;
}

bool Runtime::wants_frames() const {
    for (const Slot &s : slots_)
        if (!s.tracked.empty()) return true;
    return false;
}

}  // namespace ltm
