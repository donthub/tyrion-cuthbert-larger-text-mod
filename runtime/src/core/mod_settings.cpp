#include "mod_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ltm {

const std::vector<ModControl> &mod_controls() {
    static const std::vector<ModControl> controls = {
        {"enabled", "Mod"},
        {"scale", "Text Size"},
        {"opacity", "Box Opacity"},
        {"dialogue_box", "Dialogue Box"},
        {"cross_examination", "Cross-Examination"},
        {"story_images", "Story Images"},
        {"text_log", "Text Log"},
        {"evidence_menu", "Evidence Menu"},
        {"evidence_notification", "Evidence Popup"},
        {"pause_when_unfocused", "Pause in Background"},
        {"save_menu", "Save Menu"},
    };
    return controls;
}

const char *const kModTabNote = "Changes take effect the next time the game starts.";

int Choices::index_of(double value) const {
    int best = 0;
    for (size_t i = 1; i < values.size(); ++i) {
        if (std::fabs(values[i] - value) < std::fabs(values[static_cast<size_t>(best)] - value)) best = static_cast<int>(i);
    }
    return best;
}

namespace {

std::string scale_label(double v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    if (s.back() == '0') s.pop_back();  // 1.50 -> 1.5, keep 1.25
    return s + "x";
}

std::string percent_label(double v) {
    char buf[16];
    double p = v * 100.0;
    if (std::fabs(p - std::round(p)) < 1e-6) std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(p)));
    else std::snprintf(buf, sizeof(buf), "%.1f%%", p);
    return buf;
}

Choices steps(std::vector<double> values, double current, std::string (*label)(double)) {
    bool listed = false;
    for (double v : values) listed = listed || std::fabs(v - current) < 1e-9;
    if (!listed) {
        values.push_back(current);
        std::sort(values.begin(), values.end());
    }
    Choices c;
    for (double v : values) {
        c.values.push_back(v);
        c.labels.push_back(label(v));
    }
    return c;
}

}  // namespace

Choices mod_choices(const std::string &key, const Settings &current) {
    if (key == "scale") {
        std::vector<double> v;
        for (int i = 11; i <= 20; ++i) v.push_back(i / 10.0);
        return steps(v, current.scale, scale_label);
    }
    if (key == "opacity") {
        return steps({0.0, 0.2, 0.4, 0.6, 0.8, 0.9, 0.95, 0.99, 1.0}, current.opacity, percent_label);
    }
    return Choices{{"On", "Off"}, {1.0, 0.0}};
}

double mod_value(const std::string &key, const Settings &s) {
    if (key == "enabled") return s.enabled ? 1.0 : 0.0;
    if (key == "scale") return s.scale;
    if (key == "opacity") return s.opacity;
    return s.feature(key) ? 1.0 : 0.0;
}

void set_mod_value(const std::string &key, double value, Settings &s) {
    if (key == "enabled") s.enabled = value != 0.0;
    else if (key == "scale") s.scale = std::min(kMaxScale, std::max(kMinScale, value));
    else if (key == "opacity") s.opacity = std::min(1.0, std::max(0.0, value));
    else s.features[key] = value != 0.0;
}

}  // namespace ltm
