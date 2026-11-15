// Applies feature units to UI instances: once per instance, validated first,
// rolled back on failure, and with failures confined to the affected unit.
#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "features.h"
#include "ui.h"

namespace ltm {

enum class Level { Debug, Info, Warn, Error };

class Log {
  public:
    virtual ~Log() = default;
    virtual void write(Level level, const std::string &message) = 0;
};

enum class UnitState { Pending, Applied, Disabled, Skipped, Failed };

struct UnitStatus {
    std::string key;
    UnitState state;
    std::string reason;
    size_t instances;
};

class Runtime {
  public:
    Runtime(const Settings &settings, Log &log);
    Runtime(const Settings &settings, Log &log, std::vector<std::unique_ptr<Unit>> units);

    const Settings &settings() const { return settings_; }

    // Writes "FEATURE <key> disabled" for every unit switched off in the config.
    void report_disabled();
    // A game controller announced a (possibly new) instance.
    void on_instance(Ui &ui, Target target, Handle root);
    // Called once per frame before the canvas layout pass.
    void on_frame(Ui &ui);

    // The hooks for a target could not be installed: its units can't run.
    void target_unavailable(Target target, const std::string &reason);

    // Units that are not tied to UI instances (e.g. focus handling).
    bool global_enabled(const std::string &key) const;
    void global_applied(const std::string &key);
    void global_failed(const std::string &key, const std::string &reason);

    // Where the in-game settings tab saves the config.
    void set_config_store(ConfigStore *store) { shared_.config = store; }

    std::vector<UnitStatus> statuses() const;
    // True while any unit wants per-frame maintenance.
    bool wants_frames() const;

  private:
    struct Slot {
        std::unique_ptr<Unit> unit;  // null for global units
        std::string key, config_key;
        UnitState state = UnitState::Pending;
        std::string reason;
        std::set<int> instances;
        std::vector<Handle> tracked;
    };
    Slot *find(const std::string &key);
    const Slot *find(const std::string &key) const;
    void apply_to(Ui &ui, Slot &slot, Handle root);
    void disable(Ui &ui, Slot &slot, UnitState state, const std::string &reason);
    Context context();

    Settings settings_;
    Log &log_;
    Shared shared_;
    std::vector<Slot> slots_;
    bool text_log_open_ = false;
};

}  // namespace ltm
