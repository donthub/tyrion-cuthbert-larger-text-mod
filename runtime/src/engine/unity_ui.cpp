#include "unity_ui.h"

#include <algorithm>
#include <cmath>

namespace ltm::engine {

using namespace il2cpp;

namespace {

constexpr const char *kObject = "UnityEngine.Object";
constexpr const char *kTransform = "UnityEngine.Transform";
constexpr const char *kType = "System.Type";

struct ClassName {
    const char *ns, *name;
};
const ClassName kCompClasses[] = {
    {"TMPro", "TMP_Text"},
    {"", "SuperTextMesh"},
    {"UnityEngine.UI", "RawImage"},
    {"UnityEngine.UI", "Graphic"},
    {"UnityEngine.UI", "HorizontalLayoutGroup"},
    {"UnityEngine.UI", "VerticalLayoutGroup"},
    {"UnityEngine.UI", "LayoutElement"},
    {"UnityEngine.UI", "ContentSizeFitter"},
    {"UnityEngine.UI", "AspectRatioFitter"},
    {"UnityEngine.UI", "Button"},
    {"VisualNovel", "DialogueBox"},
    {"Courtroom", "TestimonyPrompt"},
    {"VisualNovel", "TextLogGUI"},
    {"Courtroom", "ItemMenu"},
    {"Courtroom", "AddItemDisplay"},
    {"VisualNovel", "ImageDisplay"},
    {"UIBase", "SettingsMenu"},
    {"UIBase", "SettingsTab"},
    {"UIBase", "LoadTabOnSelect"},
    {"UIBase", "LocalizedText"},
    {"UIBase", "SetActiveOnPlatforms"},
    {"UIBase", "CustomSettingDropdown"},
    {"TMPro", "TMP_Dropdown"},
};
static_assert(sizeof(kCompClasses) / sizeof(kCompClasses[0]) == static_cast<size_t>(Comp::Count), "classes");

// Field* kinds are SuperTextMesh fields (it has no properties for them).
enum class Kind { Float, Bool, Int, Field, FieldBool, FieldInt, Special };

struct Accessor {
    const char *getter, *setter;
    Kind kind;
};

Accessor accessor(Prop p) {
    switch (p) {
        case Prop::Enabled: return {"get_enabled", "set_enabled", Kind::Bool};
        case Prop::TmpFontSize: return {"get_fontSize", "set_fontSize", Kind::Float};
        case Prop::TmpAutoSize: return {"get_enableAutoSizing", "set_enableAutoSizing", Kind::Bool};
        case Prop::TmpFontSizeMin: return {"get_fontSizeMin", "set_fontSizeMin", Kind::Float};
        case Prop::TmpFontSizeMax: return {"get_fontSizeMax", "set_fontSizeMax", Kind::Float};
        case Prop::TmpCharacterSpacing: return {"get_characterSpacing", "set_characterSpacing", Kind::Float};
        case Prop::TmpOverflowMode: return {"get_overflowMode", "set_overflowMode", Kind::Int};
        case Prop::TmpPreferredHeight: return {"get_preferredHeight", nullptr, Kind::Float};
        case Prop::StmSize: return {"size", "size", Kind::Field};
        case Prop::StmCharacterSpacing: return {"characterSpacing", "characterSpacing", Kind::Field};
        case Prop::StmRememberReadPosition: return {"rememberReadPosition", "rememberReadPosition", Kind::FieldBool};
        case Prop::StmReading: return {"reading", nullptr, Kind::FieldBool};
        case Prop::StmReadTime: return {"currentReadTime", nullptr, Kind::Field};
        case Prop::StmTotalReadTime: return {"totalReadTime", nullptr, Kind::Field};
        case Prop::StmBestFit: return {"bestFit", "bestFit", Kind::FieldInt};
        case Prop::StmUiLimit: return {"uiLimit", "uiLimit", Kind::FieldBool};
        case Prop::StmVerticalLimitMode: return {"verticalLimitMode", nullptr, Kind::FieldInt};
        case Prop::LgSpacing: return {"get_spacing", "set_spacing", Kind::Float};
        case Prop::LgChildAlignment: return {"get_childAlignment", "set_childAlignment", Kind::Int};
        case Prop::LgControlWidth: return {"get_childControlWidth", "set_childControlWidth", Kind::Bool};
        case Prop::LgControlHeight: return {"get_childControlHeight", "set_childControlHeight", Kind::Bool};
        case Prop::LgExpandWidth: return {"get_childForceExpandWidth", "set_childForceExpandWidth", Kind::Bool};
        case Prop::LgExpandHeight: return {"get_childForceExpandHeight", "set_childForceExpandHeight", Kind::Bool};
        case Prop::LeIgnoreLayout: return {"get_ignoreLayout", "set_ignoreLayout", Kind::Bool};
        case Prop::LeMinWidth: return {"get_minWidth", "set_minWidth", Kind::Float};
        case Prop::LeMinHeight: return {"get_minHeight", "set_minHeight", Kind::Float};
        case Prop::LePreferredWidth: return {"get_preferredWidth", "set_preferredWidth", Kind::Float};
        case Prop::LePreferredHeight: return {"get_preferredHeight", "set_preferredHeight", Kind::Float};
        case Prop::LeFlexibleWidth: return {"get_flexibleWidth", "set_flexibleWidth", Kind::Float};
        case Prop::LeFlexibleHeight: return {"get_flexibleHeight", "set_flexibleHeight", Kind::Float};
        case Prop::FitHorizontal: return {"get_horizontalFit", "set_horizontalFit", Kind::Int};
        case Prop::FitVertical: return {"get_verticalFit", "set_verticalFit", Kind::Int};
        case Prop::AspectMode: return {"get_aspectMode", "set_aspectMode", Kind::Int};
        case Prop::DropdownValue: return {"get_value", "SetValueWithoutNotify", Kind::Int};
        case Prop::DropdownExpanded: return {"get_IsExpanded", nullptr, Kind::Bool};
        default: return {nullptr, nullptr, Kind::Special};
    }
}

Object *O(Handle h) { return static_cast<Object *>(h); }

}  // namespace

std::vector<std::string> UnityClasses::resolve() {
    std::vector<std::string> missing;
    auto get = [&](const char *ns, const char *name) {
        Class *k = find_class(ns, name);
        if (!k) missing.push_back(std::string(ns) + (*ns ? "." : "") + name);
        return k;
    };
    Object = get("UnityEngine", "Object");
    GameObject = get("UnityEngine", "GameObject");
    Component = get("UnityEngine", "Component");
    Transform = get("UnityEngine", "Transform");
    RectTransform = get("UnityEngine", "RectTransform");
    Behaviour = get("UnityEngine", "Behaviour");
    for (size_t i = 0; i < static_cast<size_t>(Comp::Count); ++i) comp[i] = get(kCompClasses[i].ns, kCompClasses[i].name);
    return missing;
}

// --- handles ------------------------------------------------------------------

Handle UnityUi::pin(Object *go) {
    if (!go) return nullptr;
    temp_.push_back(il2cpp_gchandle_new(go, false));
    return go;
}

void UnityUi::end_call() {
    for (GCHandle h : temp_) il2cpp_gchandle_free(h);
    temp_.clear();
}

void UnityUi::retain(Handle go) {
    auto &entry = retained_[go];
    if (entry.second++ == 0) entry.first = il2cpp_gchandle_new(O(go), false);
}

void UnityUi::release(Handle go) {
    auto it = retained_.find(go);
    if (it == retained_.end()) return;
    if (--it->second.second == 0) {
        il2cpp_gchandle_free(it->second.first);
        retained_.erase(it);
    }
}

Handle UnityUi::game_object_of(Object *component) {
    if (!component) return nullptr;
    return pin(invoke(method(k_.Component, "get_gameObject", 0), component));
}

Class *UnityUi::klass(Comp c) {
    Class *k = k_.comp[static_cast<int>(c)];
    if (!k) throw EngineError(std::string("class ") + comp_name(c) + " is unavailable");
    return k;
}

Object *UnityUi::transform(Handle go) {
    Object *t = invoke(method(k_.GameObject, "get_transform", 0), go);
    if (!t) throw EngineError("object has no transform");
    return t;
}

Object *UnityUi::rect_transform(Handle go) {
    Object *t = transform(go);
    if (!is_a(t, k_.RectTransform)) throw Mismatch("'" + name(go) + "' is not a UI object");
    return t;
}

Object *UnityUi::component(Handle go, Comp c) {
    Object *type = type_object(klass(c));
    Object *comp = invoke(method(k_.GameObject, "GetComponent", 1, {kType}), go, {type});
    if (!comp) throw Mismatch("'" + name(go) + "' has no " + comp_name(c));
    return comp;
}

// --- hierarchy -------------------------------------------------------------------

Handle UnityUi::find(Handle root, const std::string &path) {
    if (path.empty()) return root;
    Object *t = invoke(method(k_.Transform, "Find", 1, {"System.String"}), transform(root), {to_string(path)});
    return t ? game_object_of(t) : nullptr;
}

void UnityUi::set_name(Handle go, const std::string &object_name) {
    invoke(method(k_.Object, "set_name", 1), go, {to_string(object_name)});
}

std::string UnityUi::name(Handle go) {
    return to_utf8(reinterpret_cast<String *>(invoke(method(k_.Object, "get_name", 0), go)));
}

Handle UnityUi::parent(Handle go) {
    Object *p = invoke(method(k_.Transform, "get_parent", 0), transform(go));
    return p ? game_object_of(p) : nullptr;
}

int UnityUi::sibling_index(Handle go) {
    return unbox<int32_t>(invoke(method(k_.Transform, "GetSiblingIndex", 0), transform(go)));
}

int UnityUi::child_count(Handle go) {
    return unbox<int32_t>(invoke(method(k_.Transform, "get_childCount", 0), transform(go)));
}

Handle UnityUi::child(Handle go, int index) {
    int32_t i = index;
    return game_object_of(invoke(method(k_.Transform, "GetChild", 1), transform(go), {&i}));
}

void UnityUi::set_parent(Handle go, Handle new_parent, int index) {
    Object *t = transform(go);
    bool world_stays = false;
    invoke(method(k_.Transform, "SetParent", 2, {kTransform, "System.Boolean"}), t, {transform(new_parent), &world_stays});
    if (index < 0) {
        invoke(method(k_.Transform, "SetAsLastSibling", 0), t);
    } else {
        int32_t i = index;
        invoke(method(k_.Transform, "SetSiblingIndex", 1), t, {&i});
    }
}

bool UnityUi::active_self(Handle go) { return unbox<bool>(invoke(method(k_.GameObject, "get_activeSelf", 0), go)); }

bool UnityUi::active_in_hierarchy(Handle go) {
    return unbox<bool>(invoke(method(k_.GameObject, "get_activeInHierarchy", 0), go));
}

void UnityUi::set_active(Handle go, bool active) { invoke(method(k_.GameObject, "SetActive", 1), go, {&active}); }

int UnityUi::instance_id(Handle go) { return unbox<int32_t>(invoke(method(k_.Object, "GetInstanceID", 0), go)); }

bool UnityUi::alive(Handle go) {
    // UnityEngine.Object's bool conversion is false once the native object is gone.
    return unbox<bool>(invoke(method(k_.Object, "op_Implicit", 1, {kObject}), nullptr, {go}));
}

// --- components --------------------------------------------------------------------

bool UnityUi::has(Handle go, Comp c) {
    Object *type = type_object(klass(c));
    return invoke(method(k_.GameObject, "GetComponent", 1, {kType}), go, {type}) != nullptr;
}

void UnityUi::add(Handle go, Comp c) {
    Object *type = type_object(klass(c));
    if (!invoke(method(k_.GameObject, "AddComponent", 1, {kType}), go, {type})) {
        throw EngineError(std::string("could not add ") + comp_name(c));
    }
}

void UnityUi::remove(Handle go, Comp c) {
    Object *comp = component(go, c);
    invoke(method(k_.Object, "DestroyImmediate", 1, {kObject}), nullptr, {comp});
}

double UnityUi::get(Handle go, Comp c, Prop p) {
    Object *comp = component(go, c);
    Class *k = il2cpp_object_get_class(comp);
    switch (p) {
        case Prop::GraphicAlpha:
            return unbox<Color>(invoke(method(k, "get_color", 0), comp)).a;
        case Prop::TmpRenderedHeight: {
            bool only_visible = true;
            return unbox<Vector2>(invoke(method(k, "GetRenderedValues", 1, {"System.Boolean"}), comp, {&only_visible})).y;
        }
        case Prop::TmpLineHeight:
        case Prop::TmpLinePitch: {
            // Measured with sample text at the component's current font and
            // size. Call this on a text that isn't displayed: GetPreferredValues
            // re-parses the given string into the component's buffers.
            const MethodInfo *m = method(k, "GetPreferredValues", 3, {"System.String", "System.Single", "System.Single"});
            auto pref = [&](const char *text, float width) {
                float height = 1e6f;
                return unbox<Vector2>(invoke(m, comp, {to_string(text), &width, &height}));
            };
            Vector2 one = pref("A", 1e6f);
            if (p == Prop::TmpLineHeight) return one.y;
            // A width between "A" and "A A" wraps the second word onto a new
            // line of the same paragraph (no paragraph spacing).
            float wrap = (one.x + pref("A A", 1e6f).x) / 2.0f;
            double pitch = static_cast<double>(pref("A A", wrap).y) - one.y;
            if (!(pitch > 0.0) || !std::isfinite(pitch)) throw Mismatch("could not measure the text line pitch");
            return pitch;
        }
        case Prop::TmpOverflowing: {
            bool overflowing = unbox<bool>(invoke(method(k, "get_isTextOverflowing", 0), comp));
            int32_t first = unbox<int32_t>(invoke(method(k, "get_firstOverflowCharacterIndex", 0), comp));
            return overflowing || first >= 0 ? 1.0 : 0.0;
        }
        default: break;
    }
    Accessor a = accessor(p);
    switch (a.kind) {
        case Kind::Float: return unbox<float>(invoke(method(k, a.getter, 0), comp));
        case Kind::Bool: return unbox<bool>(invoke(method(k, a.getter, 0), comp)) ? 1.0 : 0.0;
        case Kind::Int: return unbox<int32_t>(invoke(method(k, a.getter, 0), comp));
        case Kind::Field:
        case Kind::FieldBool:
        case Kind::FieldInt: {
            FieldInfo *f = il2cpp_class_get_field_from_name(k, a.getter);
            if (!f) throw Mismatch(std::string("field ") + a.getter + " is unavailable");
            if (a.kind == Kind::FieldBool) {
                bool v = false;
                il2cpp_field_get_value(comp, f, &v);
                return v ? 1.0 : 0.0;
            }
            if (a.kind == Kind::FieldInt) {  // also enums with a 32-bit underlying type
                int32_t v = 0;
                il2cpp_field_get_value(comp, f, &v);
                return v;
            }
            float v = 0;
            il2cpp_field_get_value(comp, f, &v);
            return v;
        }
        default: throw EngineError(std::string("cannot read ") + prop_name(p));
    }
}

void UnityUi::set(Handle go, Comp c, Prop p, double value) {
    Object *comp = component(go, c);
    Class *k = il2cpp_object_get_class(comp);
    if (p == Prop::GraphicAlpha) {
        Color color = unbox<Color>(invoke(method(k, "get_color", 0), comp));
        color.a = static_cast<float>(value);
        invoke(method(k, "set_color", 1), comp, {&color});
        return;
    }
    Accessor a = accessor(p);
    if (!a.setter) throw EngineError(std::string("cannot write ") + prop_name(p));
    switch (a.kind) {
        case Kind::Float: {
            float v = static_cast<float>(value);
            invoke(method(k, a.setter, 1), comp, {&v});
            return;
        }
        case Kind::Bool: {
            bool v = value != 0.0;
            invoke(method(k, a.setter, 1), comp, {&v});
            return;
        }
        case Kind::Int: {
            int32_t v = static_cast<int32_t>(std::lround(value));
            invoke(method(k, a.setter, 1), comp, {&v});
            return;
        }
        case Kind::Field:
        case Kind::FieldBool:
        case Kind::FieldInt: {
            FieldInfo *f = il2cpp_class_get_field_from_name(k, a.setter);
            if (!f) throw Mismatch(std::string("field ") + a.setter + " is unavailable");
            if (a.kind == Kind::FieldBool) {
                bool v = value != 0.0;
                il2cpp_field_set_value(comp, f, &v);
            } else if (a.kind == Kind::FieldInt) {
                int32_t v = static_cast<int32_t>(std::lround(value));
                il2cpp_field_set_value(comp, f, &v);
            } else {
                float v = static_cast<float>(value);
                il2cpp_field_set_value(comp, f, &v);
            }
            return;
        }
        default: throw EngineError(std::string("cannot write ") + prop_name(p));
    }
}

// --- rect transforms ---------------------------------------------------------------

RectData UnityUi::rect(Handle go) {
    Object *t = rect_transform(go);
    Class *k = k_.RectTransform;
    auto v2 = [&](const char *getter) {
        Vector2 v = unbox<Vector2>(invoke(method(k, getter, 0), t));
        return Vec2{v.x, v.y};
    };
    RectData r;
    r.anchorMin = v2("get_anchorMin");
    r.anchorMax = v2("get_anchorMax");
    r.anchoredPosition = v2("get_anchoredPosition");
    r.sizeDelta = v2("get_sizeDelta");
    r.pivot = v2("get_pivot");
    return r;
}

void UnityUi::set_rect(Handle go, const RectData &r) {
    Object *t = rect_transform(go);
    Class *k = k_.RectTransform;
    auto v2 = [&](const char *setter, Vec2 value) {
        Vector2 v{value.x, value.y};
        invoke(method(k, setter, 1), t, {&v});
    };
    v2("set_anchorMin", r.anchorMin);
    v2("set_anchorMax", r.anchorMax);
    v2("set_pivot", r.pivot);
    v2("set_sizeDelta", r.sizeDelta);
    v2("set_anchoredPosition", r.anchoredPosition);
}

Vec2 UnityUi::rect_size(Handle go) {
    Rect r = unbox<Rect>(invoke(method(k_.RectTransform, "get_rect", 0), rect_transform(go)));
    return {r.width, r.height};
}

Vec2 UnityUi::local_scale(Handle go) {
    Vector3 v = unbox<Vector3>(invoke(method(k_.Transform, "get_localScale", 0), transform(go)));
    return {v.x, v.y};
}

Bounds UnityUi::bounds_in(Handle go, Handle ref) {
    // Each corner of the local rect goes through world space into `ref`'s
    // local space (no Vector3[] needed, unlike GetWorldCorners).
    Object *t = transform(go);
    Object *rt = transform(ref);
    Rect r = unbox<Rect>(invoke(method(k_.RectTransform, "get_rect", 0), rect_transform(go)));
    const MethodInfo *to_world = method(k_.Transform, "TransformPoint", 1, {"UnityEngine.Vector3"});
    const MethodInfo *to_ref = method(k_.Transform, "InverseTransformPoint", 1, {"UnityEngine.Vector3"});
    Bounds b{1e30f, 1e30f, -1e30f, -1e30f};
    for (float x : {r.x, r.x + r.width}) {
        for (float y : {r.y, r.y + r.height}) {
            Vector3 local{x, y, 0};
            Vector3 world = unbox<Vector3>(invoke(to_world, t, {&local}));
            Vector3 p = unbox<Vector3>(invoke(to_ref, rt, {&world}));
            b.x_min = std::min(b.x_min, p.x);
            b.y_min = std::min(b.y_min, p.y);
            b.x_max = std::max(b.x_max, p.x);
            b.y_max = std::max(b.y_max, p.y);
        }
    }
    return b;
}

// --- objects ----------------------------------------------------------------------

Handle UnityUi::create(const std::string &object_name, Handle parent_go) {
    Object *go = il2cpp_object_new(k_.GameObject);
    if (!go) throw EngineError("could not allocate a GameObject");
    Handle h = pin(go);
    invoke(method(k_.GameObject, ".ctor", 1, {"System.String"}), go, {to_string(object_name)});
    // Adding a RectTransform turns the object into a UI object.
    Object *type = type_object(k_.RectTransform);
    invoke(method(k_.GameObject, "AddComponent", 1, {kType}), go, {type});
    int32_t layer = unbox<int32_t>(invoke(method(k_.GameObject, "get_layer", 0), parent_go));
    invoke(method(k_.GameObject, "set_layer", 1), go, {&layer});
    set_parent(h, parent_go, -1);
    return h;
}

Handle UnityUi::clone(Handle original, Handle parent_go, const std::string &object_name) {
    Object *copy = invoke(method(k_.Object, "Instantiate", 2, {kObject, kTransform}), nullptr,
                          {original, transform(parent_go)});
    if (!copy) throw EngineError("could not copy '" + name(original) + "'");
    Handle h = pin(copy);
    invoke(method(k_.Object, "set_name", 1), copy, {to_string(object_name)});
    return h;
}

void UnityUi::destroy(Handle go) { invoke(method(k_.Object, "DestroyImmediate", 1, {kObject}), nullptr, {go}); }

// --- text ------------------------------------------------------------------------

Handle UnityUi::linked_text(Handle go) {
    Object *tmp = component(go, Comp::Tmp);
    Object *linked = invoke(method(il2cpp_object_get_class(tmp), "get_linkedTextComponent", 0), tmp);
    return linked ? game_object_of(linked) : nullptr;
}

void UnityUi::set_linked_text(Handle go, Handle target) {
    Object *tmp = component(go, Comp::Tmp);
    Object *target_tmp = target ? component(target, Comp::Tmp) : nullptr;
    invoke(method(il2cpp_object_get_class(tmp), "set_linkedTextComponent", 1), tmp, {target_tmp});
}

void UnityUi::update_text(Handle go, Comp c) {
    Object *comp = component(go, c);
    Class *k = il2cpp_object_get_class(comp);
    if (c == Comp::Tmp) {
        bool ignore_active_state = false, force_reparsing = false;
        invoke(method(k, "ForceMeshUpdate", 2), comp, {&ignore_active_state, &force_reparsing});
    } else if (c == Comp::Stm) {
        invoke(method(k, "Rebuild", 0), comp);
    }
}

const void *UnityUi::text_identity(Handle go, Comp c) {
    Object *comp = component(go, c);
    if (c == Comp::Stm) {
        FieldInfo *f = il2cpp_class_get_field_from_name(il2cpp_object_get_class(comp), "_text");
        if (!f) throw EngineError("field _text is unavailable");
        Object *text = nullptr;
        il2cpp_field_get_value(comp, f, &text);
        return text;
    }
    return invoke(method(il2cpp_object_get_class(comp), "get_text", 0), comp);
}

std::string UnityUi::text(Handle go, Comp c) {
    if (c != Comp::Tmp) throw EngineError("text() supports TMP only");
    Object *comp = component(go, c);
    return to_utf8(reinterpret_cast<String *>(invoke(method(il2cpp_object_get_class(comp), "get_text", 0), comp)));
}

void UnityUi::show_text_until(Handle go, double read_time) {
    Object *comp = component(go, Comp::Stm);
    float t = static_cast<float>(read_time);
    invoke(method(il2cpp_object_get_class(comp), "SetMesh", 1, {"System.Single"}), comp, {&t});
}

int UnityUi::enum_value(Handle go, Comp c, Prop p, const std::string &member) {
    Accessor a = accessor(p);
    if (a.kind != Kind::FieldInt) throw EngineError(std::string(prop_name(p)) + " is not an enum field");
    Object *comp = component(go, c);
    FieldInfo *f = il2cpp_class_get_field_from_name(il2cpp_object_get_class(comp), a.getter);
    if (!f) throw Mismatch(std::string("field ") + a.getter + " is unavailable");
    Class *e = il2cpp_class_from_type(il2cpp_field_get_type(f));
    FieldInfo *constant = e ? il2cpp_class_get_field_from_name(e, member.c_str()) : nullptr;
    if (!constant) throw Mismatch(std::string(a.getter) + " has no value " + member);
    // Enum members are literal static fields; IL2CPP returns their constant value.
    int32_t v = 0;
    il2cpp_field_static_get_value(constant, &v);
    return v;
}

// --- strings, dropdowns, arrays ------------------------------------------------------

namespace {

Class *system_class(const char *ns, const char *name) {
    Class *k = find_class(ns, name);
    if (!k) throw EngineError(std::string("class ") + ns + "." + name + " is unavailable");
    return k;
}

FieldInfo *field(Object *obj, const std::string &name) {
    FieldInfo *f = il2cpp_class_get_field_from_name(il2cpp_object_get_class(obj), name.c_str());
    if (!f) throw Mismatch(class_name(obj) + " has no field " + name);
    return f;
}

Object *get_ref(Object *obj, const std::string &name) {
    Object *v = nullptr;
    il2cpp_field_get_value(obj, field(obj, name), &v);
    return v;
}

void set_ref(Object *obj, const std::string &name, Object *value) {
    // il2cpp_field_set_value copies the reference with the GC write barrier.
    il2cpp_field_set_value(obj, field(obj, name), value);
}

// System.Array through its managed methods, so element writes go through the
// runtime (type checks, write barriers, boxing of value-type elements).
int array_length(Object *array) {
    Class *k = system_class("System", "Array");
    return unbox<int32_t>(invoke(method(k, "get_Length", 0), array));
}
Object *array_get(Object *array, int index) {
    int32_t i = index;
    return invoke(method(system_class("System", "Array"), "GetValue", 1, {"System.Int32"}), array, {&i});
}
void array_set(Object *array, int index, Object *value) {
    int32_t i = index;
    invoke(method(system_class("System", "Array"), "SetValue", 2, {"System.Object", "System.Int32"}), array, {value, &i});
}
Object *array_new(Class *element, int length) {
    int32_t n = length;
    Object *a = invoke(method(system_class("System", "Array"), "CreateInstance", 2, {kType, "System.Int32"}), nullptr,
                       {type_object(element), &n});
    if (!a) throw EngineError("could not create an array");
    return a;
}

}  // namespace

void UnityUi::set_text(Handle go, const std::string &value) {
    Object *tmp = component(go, Comp::Tmp);
    invoke(method(il2cpp_object_get_class(tmp), "set_text", 1, {"System.String"}), tmp, {to_string(value)});
}

void UnityUi::set_dropdown_options(Handle go, const std::vector<std::string> &labels) {
    Object *dropdown = component(go, Comp::Dropdown);
    Class *k = il2cpp_object_get_class(dropdown);
    Object *list = invoke(method(k, "get_options", 0), dropdown);
    if (!list) throw EngineError("dropdown has no option list");
    Class *list_class = il2cpp_object_get_class(list);
    const MethodInfo *add = method(list_class, "Add", 1);
    Class *option = il2cpp_class_from_type(il2cpp_method_get_param(add, 0));
    if (!option) throw EngineError("dropdown option class is unavailable");
    invoke(method(list_class, "Clear", 0), list);
    for (const std::string &label : labels) {
        Object *o = il2cpp_object_new(option);
        if (!o) throw EngineError("could not create a dropdown option");
        invoke(method(option, ".ctor", 1, {"System.String"}), o, {to_string(label)});
        invoke(add, list, {o});
    }
    invoke(method(k, "RefreshShownValue", 0), dropdown);
}

std::string UnityUi::get_string(Handle go, Comp c, const std::string &name_) {
    return to_utf8(reinterpret_cast<String *>(get_ref(component(go, c), name_)));
}

void UnityUi::set_string(Handle go, Comp c, const std::string &name_, const std::string &value) {
    set_ref(component(go, c), name_, reinterpret_cast<Object *>(to_string(value)));
}

void UnityUi::set_strings(Handle go, Comp c, const std::string &name_, const std::vector<std::string> &values) {
    Object *comp = component(go, c);
    field(comp, name_);  // check before allocating
    Object *array = array_new(system_class("System", "String"), static_cast<int>(values.size()));
    for (size_t i = 0; i < values.size(); ++i) array_set(array, static_cast<int>(i), reinterpret_cast<Object *>(to_string(values[i])));
    set_ref(comp, name_, array);
}

Handle UnityUi::holder() {
    if (holder_ && alive(holder_)) return holder_;
    Object *go = il2cpp_object_new(k_.GameObject);
    if (!go) throw EngineError("could not allocate a GameObject");
    invoke(method(k_.GameObject, ".ctor", 1, {"System.String"}), go, {to_string("LTM Holder")});
    bool off = false;
    invoke(method(k_.GameObject, "SetActive", 1), go, {&off});
    invoke(method(k_.Object, "DontDestroyOnLoad", 1, {kObject}), nullptr, {go});
    if (holder_) il2cpp_gchandle_free(holder_handle_);
    holder_handle_ = il2cpp_gchandle_new(go, false);
    holder_ = go;
    return go;
}

// --- the game's Settings menu --------------------------------------------------------
// UIBase.SettingsMenu.tabs is a SettingsTabInfo[] of {name, prefab, button}.

namespace {

// The object for a reference field of the given type: the GameObject itself or
// its component of the field's type.
Object *as_field_value(FieldInfo *f, Object *go, Class *game_object, Class *component_class) {
    if (!go) return nullptr;
    Class *k = il2cpp_class_from_type(il2cpp_field_get_type(f));
    if (k == game_object) return go;
    Object *c = invoke(method(game_object, "GetComponent", 1, {kType}), go, {type_object(k)});
    if (!c) throw Mismatch(std::string("object has no ") + il2cpp_class_get_name(k));
    (void)component_class;
    return c;
}

}  // namespace

Object *UnityUi::settings_tab_array(Handle menu) {
    Object *tabs = get_ref(component(menu, Comp::SettingsMenu), "tabs");
    if (!tabs) throw Mismatch("the Settings menu has no tab list");
    return tabs;
}

std::vector<std::string> UnityUi::settings_tabs(Handle menu) {
    Object *tabs = settings_tab_array(menu);
    std::vector<std::string> names;
    for (int i = 0, n = array_length(tabs); i < n; ++i) {
        Object *info = array_get(tabs, i);
        names.push_back(info ? to_utf8(reinterpret_cast<String *>(get_ref(info, "name"))) : "");
    }
    return names;
}

Handle UnityUi::settings_tab_prefab(Handle menu, const std::string &tab) {
    Object *tabs = settings_tab_array(menu);
    for (int i = 0, n = array_length(tabs); i < n; ++i) {
        Object *info = array_get(tabs, i);
        if (!info || to_utf8(reinterpret_cast<String *>(get_ref(info, "name"))) != tab) continue;
        Object *prefab = get_ref(info, "prefab");
        if (!prefab) return nullptr;
        return is_a(prefab, k_.GameObject) ? pin(prefab) : game_object_of(prefab);
    }
    return nullptr;
}

void UnityUi::add_settings_tab(Handle menu, const std::string &tab, Handle prefab, Handle button) {
    Object *settings = component(menu, Comp::SettingsMenu);
    Object *tabs = settings_tab_array(menu);
    int n = array_length(tabs);
    if (n == 0) throw Mismatch("the Settings menu has no tabs");
    Object *first = array_get(tabs, 0);
    if (!first) throw Mismatch("the Settings menu's first tab is empty");
    Class *info_class = il2cpp_object_get_class(first);  // SettingsTabInfo (boxed if a struct)
    Object *info = il2cpp_object_new(info_class);
    if (!info) throw EngineError("could not create a tab entry");
    if (!il2cpp_class_is_valuetype(info_class)) {
        if (const MethodInfo *ctor = find_method(info_class, ".ctor", 0)) invoke(ctor, info);
    }
    set_ref(info, "name", reinterpret_cast<Object *>(to_string(tab)));
    set_ref(info, "prefab", as_field_value(field(info, "prefab"), O(prefab), k_.GameObject, k_.Component));
    set_ref(info, "button", as_field_value(field(info, "button"), O(button), k_.GameObject, k_.Component));
    Object *grown = array_new(info_class, n + 1);
    for (int i = 0; i < n; ++i) array_set(grown, i, array_get(tabs, i));
    array_set(grown, n, info);
    set_ref(settings, "tabs", grown);
}

void UnityUi::remove_settings_tab(Handle menu, const std::string &tab) {
    Object *settings = component(menu, Comp::SettingsMenu);
    Object *tabs = settings_tab_array(menu);
    std::vector<Object *> kept;
    Class *info_class = nullptr;
    for (int i = 0, n = array_length(tabs); i < n; ++i) {
        Object *info = array_get(tabs, i);
        if (info && !info_class) info_class = il2cpp_object_get_class(info);
        if (info && to_utf8(reinterpret_cast<String *>(get_ref(info, "name"))) == tab) continue;
        kept.push_back(info);
    }
    if (!info_class || kept.size() == static_cast<size_t>(array_length(tabs))) return;
    Object *shrunk = array_new(info_class, static_cast<int>(kept.size()));
    for (size_t i = 0; i < kept.size(); ++i) array_set(shrunk, static_cast<int>(i), kept[i]);
    set_ref(settings, "tabs", shrunk);
}

std::string UnityUi::current_settings_tab(Handle menu) {
    return to_utf8(reinterpret_cast<String *>(get_ref(component(menu, Comp::SettingsMenu), "currentTab")));
}

Handle UnityUi::loaded_settings_tab(Handle menu) {
    Object *go = get_ref(component(menu, Comp::SettingsMenu), "loadedTab");
    if (!go || !alive(go)) return nullptr;
    return pin(go);
}

void UnityUi::set_settings_options(Handle tab, const std::vector<Handle> &options) {
    Object *settings_tab = component(tab, Comp::SettingsTab);
    FieldInfo *all = field(settings_tab, "allSettings");
    Class *array_class = il2cpp_class_from_type(il2cpp_field_get_type(all));
    (void)array_class;
    Class *option_class = system_class("UIBase", "SettingsOption");
    Object *array = array_new(option_class, static_cast<int>(options.size()));
    Object *first = nullptr;
    for (size_t i = 0; i < options.size(); ++i) {
        Object *option = component(options[i], Comp::CustomSettingDropdown);
        if (!first) first = option;
        array_set(array, static_cast<int>(i), option);
    }
    set_ref(settings_tab, "allSettings", array);
    set_ref(settings_tab, "firstSelect", first);
}

}  // namespace ltm::engine
