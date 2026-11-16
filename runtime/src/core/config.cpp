#include "config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace ltm {

const char *const kFeatureKeys[10] = {
    "dialogue_box", "cross_examination", "story_images", "text_log",
    "evidence_menu", "evidence_notification", "pause_when_unfocused", "save_menu",
    "gamepad_navigation", "selection_border",
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

std::string format_number(double value) {
    // Shortest representation that reads back as the same double, like repr().
    char buf[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, value);
        if (std::strtod(buf, nullptr) == value) break;
    }
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";  // n: nan/inf stay as they are
    return s;
}

ConfigUpdate config_update(const Settings &settings, const std::string &key) {
    if (key == "enabled") return {"general", key, settings.enabled ? "true" : "false"};
    if (key == "scale") return {"general", key, format_number(settings.scale)};
    if (key == "opacity") return {"general", key, format_number(settings.opacity)};
    return {"features", key, settings.feature(key) ? "true" : "false"};
}

std::string update_config(const std::string &text, const std::vector<ConfigUpdate> &updates) {
    if (updates.empty()) return text;
    std::string bom, body = text;
    if (body.size() >= 3 && body.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        bom = body.substr(0, 3);
        body = body.substr(3);
    }
    const std::string newline = body.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    // Lines with their endings.
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < body.size();) {
        size_t nl = body.find('\n', pos);
        size_t end = nl == std::string::npos ? body.size() : nl + 1;
        lines.push_back(body.substr(pos, end - pos));
        pos = end;
    }

    std::vector<bool> done(updates.size(), false);
    // Index of the last line that belongs to each section seen (header or key line).
    std::map<std::string, size_t> section_end;
    std::string section;
    bool in_section = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string content = lines[i];
        while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) content.pop_back();
        std::string ending = lines[i].substr(content.size());
        std::string code = trim(content);
        if (!code.empty() && code[0] == '[') {
            in_section = code.back() == ']';
            section = in_section ? lower(trim(code.substr(1, code.size() - 2))) : "";
            if (in_section) section_end[section] = i;
            continue;
        }
        if (!in_section || code.empty() || code[0] == ';' || code[0] == '#') continue;
        section_end[section] = i;
        size_t eq = content.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(content.substr(0, eq)));
        for (size_t u = 0; u < updates.size(); ++u) {
            if (lower(updates[u].section) != section || lower(updates[u].key) != key) continue;
            // Keep the indentation, key spelling, spacing around '=' and any inline comment.
            size_t value_start = eq + 1;
            while (value_start < content.size() && (content[value_start] == ' ' || content[value_start] == '\t'))
                ++value_start;
            std::string rest = content.substr(value_start);
            std::string value = strip_comment(rest);
            std::string comment = rest.substr(value.size());
            size_t kept = value.find_last_not_of(" \t");
            std::string trailing = kept == std::string::npos ? value : value.substr(kept + 1);
            lines[i] = content.substr(0, value_start) + updates[u].value + (comment.empty() ? "" : trailing + comment) + ending;
            done[u] = true;
        }
    }

    // Missing keys: at the end of their section, or in a new section at the end.
    std::vector<std::string> new_sections;
    std::map<std::string, std::vector<std::string>> appended;
    for (size_t u = 0; u < updates.size(); ++u) {
        if (done[u]) continue;
        std::string s = lower(updates[u].section);
        if (!appended.count(s) && !section_end.count(s)) new_sections.push_back(s);
        appended[s].push_back(updates[u].key + " = " + updates[u].value + newline);
    }
    // Insert from the bottom up so earlier indices stay valid.
    std::vector<std::pair<size_t, std::string>> inserts;
    for (auto &entry : appended) {
        auto it = section_end.find(entry.first);
        if (it == section_end.end()) continue;
        inserts.emplace_back(it->second, entry.first);
    }
    std::sort(inserts.begin(), inserts.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (auto &ins : inserts) {
        std::string &last = lines[ins.first];
        if (last.empty() || last.back() != '\n') last += newline;
        lines.insert(lines.begin() + static_cast<long>(ins.first) + 1, appended[ins.second].begin(),
                     appended[ins.second].end());
    }
    for (const std::string &s : new_sections) {
        if (!lines.empty() && lines.back().back() != '\n') lines.back() += newline;
        lines.push_back(newline);  // a blank line before the new section, as the installer does
        lines.push_back("[" + s + "]" + newline);
        for (const std::string &l : appended[s]) lines.push_back(l);
    }

    std::string out = bom;
    for (const std::string &l : lines) out += l;
    return out;
}

}  // namespace ltm
