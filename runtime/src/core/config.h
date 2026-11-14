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
extern const char *const kFeatureKeys[7];

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

}  // namespace ltm
