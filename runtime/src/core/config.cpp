#include "config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>

namespace ltm {

const char *const kFeatureKeys[7] = {
    "dialogue_box", "cross_examination", "story_images", "text_log",
    "evidence_menu", "evidence_notification", "pause_when_unfocused",
};

Settings::Settings() {
    for (const char *key : kFeatureKeys) features[key] = true;
}

bool Settings::feature(const std::string &key) const {
    auto it = features.find(key);
    return it == features.end() ? true : it->second;
}

namespace {

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Removes an inline comment (" ;" or " #") from a value.
std::string strip_comment(const std::string &value) {
    for (size_t i = 0; i < value.size(); ++i) {
        if ((value[i] == ';' || value[i] == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1])))) {
            return value.substr(0, i);
        }
    }
    return value;
}

bool parse_bool(const std::string &raw, bool &out) {
    std::string v = lower(raw);
    if (v == "true" || v == "yes" || v == "on" || v == "1") { out = true; return true; }
    if (v == "false" || v == "no" || v == "off" || v == "0") { out = false; return true; }
    return false;
}

bool parse_number(const std::string &raw, double &out) {
    if (raw.empty()) return false;
    std::string v = raw;
    std::replace(v.begin(), v.end(), ',', '.');  // tolerate a decimal comma
    char *end = nullptr;
    errno = 0;
    double d = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == v.c_str() || *end != '\0' || !std::isfinite(d)) return false;
    out = d;
    return true;
}

}  // namespace

ConfigResult parse_config(const std::string &text) {
    ConfigResult result;
    Settings &s = result.settings;
    auto warn = [&](const std::string &w) { result.warnings.push_back(w); };

    std::set<std::string> feature_keys(std::begin(kFeatureKeys), std::end(kFeatureKeys));
    std::set<std::string> seen;
    std::string section;
    size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        if (line_no == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);  // UTF-8 BOM, e.g. from Notepad
        }
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            if (line.back() != ']') {
                warn("line " + std::to_string(line_no) + ": malformed section header ignored");
                section = "";
                continue;
            }
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn("line " + std::to_string(line_no) + ": expected 'key = value', ignored");
            continue;
        }
        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(strip_comment(line.substr(eq + 1)));
        std::string id = section + "." + key;
        if (seen.count(id)) warn("'" + key + "' appears more than once; the last value is used");
        seen.insert(id);

        if (section == "general" && key == "enabled") {
            bool b;
            if (parse_bool(value, b)) s.enabled = b;
            else warn("enabled: '" + value + "' is not true/false; using default (true)");
        } else if (section == "general" && key == "debug") {
            bool b;
            if (parse_bool(value, b)) s.debug = b;
            else warn("debug: '" + value + "' is not true/false; using default (false)");
        } else if (section == "general" && key == "scale") {
            double d;
            if (!parse_number(value, d)) {
                warn("scale: '" + value + "' is not a number; using default (1.5)");
                s.scale = kDefaultScale;
            } else if (d < kMinScale || d > kMaxScale) {
                warn("scale: " + value + " is outside 1.1-2.0; using default (1.5)");
                s.scale = kDefaultScale;
            } else {
                s.scale = d;
            }
        } else if (section == "general" && key == "opacity") {
            double d;
            if (!parse_number(value, d)) {
                warn("opacity: '" + value + "' is not a number; using default (0.99)");
                s.opacity = kDefaultOpacity;
            } else if (d < 0.0 || d > 1.0) {
                warn("opacity: " + value + " is outside 0.0-1.0; using default (0.99)");
                s.opacity = kDefaultOpacity;
            } else {
                s.opacity = d;
            }
        } else if (section == "features" && feature_keys.count(key)) {
            bool b;
            if (parse_bool(value, b)) s.features[key] = b;
            else warn(key + ": '" + value + "' is not true/false; using default (true)");
        }
        // Anything else is an unknown key: kept in the file, ignored here.
    }
    if (!seen.count("general.scale")) warn("scale is missing; using default (1.5)");
    if (!seen.count("general.opacity")) warn("opacity is missing; using default (0.99)");
    return result;
}

}  // namespace ltm
