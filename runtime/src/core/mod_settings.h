// The controls of the mod's tab in the game's Settings menu, as pure data:
// which setting each control edits, its label, and its choices.
#pragma once
#include <string>
#include <vector>

#include "config.h"

namespace ltm {

struct ModControl {
    const char *key;    // "enabled", "scale", "opacity" or a feature key (config key)
    const char *label;  // shown in the tab
};

// The controls in display order: two per row, like the game's Gameplay tab.
const std::vector<ModControl> &mod_controls();

// The choices of one control's dropdown.
struct Choices {
    std::vector<std::string> labels;
    std::vector<double> values;  // switches: 1 = on, 0 = off
    int index_of(double value) const;  // the nearest choice
};

// The choices for `key`. A value in `current` that is not one of the usual
// steps (e.g. scale = 1.25 typed into config.ini) is added, so opening the tab
// never changes a setting by itself.
Choices mod_choices(const std::string &key, const Settings &current);

double mod_value(const std::string &key, const Settings &s);
void set_mod_value(const std::string &key, double value, Settings &s);

// The text shown below the controls.
extern const char *const kModTabNote;

}  // namespace ltm
