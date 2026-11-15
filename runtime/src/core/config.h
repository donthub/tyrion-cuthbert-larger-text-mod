// Settings read from TyrionCuthbertLargerTextMod/config.ini at game start.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace ltm {

constexpr double kDefaultScale = 1.5;
constexpr double kMinScale = 1.1;
constexpr double kMaxScale = 2.0;
constexpr double kDefaultOpacity = 0.99;

// Config keys of the feature switches, in display order.
extern const char *const kFeatureKeys[8];

struct Settings {
    bool enabled = true;
    double scale = kDefaultScale;
    double opacity = kDefaultOpacity;
    bool debug = false;  // [general] debug: verbose log
    std::map<std::string, bool> features;  // every key of kFeatureKeys is present

    Settings();
    bool feature(const std::string &key) const;
};

struct ConfigResult {
    Settings settings;
    std::vector<std::string> warnings;
};

// Parses config text. Never fails: problems fall back to defaults and are
// reported as warnings (FR-RT-3).
ConfigResult parse_config(const std::string &text);

// One value to write into the config.
struct ConfigUpdate {
    std::string section, key, value;
};

// Returns `text` with the given values set. Everything else (comments, inline
// comments, unknown keys, BOM, line endings) is kept. A missing key is added at
// the end of its section, a missing section at the end of the file. Same rules
// as update_config_text in tool/larger_text_mod.py.
std::string update_config(const std::string &text, const std::vector<ConfigUpdate> &updates);

// A number the way the installer writes it (Python repr of a float: 1.5, 0.99, 2.0).
std::string format_number(double value);

// The config line for one setting: "general.scale", "features.text_log", ...
// `key` is "enabled", "scale", "opacity" or a feature key.
ConfigUpdate config_update(const Settings &settings, const std::string &key);

// Reads and writes the config file. The runtime writes it only when the player
// changes a setting in the game's Settings menu.
class ConfigStore {
  public:
    virtual ~ConfigStore() = default;
    // The file's text; false when it can't be read (missing counts as empty text).
    virtual bool read(std::string &text) = 0;
    // Replaces the file atomically. False on failure (the old file is kept).
    virtual bool write(const std::string &text) = 0;
};

}  // namespace ltm
