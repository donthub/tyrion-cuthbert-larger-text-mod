// Thin helpers over the IL2CPP C API for the Unity types the runtime uses.
#pragma once
#include <initializer_list>
#include <string>
#include <vector>

#include "../core/ui.h"
#include "il2cpp_api.h"

namespace ltm::engine {

using il2cpp::Class;
using il2cpp::MethodInfo;
using il2cpp::Object;

struct Vector2 {
    float x, y;
};
struct Vector3 {
    float x, y, z;
};
struct Color {
    float r, g, b, a;
};
struct Rect {
    float x, y, width, height;
};

// Finds a class in any loaded assembly. Returns nullptr when missing.
Class *find_class(const char *ns, const char *name);
// Finds a method on the class or a base class. `params` are full type names;
// an empty list matches any overload with `argc` parameters.
const MethodInfo *find_method(Class *klass, const char *name, int argc,
                              std::initializer_list<const char *> params = {});
// Same, but throws EngineError when missing.
const MethodInfo *method(Class *klass, const char *name, int argc, std::initializer_list<const char *> params = {});
// Native code address of a method (MethodInfo::methodPointer).
void *method_pointer(const MethodInfo *m);

// Invokes a managed method; a managed exception becomes EngineError.
Object *invoke(const MethodInfo *m, void *self, std::initializer_list<void *> args = {});

template <typename T>
T unbox(Object *boxed) {
    if (!boxed) throw EngineError("unexpected null result");
    return *static_cast<T *>(il2cpp::il2cpp_object_unbox(boxed));
}

std::string to_utf8(il2cpp::String *s);
il2cpp::String *to_string(const std::string &utf8);
std::string class_name(Object *obj);
bool is_a(Object *obj, Class *klass);
Object *type_object(Class *klass);  // System.Type for a class (cached, pinned)

}  // namespace ltm::engine
