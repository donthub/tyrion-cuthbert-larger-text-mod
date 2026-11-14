#include "unity_ui.h"

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

}  // namespace ltm::engine
