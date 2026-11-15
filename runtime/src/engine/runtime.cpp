// Runtime start-up and method hooks.
#include "runtime.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../../third_party/minhook/include/MinHook.h"
#include "../core/config.h"
#include "../core/framework.h"
#include "il2cpp_api.h"
#include "unity.h"
#include "unity_ui.h"

#ifndef LTM_VERSION
#define LTM_VERSION "0.0.0-dev"
#endif

namespace ltm {
namespace {

using engine::invoke;
using engine::method;
using il2cpp::Class;
using il2cpp::MethodInfo;
using il2cpp::Object;

// --- log --------------------------------------------------------------------------

class FileLog : public Log {
  public:
    bool open(const std::wstring &path) {
        // One previous log is kept; the log never grows across sessions (FR-RT-8).
        std::wstring previous = path + L".1";
        DeleteFileW(previous.c_str());
        MoveFileW(path.c_str(), previous.c_str());
        file_ = _wfopen(path.c_str(), L"wb");
        return file_ != nullptr;
    }
    void set_debug(bool debug) { debug_ = debug; }
    void write(Level level, const std::string &message) override {
        if (!file_ || (level == Level::Debug && !debug_)) return;
        // Cap the size within a session too.
        if (written_ > 4 * 1024 * 1024) return;
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &now);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
        static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        int n = std::fprintf(file_, "%s %s %s\n", stamp, names[static_cast<int>(level)], message.c_str());
        if (n > 0) written_ += static_cast<size_t>(n);
        std::fflush(file_);
    }

  private:
    FILE *file_ = nullptr;
    bool debug_ = false;
    size_t written_ = 0;
};

FileLog g_log;
std::unique_ptr<Runtime> g_runtime;
std::unique_ptr<engine::UnityClasses> g_classes;
std::unique_ptr<engine::UnityUi> g_ui;
bool g_busy = false;
uint64_t g_frames = 0;

std::string narrow(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

bool read_file(const std::wstring &path, std::string &out) {
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0 && out.size() < 1024 * 1024) out.append(buffer, n);
    std::fclose(f);
    return true;
}

// Runs a callback from a hook: on the main thread, never re-entrant, and no
// error ever propagates into the game (FR-RT-7).
template <typename F>
void guarded(F &&f) {
    if (g_busy || !g_runtime || !g_ui) return;
    g_busy = true;
    try {
        f();
    } catch (const std::exception &e) {
        g_log.write(Level::Error, std::string("unexpected error in hook: ") + e.what());
    } catch (...) {
        g_log.write(Level::Error, "unexpected error in hook");
    }
    try {
        g_ui->end_call();
    } catch (...) {
    }
    g_busy = false;
}

// --- hooks --------------------------------------------------------------------

// Detour shapes, by managed signature. IL2CPP passes the MethodInfo last.
enum class Shape { Instance0, InstanceStr1, InstanceRet0, InstanceRetStr2, InstanceRetStr3, Static0, InstanceInt1 };

using Handler = std::function<void(Object *self)>;
// Returns true to skip the original method (and the other handlers) for `self`.
using Skip = std::function<bool(Object *self)>;

struct Slot {
    void *target = nullptr;
    void *original = nullptr;
    Shape shape = Shape::Instance0;
    std::vector<Handler> prefix, postfix;
    std::vector<Skip> skip;
};

constexpr int kSlots = 16;
std::array<Slot, kSlots> g_slots;
int g_slot_count = 0;

void run(std::vector<Handler> &handlers, Object *self) {
    if (handlers.empty()) return;
    guarded([&] {
        for (auto &h : handlers) h(self);
    });
}

// Never throws; a failing check doesn't skip.
bool skipped(Slot &s, Object *self) {
    if (s.skip.empty() || !self || !g_ui) return false;
    bool result = false;
    try {
        for (auto &f : s.skip)
            if (f(self)) {
                result = true;
                break;
            }
    } catch (...) {
        result = false;
    }
    if (!g_busy) {
        try {
            g_ui->end_call();
        } catch (...) {
        }
    }
    return result;
}

using String = il2cpp::String;

template <int N>
void d_instance0(Object *self, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    if (skipped(s, self)) return;
    run(s.prefix, self);
    reinterpret_cast<void (*)(Object *, const MethodInfo *)>(s.original)(self, mi);
    run(s.postfix, self);
}
template <int N>
void d_instance_str1(Object *self, String *a, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    run(s.prefix, self);
    reinterpret_cast<void (*)(Object *, String *, const MethodInfo *)>(s.original)(self, a, mi);
    run(s.postfix, self);
}
template <int N>
Object *d_instance_ret0(Object *self, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    run(s.prefix, self);
    Object *r = reinterpret_cast<Object *(*)(Object *, const MethodInfo *)>(s.original)(self, mi);
    run(s.postfix, self);
    return r;
}
template <int N>
Object *d_instance_ret_str2(Object *self, String *a, String *b, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    run(s.prefix, self);
    Object *r = reinterpret_cast<Object *(*)(Object *, String *, String *, const MethodInfo *)>(s.original)(self, a, b, mi);
    run(s.postfix, self);
    return r;
}
template <int N>
Object *d_instance_ret_str3(Object *self, String *a, String *b, String *c, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    run(s.prefix, self);
    Object *r = reinterpret_cast<Object *(*)(Object *, String *, String *, String *, const MethodInfo *)>(s.original)(
        self, a, b, c, mi);
    run(s.postfix, self);
    return r;
}
template <int N>
void d_instance_int1(Object *self, int32_t a, const MethodInfo *mi) {
    Slot &s = g_slots[N];
    if (skipped(s, self)) return;
    run(s.prefix, self);
    reinterpret_cast<void (*)(Object *, int32_t, const MethodInfo *)>(s.original)(self, a, mi);
    run(s.postfix, self);
}
template <int N>
void d_static0(const MethodInfo *mi) {
    Slot &s = g_slots[N];
    run(s.prefix, nullptr);
    reinterpret_cast<void (*)(const MethodInfo *)>(s.original)(mi);
    run(s.postfix, nullptr);
}
template <int... I>
void *detour_for(Shape shape, int n, std::integer_sequence<int, I...>) {
    static void *const tables[][kSlots] = {
        {reinterpret_cast<void *>(&d_instance0<I>)...},
        {reinterpret_cast<void *>(&d_instance_str1<I>)...},
        {reinterpret_cast<void *>(&d_instance_ret0<I>)...},
        {reinterpret_cast<void *>(&d_instance_ret_str2<I>)...},
        {reinterpret_cast<void *>(&d_instance_ret_str3<I>)...},
        {reinterpret_cast<void *>(&d_static0<I>)...},
        {reinterpret_cast<void *>(&d_instance_int1<I>)...},
    };
    return tables[static_cast<int>(shape)][n];
}

// Hooks a managed method. The handler runs before (prefix) or after the
// original; a skip check can suppress the original for some objects (only
// Instance0 and InstanceInt1 hooks check it). Methods that the compiler folded
// into one body share a slot.
bool hook(const MethodInfo *m, Shape shape, bool prefix, Handler handler, std::string &error, Skip skip = nullptr) {
    void *target = engine::method_pointer(m);
    if (!target) {
        error = "method has no code";
        return false;
    }
    for (int i = 0; i < g_slot_count; ++i) {
        Slot &s = g_slots[i];
        if (s.target != target) continue;
        if (s.shape != shape) {
            error = "method body shared with an incompatible method";
            return false;
        }
        if (handler) (prefix ? s.prefix : s.postfix).push_back(std::move(handler));
        if (skip) s.skip.push_back(std::move(skip));
        return true;
    }
    if (g_slot_count == kSlots) {
        error = "too many hooks";
        return false;
    }
    int n = g_slot_count;
    Slot &s = g_slots[n];
    s.target = target;
    s.shape = shape;
    if (handler) (prefix ? s.prefix : s.postfix).push_back(std::move(handler));
    if (skip) s.skip.push_back(std::move(skip));
    void *detour = detour_for(shape, n, std::make_integer_sequence<int, kSlots>{});
    MH_STATUS st = MH_CreateHook(target, detour, &s.original);
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        error = std::string("MinHook: ") + MH_StatusToString(st);
        s = Slot{};
        return false;
    }
    ++g_slot_count;
    return true;
}

// --- per-target handlers -----------------------------------------------------------

Comp comp_of(Target t) {
    switch (t) {
        case Target::DialogueBox: return Comp::DialogueBox;
        case Target::TextLog: return Comp::TextLogGUI;
        case Target::ItemMenu: return Comp::ItemMenu;
        case Target::Notification: return Comp::AddItemDisplay;
        case Target::ImageDisplay: return Comp::ImageDisplay;
        case Target::SettingsMenu: return Comp::SettingsMenu;
    }
    return Comp::Count;
}

// `self` is a component on the instance's root object. Folded method bodies or
// base-class hooks can be reached by other classes, so filter by component.
Handler announce(Target target) {
    return [target](Object *self) {
        if (!self) return;
        g_log.write(Level::Debug, "hook: " + engine::class_name(self) + " -> target " + std::to_string(static_cast<int>(target)));
        Handle go = g_ui->game_object_of(self);
        if (!go || !g_ui->has(go, comp_of(target))) return;
        g_log.write(Level::Debug, "hook: announcing " + g_ui->name(go));
        g_runtime->on_instance(*g_ui, target, go);
        g_log.write(Level::Debug, "hook: done");
    };
}

// --- debug command file -----------------------------------------------------------
// Maintainer aid for in-game verification: with `[debug] commands = true`,
// lines in TyrionCuthbertLargerTextMod/debug-commands.txt are executed on the
// main thread and the file is deleted. One command per line:
//   call|Namespace.Class.Method|string arg|...
// Static methods are called directly; instance methods on the class's static
// `instance` field. Missing reference arguments are passed as null.
std::wstring g_command_file;
bool g_commands = false;

void dump_classes(const std::string &config_text);

bool debug_commands_enabled(const std::string &config_text) {
    std::istringstream in(config_text);
    std::string section;
    for (std::string line; std::getline(in, line);) {
        std::string flat;
        for (char c : line)
            if (c != ' ' && c != '\t' && c != '\r') flat += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!flat.empty() && flat[0] == '[') section = flat;
        else if (section == "[debug]" && flat == "commands=true") return true;
    }
    return false;
}

void run_command(const std::string &line) {
    std::vector<std::string> parts;
    std::istringstream in(line);
    for (std::string p; std::getline(in, p, '|');) parts.push_back(p);
    if (parts.size() == 2 && parts[0] == "dump") {
        dump_classes("[debug]\ndump_classes = " + parts[1] + "\n");
        return;
    }
    if (parts.size() == 1 && parts[0] == "background") {
        // Keep the game running while unfocused, for unattended checks.
        auto set = reinterpret_cast<void (*)(bool)>(
            il2cpp::il2cpp_resolve_icall("UnityEngine.Application::set_runInBackground(System.Boolean)"));
        if (!set) throw EngineError("runInBackground icall unavailable");
        set(true);
        g_log.write(Level::Info, "debug: running in background");
        return;
    }
    if (parts.size() >= 4 && parts[0] == "callf") {
        // callf|Namespace.Class|field|Method|args...: calls a method on a field's object.
        size_t dot = parts[1].rfind('.');
        Class *k = engine::find_class(parts[1].substr(0, dot).c_str(), parts[1].substr(dot + 1).c_str());
        if (!k) throw EngineError("class not found");
        Class *object = engine::find_class("UnityEngine", "Object");
        Object *owner = invoke(method(object, "FindFirstObjectByType", 1, {"System.Type"}), nullptr, {engine::type_object(k)});
        if (!owner) throw EngineError("no live instance");
        il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(k, parts[2].c_str());
        if (!f) throw EngineError("field not found");
        Object *target = nullptr;
        il2cpp::il2cpp_field_get_value(owner, f, &target);
        if (!target) throw EngineError("field is null");
        size_t given = parts.size() - 4;
        const MethodInfo *m = engine::find_method(il2cpp::il2cpp_object_get_class(target), parts[3].c_str(), static_cast<int>(given));
        if (!m) throw EngineError("method not found");
        void *args[8] = {};
        float floats[8] = {};
        bool bools[8] = {};
        int32_t ints[8] = {};
        for (size_t i = 0; i < given && i < 8; ++i) {
            char *t = il2cpp::il2cpp_type_get_name(il2cpp::il2cpp_method_get_param(m, static_cast<uint32_t>(i)));
            std::string type = t ? t : "";
            if (t) il2cpp::il2cpp_free(t);
            const std::string &v = parts[i + 4];
            if (type == "System.Single") { floats[i] = static_cast<float>(std::atof(v.c_str())); args[i] = &floats[i]; }
            else if (type == "System.Boolean") { bools[i] = v == "true"; args[i] = &bools[i]; }
            else if (type == "System.Int32") { ints[i] = std::atoi(v.c_str()); args[i] = &ints[i]; }
            else args[i] = engine::to_string(v);
        }
        il2cpp::Exception *exc = nullptr;
        il2cpp::il2cpp_runtime_invoke(m, target, args, &exc);
        g_log.write(Level::Info, "debug: " + parts[2] + "." + parts[3] + (exc ? " threw" : " called"));
        return;
    }
    if (parts.size() == 3 && parts[0] == "getstatic") {
        // getstatic|Namespace.Class|field: logs a static string field.
        size_t dot = parts[1].rfind('.');
        Class *k = engine::find_class(parts[1].substr(0, dot).c_str(), parts[1].substr(dot + 1).c_str());
        if (!k) throw EngineError("class not found");
        il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(k, parts[2].c_str());
        if (!f) throw EngineError("field not found");
        String *value = nullptr;
        il2cpp::il2cpp_field_static_get_value(f, &value);
        g_log.write(Level::Info, "debug: " + parts[1] + "." + parts[2] + " = \"" + engine::to_utf8(value) + "\"");
        return;
    }
    if (parts.size() == 3 && parts[0] == "get") {
        // get|Namespace.Class|field.field...: logs a value on the first live instance.
        size_t dot = parts[1].rfind('.');
        std::string ns = dot == std::string::npos ? "" : parts[1].substr(0, dot);
        std::string cls = dot == std::string::npos ? parts[1] : parts[1].substr(dot + 1);
        Class *k = engine::find_class(ns.c_str(), cls.c_str());
        if (!k) throw EngineError("class not found");
        Class *object = engine::find_class("UnityEngine", "Object");
        Object *cur = invoke(method(object, "FindFirstObjectByType", 1, {"System.Type"}), nullptr, {engine::type_object(k)});
        std::istringstream path(parts[2]);
        std::string out;
        for (std::string name; std::getline(path, name, '.');) {
            if (!cur) throw EngineError("null before " + name);
            il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(il2cpp::il2cpp_object_get_class(cur), name.c_str());
            if (!f) throw EngineError("field " + name + " not found");
            char *t = il2cpp::il2cpp_type_get_name(il2cpp::il2cpp_field_get_type(f));
            std::string type = t ? t : "";
            if (t) il2cpp::il2cpp_free(t);
            uint8_t buf[16] = {};
            il2cpp::il2cpp_field_get_value(cur, f, buf);
            if (type == "System.Boolean") out = buf[0] ? "true" : "false";
            else if (type == "System.Single") out = std::to_string(*reinterpret_cast<float *>(buf));
            else if (type == "System.Int32") out = std::to_string(*reinterpret_cast<int32_t *>(buf));
            else if (type == "System.String") out = "\"" + engine::to_utf8(*reinterpret_cast<String **>(buf)) + "\"";
            else { cur = *reinterpret_cast<Object **>(buf); out = type; continue; }
            cur = nullptr;
        }
        g_log.write(Level::Info, "debug: " + parts[1] + "." + parts[2] + " = " + out);
        return;
    }
    if (parts.size() == 2 && parts[0] == "press") {
        // press|Label: clicks the active Button whose text is Label.
        Class *object = engine::find_class("UnityEngine", "Object");
        Class *component = engine::find_class("UnityEngine", "Component");
        Class *btn = engine::find_class("UnityEngine.UI", "Button");
        Class *tmp = engine::find_class("TMPro", "TMP_Text");
        Class *behaviour = engine::find_class("UnityEngine", "Behaviour");
        int32_t sort = 0;
        Object *array = invoke(method(object, "FindObjectsByType", 2, {"System.Type", "UnityEngine.FindObjectsSortMode"}),
                               nullptr, {engine::type_object(btn), &sort});
        if (!array) throw EngineError("no buttons");
        // Il2CppArray: header (klass, monitor, bounds, max_length), then elements.
        auto *raw = reinterpret_cast<uint8_t *>(array);
        size_t count = *reinterpret_cast<uintptr_t *>(raw + 3 * sizeof(void *));
        Object **items = reinterpret_cast<Object **>(raw + 4 * sizeof(void *));
        for (size_t i = 0; i < count; ++i) {
            Object *b = items[i];
            if (!engine::unbox<bool>(invoke(method(behaviour, "get_isActiveAndEnabled", 0), b))) continue;
            bool inactive = false;
            Object *label = invoke(method(component, "GetComponentInChildren", 2, {"System.Type", "System.Boolean"}), b,
                                   {engine::type_object(tmp), &inactive});
            if (!label) continue;
            std::string text = engine::to_utf8(reinterpret_cast<String *>(invoke(method(tmp, "get_text", 0), label)));
            if (text != parts[1]) continue;
            Object *event = invoke(method(btn, "get_onClick", 0), b);
            invoke(method(il2cpp::il2cpp_object_get_class(event), "Invoke", 0), event);
            g_log.write(Level::Info, "debug: pressed '" + parts[1] + "'");
            return;
        }
        throw EngineError("no active button labelled '" + parts[1] + "'");
    }
    if (parts.size() == 3 && parts[0] == "click") {
        // click|Namespace.Class|buttonField: invokes that Button's onClick.
        size_t dot = parts[1].rfind('.');
        std::string ns = dot == std::string::npos ? "" : parts[1].substr(0, dot);
        std::string cls = dot == std::string::npos ? parts[1] : parts[1].substr(dot + 1);
        Class *k = engine::find_class(ns.c_str(), cls.c_str());
        if (!k) throw EngineError("class not found");
        Class *object = engine::find_class("UnityEngine", "Object");
        Object *self = invoke(method(object, "FindFirstObjectByType", 1, {"System.Type"}), nullptr, {engine::type_object(k)});
        if (!self) throw EngineError("no live instance");
        il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(k, parts[2].c_str());
        if (!f) throw EngineError("field not found");
        Object *button = nullptr;
        il2cpp::il2cpp_field_get_value(self, f, &button);
        if (!button) throw EngineError("field is null");
        if (engine::class_name(button) == "UnityEngine.GameObject") {
            Class *go = engine::find_class("UnityEngine", "GameObject");
            Class *btn = engine::find_class("UnityEngine.UI", "Button");
            button = invoke(method(go, "GetComponent", 1, {"System.Type"}), button, {engine::type_object(btn)});
            if (!button) throw EngineError("object has no Button");
        }
        Object *event = invoke(method(il2cpp::il2cpp_object_get_class(button), "get_onClick", 0), button);
        invoke(method(il2cpp::il2cpp_object_get_class(event), "Invoke", 0), event);
        g_log.write(Level::Info, "debug: clicked " + parts[1] + "." + parts[2]);
        return;
    }
    if (parts.size() < 2 || parts[0] != "call") {
        g_log.write(Level::Info, "debug: ignored '" + line + "'");
        return;
    }
    const std::string &target = parts[1];
    size_t m_dot = target.rfind('.');
    if (m_dot == std::string::npos || m_dot == 0) throw EngineError("expected Namespace.Class.Method");
    size_t c_dot = target.rfind('.', m_dot - 1);
    size_t cls_start = c_dot == std::string::npos ? 0 : c_dot + 1;
    std::string ns = c_dot == std::string::npos ? "" : target.substr(0, c_dot);
    std::string cls = target.substr(cls_start, m_dot - cls_start);
    std::string name = target.substr(m_dot + 1);
    Class *k = engine::find_class(ns.c_str(), cls.c_str());
    if (!k) throw EngineError("class " + ns + "." + cls + " not found");
    size_t given = parts.size() - 2;
    const MethodInfo *m = nullptr;
    for (int argc = static_cast<int>(given); argc <= static_cast<int>(given) + 3 && !m; ++argc)
        m = engine::find_method(k, name.c_str(), argc);
    if (!m) throw EngineError("method " + name + " not found");
    uint32_t iflags = 0;
    bool is_static = (il2cpp::il2cpp_method_get_flags(m, &iflags) & 0x10) != 0;
    Object *self = nullptr;
    if (!is_static) {
        il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(k, "instance");
        if (f) il2cpp::il2cpp_field_static_get_value(f, &self);
        if (!self) {
            // No singleton: use the first live object of the class.
            Class *object = engine::find_class("UnityEngine", "Object");
            const MethodInfo *find = engine::find_method(object, "FindFirstObjectByType", 1, {"System.Type"});
            if (!find) find = engine::find_method(object, "FindObjectOfType", 1, {"System.Type"});
            if (!find) throw EngineError("no instance and no FindObjectOfType");
            self = invoke(find, nullptr, {engine::type_object(k)});
        }
        if (!self) throw EngineError("no live instance");
    }
    void *args[8] = {};
    int32_t ints[8] = {};
    bool bools[8] = {};
    for (size_t i = 0; i < given && i < 8; ++i) {
        char *t = il2cpp::il2cpp_type_get_name(il2cpp::il2cpp_method_get_param(m, static_cast<uint32_t>(i)));
        std::string type = t ? t : "";
        if (t) il2cpp::il2cpp_free(t);
        if (type == "System.Int32") {
            ints[i] = std::atoi(parts[i + 2].c_str());
            args[i] = &ints[i];
        } else if (type == "System.Boolean") {
            bools[i] = parts[i + 2] == "true";
            args[i] = &bools[i];
        } else {
            args[i] = engine::to_string(parts[i + 2]);
        }
    }
    il2cpp::Exception *exc = nullptr;
    il2cpp::il2cpp_runtime_invoke(m, self, args, &exc);
    g_log.write(Level::Info, "debug: " + target + (exc ? " threw " + engine::class_name(reinterpret_cast<Object *>(exc)) : " called"));
}

void poll_commands() {
    if (GetFileAttributesW(g_command_file.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::string text;
    read_file(g_command_file, text);
    DeleteFileW(g_command_file.c_str());
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        try {
            run_command(line);
        } catch (const std::exception &e) {
            g_log.write(Level::Info, "debug: '" + line + "' failed: " + e.what());
        }
    }
}

// --- focus (FR-UI-36/37) -------------------------------------------------------------

using set_bool_icall = void (*)(bool);
set_bool_icall g_set_run_in_background = nullptr;
const MethodInfo *g_get_run_in_background = nullptr;
bool g_focus_reapplied_logged = false;

void enforce_pause_when_unfocused() {
    if (!g_runtime->global_enabled("pause_when_unfocused")) return;
    try {
        if (!g_set_run_in_background) {
            // The managed setter is stripped from this build; the engine's
            // internal call is always registered.
            g_set_run_in_background = reinterpret_cast<set_bool_icall>(
                il2cpp::il2cpp_resolve_icall("UnityEngine.Application::set_runInBackground(System.Boolean)"));
            if (!g_set_run_in_background) {
                g_set_run_in_background = reinterpret_cast<set_bool_icall>(
                    il2cpp::il2cpp_resolve_icall("UnityEngine.Application::set_runInBackground"));
            }
            Class *app = engine::find_class("UnityEngine", "Application");
            g_get_run_in_background = app ? engine::find_method(app, "get_runInBackground", 0) : nullptr;
            if (!g_set_run_in_background || !g_get_run_in_background) {
                g_runtime->global_failed("pause_when_unfocused", "Application.runInBackground is unavailable");
                return;
            }
        }
        bool running = engine::unbox<bool>(invoke(g_get_run_in_background, nullptr));
        if (!running) {
            g_runtime->global_applied("pause_when_unfocused");
            return;
        }
        g_set_run_in_background(false);
        if (engine::unbox<bool>(invoke(g_get_run_in_background, nullptr))) {
            g_runtime->global_failed("pause_when_unfocused", "the engine ignored runInBackground = false");
            return;
        }
        if (g_runtime->statuses().back().state == UnitState::Applied && !g_focus_reapplied_logged) {
            g_focus_reapplied_logged = true;
            g_log.write(Level::Info, "the game turned run-in-background on again; turned it off");
        }
        g_runtime->global_applied("pause_when_unfocused");
    } catch (const std::exception &e) {
        g_runtime->global_failed("pause_when_unfocused", e.what());
    }
}

void log_versions() {
    try {
        Class *app = engine::find_class("UnityEngine", "Application");
        std::string game = engine::to_utf8(reinterpret_cast<String *>(invoke(method(app, "get_version", 0), nullptr)));
        std::string unity =
            engine::to_utf8(reinterpret_cast<String *>(invoke(method(app, "get_unityVersion", 0), nullptr)));
        g_log.write(Level::Info, "game version " + game + ", Unity " + unity);
    } catch (const std::exception &e) {
        g_log.write(Level::Warn, std::string("could not read the game version: ") + e.what());
    }
}

void on_frame(Object *) {
    ++g_frames;
    if (g_frames == 1) {
        log_versions();
        enforce_pause_when_unfocused();
    } else if (g_frames % 300 == 0) {
        enforce_pause_when_unfocused();  // cheap; catches the game turning it back on
    }
    if (g_runtime->wants_frames()) g_runtime->on_frame(*g_ui);
    if (g_commands && g_frames % 30 == 0) poll_commands();
}

struct HookSpec {
    Target target;
    const char *ns, *cls, *name;
    int argc;
    Shape shape;
    bool prefix;
};

// Each target is announced by its controller's Awake (runs when the prefab
// is instantiated, before the first frame shows it) and by later entry points
// as a fallback.
const HookSpec kHooks[] = {
    {Target::DialogueBox, "VisualNovel", "DialogueBox", "Awake", 0, Shape::Instance0, false},
    {Target::TextLog, "VisualNovel", "TextLogGUI", "Awake", 0, Shape::Instance0, false},
    {Target::ItemMenu, "Courtroom", "ItemMenu", "Awake", 0, Shape::Instance0, false},
    {Target::Notification, "UIBase", "AnimatedGUIObject", "Awake", 0, Shape::Instance0, false},
    {Target::Notification, "Courtroom", "AddItemDisplay", "YarnAddItem", 2, Shape::InstanceRetStr2, true},
    {Target::Notification, "Courtroom", "AddItemDisplay", "YarnUpdateItem", 3, Shape::InstanceRetStr3, true},
    {Target::ImageDisplay, "VisualNovel", "ImageDisplay", "SetImage", 1, Shape::InstanceStr1, true},
    {Target::ImageDisplay, "UIBase", "GUIObject", "Show", 0, Shape::InstanceRet0, true},
    // Before Awake: Awake reopens the tab shown last (SettingsMenu.lastTab), which
    // may be the mod's tab, so it must already be registered.
    {Target::SettingsMenu, "UIBase", "SettingsMenu", "Awake", 0, Shape::Instance0, true},
};

// SettingsMenu.lastTab is static: the tab shown last in this session, reopened
// by the next menu's Awake. If that tab is not in this menu (the mod's tab when
// it could not be added), the game's Awake fails and the menu stays broken, so
// fall back to the first tab. Runs before Awake, after the tab was added.
void forget_missing_last_tab(Object *self) {
    if (!self || engine::class_name(self) != "UIBase.SettingsMenu") return;
    il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_field_from_name(il2cpp::il2cpp_object_get_class(self), "lastTab");
    if (!f) return;
    String *last = nullptr;
    il2cpp::il2cpp_field_static_get_value(f, &last);
    if (!last) return;
    std::string name = engine::to_utf8(last);
    if (name.empty()) return;  // nothing shown yet this session
    Handle go = g_ui->game_object_of(self);
    std::vector<std::string> tabs = g_ui->settings_tabs(go);
    if (tabs.empty() || std::find(tabs.begin(), tabs.end(), name) != tabs.end()) return;
    il2cpp::il2cpp_field_static_set_value(f, engine::to_string(tabs.front()));
    g_log.write(Level::Info, "settings menu: last tab '" + name + "' is not available; opening '" + tabs.front() + "'");
}

// The game's dropdown script stays on the mod's settings (it opens the list and
// handles navigation), but must not load or store their values in the game's
// own settings: these methods are skipped for objects named "LTM ...".
struct SkipSpec {
    const char *name;
    int argc;
    Shape shape;
};
const SkipSpec kSkips[] = {
    {"Start", 0, Shape::Instance0},
    {"ResetSelectedOption", 0, Shape::Instance0},
    {"OnDropdownOption", 1, Shape::InstanceInt1},
};

bool is_mod_object(Object *component) {
    Handle go = g_ui->game_object_of(component);
    return go && g_ui->name(go).rfind("LTM ", 0) == 0;
}

// Installs the skips; false (with the reason) when any is missing.
bool install_skips(std::string &reason) {
    Class *k = engine::find_class("UIBase", "CustomSettingDropdown");
    if (!k) {
        reason = "CustomSettingDropdown not found";
        return false;
    }
    for (const SkipSpec &spec : kSkips) {
        const MethodInfo *m = nullptr;
        // Only the class's own method: a base-class body is shared with other dropdowns.
        void *it = nullptr;
        while (const MethodInfo *c = il2cpp::il2cpp_class_get_methods(k, &it)) {
            if (std::string(il2cpp::il2cpp_method_get_name(c)) == spec.name &&
                static_cast<int>(il2cpp::il2cpp_method_get_param_count(c)) == spec.argc)
                m = c;
        }
        std::string where = std::string("CustomSettingDropdown.") + spec.name;
        if (!m) {
            reason = where + " not found";
            return false;
        }
        std::string error;
        if (!hook(m, spec.shape, true, nullptr, error, is_mod_object)) {
            reason = where + ": " + error;
            return false;
        }
    }
    return true;
}

// --- config file (written by the in-game settings tab) ---------------------------

class FileStore : public ConfigStore {
  public:
    explicit FileStore(std::wstring path) : path_(std::move(path)) {}
    bool read(std::string &text) override {
        text.clear();
        if (GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES) return true;  // missing: empty
        if (read_file(path_, text)) return true;
        g_log.write(Level::Error, "config.ini could not be read; the setting was not saved");
        return false;
    }
    bool write(const std::string &text) override {
        std::wstring tmp = path_ + L".tmp";
        FILE *f = _wfopen(tmp.c_str(), L"wb");
        bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if (f) ok = std::fclose(f) == 0 && ok;
        ok = ok && MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) {
            DeleteFileW(tmp.c_str());
            g_log.write(Level::Error, "config.ini could not be written (error " + std::to_string(GetLastError()) + ")");
            return false;
        }
        g_log.write(Level::Info, "settings changed in the game; saved to config.ini (active at the next start)");
        return true;
    }

  private:
    std::wstring path_;
};
std::unique_ptr<FileStore> g_store;

// `all` = false: only the settings tab (master switch off).
void install(bool all) {
    g_classes = std::make_unique<engine::UnityClasses>();
    std::vector<std::string> missing = g_classes->resolve();
    for (const std::string &m : missing) g_log.write(Level::Warn, "class not found: " + m);
    if (!g_classes->GameObject || !g_classes->Component || !g_classes->Transform || !g_classes->RectTransform ||
        !g_classes->Object) {
        g_log.write(Level::Error, "core Unity classes are missing; the mod stays inactive");
        for (const char *k : kFeatureKeys) g_log.write(Level::Error, std::string("FEATURE ") + k + " failed: Unity classes missing");
        return;
    }
    g_ui = std::make_unique<engine::UnityUi>(*g_classes);

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_log.write(Level::Error, std::string("MinHook initialisation failed: ") + MH_StatusToString(init));
        return;
    }

    bool hooked[kTargetCount] = {};
    std::string reasons[kTargetCount];
    for (const HookSpec &h : kHooks) {
        if (!all && h.target != Target::SettingsMenu) continue;
        int t = static_cast<int>(h.target);
        Class *k = engine::find_class(h.ns, h.cls);
        const MethodInfo *m = k ? engine::find_method(k, h.name, h.argc) : nullptr;
        std::string where = std::string(h.cls) + "." + h.name;
        if (!m) {
            reasons[t] = where + " not found";
            g_log.write(Level::Warn, "hook " + where + ": not found");
            continue;
        }
        std::string error;
        if (hook(m, h.shape, h.prefix, announce(h.target), error)) {
            hooked[t] = true;
            g_log.write(Level::Debug, "hooked " + where);
        } else {
            reasons[t] = where + ": " + error;
            g_log.write(Level::Warn, "hook " + where + ": " + error);
        }
    }
    {
        Class *menu = engine::find_class("UIBase", "SettingsMenu");
        const MethodInfo *awake = menu ? engine::find_method(menu, "Awake", 0) : nullptr;
        std::string error;
        if (!awake || !hook(awake, Shape::Instance0, true, forget_missing_last_tab, error))
            g_log.write(Level::Warn, "settings menu last-tab guard unavailable: " + (awake ? error : "Awake not found"));
    }
    std::string skip_reason;
    if (hooked[static_cast<int>(Target::SettingsMenu)] && !install_skips(skip_reason)) {
        // Without the skips the game would store the mod's values in its own settings.
        hooked[static_cast<int>(Target::SettingsMenu)] = false;
        reasons[static_cast<int>(Target::SettingsMenu)] = skip_reason;
        g_log.write(Level::Warn, "settings tab: " + skip_reason);
    }
    for (int t = 0; t < kTargetCount; ++t) {
        if (!hooked[t]) g_runtime->target_unavailable(static_cast<Target>(t), "no hook point: " + reasons[t]);
    }

    // Per-frame tick, right before the canvases are laid out and drawn.
    Class *canvas = engine::find_class("UnityEngine", "Canvas");
    const MethodInfo *frame = canvas ? engine::find_method(canvas, "SendWillRenderCanvases", 0) : nullptr;
    std::string error = "Canvas.SendWillRenderCanvases not found";
    if (!frame || !hook(frame, Shape::Static0, true, on_frame, error)) {
        g_log.write(Level::Error, "frame hook unavailable: " + error);
        g_runtime->target_unavailable(Target::ItemMenu, "frame hook unavailable");
        g_runtime->target_unavailable(Target::Notification, "frame hook unavailable");
        g_runtime->target_unavailable(Target::SettingsMenu, "frame hook unavailable");
        g_runtime->global_failed("pause_when_unfocused", "frame hook unavailable");
    }
}

// Maintainer aid: `[debug] dump_classes = Namespace.Class, ...` in config.ini
// writes those classes' fields and methods to the log at start-up.
void dump_classes(const std::string &config_text) {
    std::string section, list;
    std::istringstream in(config_text);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] == '[') section = line;
        else if (section == "[debug]" && line.rfind("dump_classes", 0) == 0 && line.find('=') != std::string::npos)
            list = line.substr(line.find('=') + 1);
    }
    std::istringstream items(list);
    for (std::string item; std::getline(items, item, ',');) {
        item.erase(0, item.find_first_not_of(' '));
        item.erase(item.find_last_not_of(' ') + 1);
        if (item.empty()) continue;
        size_t dot = item.rfind('.');
        std::string ns = dot == std::string::npos ? "" : item.substr(0, dot);
        std::string name = dot == std::string::npos ? item : item.substr(dot + 1);
        Class *k = engine::find_class(ns.c_str(), name.c_str());
        if (!k) {
            g_log.write(Level::Info, "dump " + item + ": not found");
            continue;
        }
        void *it = nullptr;
        while (il2cpp::FieldInfo *f = il2cpp::il2cpp_class_get_fields(k, &it)) {
            char *t = il2cpp::il2cpp_type_get_name(il2cpp::il2cpp_field_get_type(f));
            g_log.write(Level::Info, "dump " + item + " field " + il2cpp::il2cpp_field_get_name(f) + " : " + (t ? t : "?"));
            if (t) il2cpp::il2cpp_free(t);
        }
        it = nullptr;
        while (const MethodInfo *m = il2cpp::il2cpp_class_get_methods(k, &it)) {
            std::string sig;
            for (uint32_t i = 0; i < il2cpp::il2cpp_method_get_param_count(m); ++i) {
                char *t = il2cpp::il2cpp_type_get_name(il2cpp::il2cpp_method_get_param(m, i));
                sig += (i ? ", " : "") + std::string(t ? t : "?");
                if (t) il2cpp::il2cpp_free(t);
            }
            uint32_t iflags = 0;
            bool is_static = (il2cpp::il2cpp_method_get_flags(m, &iflags) & 0x10) != 0;
            g_log.write(Level::Info, "dump " + item + " method " + (is_static ? "static " : "") +
                                         il2cpp::il2cpp_method_get_name(m) + "(" + sig + ")");
        }
    }
}

}  // namespace

void runtime_start(void *self_module, void *game_assembly) {
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(static_cast<HMODULE>(self_module), path, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return;
    std::wstring dir(path, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring mod_dir = dir + L"\\TyrionCuthbertLargerTextMod";
    DWORD attrs = GetFileAttributesW(mod_dir.c_str());
    // Without its folder the mod is not installed: stay completely inactive.
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return;

    g_log.open(mod_dir + L"\\runtime.log");
    g_log.write(Level::Info, std::string("Tyrion Cuthbert Larger Text Mod ") + LTM_VERSION);
    wchar_t exe[MAX_PATH * 2];
    DWORD exe_len = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    g_log.write(Level::Info, "game: " + narrow(std::wstring(exe, exe_len)));

    std::string text;
    ConfigResult config;
    if (read_file(mod_dir + L"\\config.ini", text)) {
        config = parse_config(text);
    } else {
        config = parse_config("");
        config.warnings.assign(1, "config.ini not found; using the defaults");
    }
    for (const std::string &w : config.warnings) g_log.write(Level::Warn, "config: " + w);
    const Settings &s = config.settings;
    g_log.set_debug(s.debug);
    {
        std::ostringstream line;
        line << "settings: enabled=" << (s.enabled ? "true" : "false") << " scale=" << s.scale
             << " opacity=" << s.opacity;
        for (const char *k : kFeatureKeys) line << " " << k << "=" << (s.feature(k) ? "on" : "off");
        g_log.write(Level::Info, line.str());
    }

    if (const char *missing = il2cpp::resolve(game_assembly)) {
        g_log.write(Level::Error, std::string("IL2CPP export missing: ") + missing + "; the mod stays inactive");
        return;
    }

    dump_classes(text);
    g_command_file = mod_dir + L"\\debug-commands.txt";
    g_commands = debug_commands_enabled(text);
    if (g_commands) g_log.write(Level::Warn, "debug command file enabled");
    g_runtime = std::make_unique<Runtime>(s, g_log);
    g_store = std::make_unique<FileStore>(mod_dir + L"\\config.ini");
    g_runtime->set_config_store(g_store.get());
    if (!s.enabled) {
        // FR-RT-10: only the Mod tab in the Settings menu, so the mod can be
        // switched back on in the game.
        g_log.write(Level::Info, "master switch is off: the mod changes nothing but adds its settings tab");
    }
    g_runtime->report_disabled();
    try {
        install(s.enabled);
    } catch (const std::exception &e) {
        g_log.write(Level::Error, std::string("start-up failed: ") + e.what());
        g_runtime.reset();
    } catch (...) {
        g_log.write(Level::Error, "start-up failed");
        g_runtime.reset();
    }
}

}  // namespace ltm
