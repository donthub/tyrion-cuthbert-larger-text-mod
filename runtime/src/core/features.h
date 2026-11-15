// The feature units: independently applied groups of UI changes.
#pragma once
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "layout.h"
#include "tx.h"
#include "ui.h"

namespace ltm {

// Thrown by Unit::validate for an instance that already carries the changes
// (e.g. a copy of a modified object). The instance is left as it is.
struct AlreadyApplied {};

// Which game controller announced a new UI instance.
enum class Target { DialogueBox, TextLog, ItemMenu, Notification, ImageDisplay, SettingsMenu };
constexpr int kTargetCount = 6;

// State shared between units.
struct Shared {
    // Original geometry of the dialogue and narrator panels, once seen.
    std::optional<layout::DialoguePanel> dialogue_panel, narrator_panel;
    // Frames left in which a just-closed text log may have blanked the dialogue.
    int text_log_closed_frames = 0;
    // Whether the panels are being enlarged (dialogue_box switched on and working).
    bool panels_enlarged = true;
    // Where the in-game settings tab saves to; nullptr: nowhere.
    ConfigStore *config = nullptr;
};

struct Context {
    const Settings &settings;
    Shared &shared;
};

class Unit {
  public:
    Unit(const char *key, const char *config_key, Target target)
        : key_(key), config_key_(config_key), target_(target) {}
    virtual ~Unit() = default;

    const char *key() const { return key_; }
    const char *config_key() const { return config_key_; }
    Target target() const { return target_; }

    // Whether this unit is responsible for the given instance.
    virtual bool matches(Ui &ui, Handle root) = 0;
    // Checks the structure the unit relies on and records the original values
    // (from the first instance). Throws Mismatch. Must not change anything.
    virtual void validate(Ui &ui, Handle root) = 0;
    // Applies the changes through the transaction. Values are absolute and
    // computed from the recorded originals, so applying twice never compounds.
    virtual void apply(Tx &tx, Handle root, const Context &ctx) = 0;
    // Runs even with the master switch off and has no switch of its own (the
    // settings tab, so the mod can be switched back on in the game).
    virtual bool always_on() const { return false; }
    // Per-frame maintenance for instances that need it (text overflow etc.).
    virtual bool wants_tick() const { return false; }
    virtual void tick(Ui &ui, Handle root, const Context &ctx) { (void)ui, (void)root, (void)ctx; }
  private:
    const char *key_;
    const char *config_key_;
    Target target_;
};

// Creates every unit, in application order.
std::vector<std::unique_ptr<Unit>> make_units();

// Helpers shared with tests.
layout::TmpSize read_tmp(Ui &ui, Handle go);
void write_tmp(Tx &tx, Handle go, const layout::TmpSize &size);

}  // namespace ltm
