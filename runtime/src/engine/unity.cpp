#include "unity.h"

#include <map>
#include <tuple>

namespace ltm::engine {

using namespace il2cpp;

Class *find_class(const char *ns, const char *name) {
    size_t count = 0;
    const Assembly **assemblies = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &count);
    for (size_t i = 0; i < count; ++i) {
        const Image *image = il2cpp_assembly_get_image(assemblies[i]);
        if (Class *k = il2cpp_class_from_name(image, ns, name)) return k;
    }
    return nullptr;
}

namespace {

std::string type_name(const Type *t) {
    char *n = t ? il2cpp_type_get_name(t) : nullptr;
    std::string s = n ? n : "";
    if (n) il2cpp_free(n);
    return s;
}

bool params_match(const MethodInfo *m, std::initializer_list<const char *> params) {
    uint32_t i = 0;
    for (const char *p : params) {
        if (type_name(il2cpp_method_get_param(m, i++)) != p) return false;
    }
    return true;
}

}  // namespace

const MethodInfo *find_method(Class *klass, const char *name, int argc, std::initializer_list<const char *> params) {
    using Key = std::tuple<Class *, std::string, int, std::string>;
    static std::map<Key, const MethodInfo *> cache;
    std::string sig;
    for (const char *p : params) sig += std::string(p) + ",";
    Key key{klass, name, argc, sig};
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const MethodInfo *found = nullptr;
    for (Class *k = klass; k && !found; k = il2cpp_class_get_parent(k)) {
        void *iter = nullptr;
        while (const MethodInfo *m = il2cpp_class_get_methods(k, &iter)) {
            if (std::string(il2cpp_method_get_name(m)) != name) continue;
            if (static_cast<int>(il2cpp_method_get_param_count(m)) != argc) continue;
            if (params.size() && !params_match(m, params)) continue;
            found = m;
            break;
        }
    }
    cache[key] = found;
    return found;
}

const MethodInfo *method(Class *klass, const char *name, int argc, std::initializer_list<const char *> params) {
    if (!klass) throw EngineError(std::string("class for ") + name + " is unavailable");
    const MethodInfo *m = find_method(klass, name, argc, params);
    if (!m) {
        throw EngineError(std::string("method ") + il2cpp_class_get_name(klass) + "." + name + " is unavailable");
    }
    return m;
}

void *method_pointer(const MethodInfo *m) {
    // methodPointer is the first member of MethodInfo in every IL2CPP version.
    return m ? *reinterpret_cast<void *const *>(m) : nullptr;
}

std::string class_name(Object *obj) {
    if (!obj) return "null";
    Class *k = il2cpp_object_get_class(obj);
    std::string ns = il2cpp_class_get_namespace(k);
    return (ns.empty() ? "" : ns + ".") + il2cpp_class_get_name(k);
}

Object *invoke(const MethodInfo *m, void *self, std::initializer_list<void *> args) {
    void *argv[8] = {};
    size_t n = 0;
    for (void *a : args) {
        if (n == 8) throw EngineError("too many arguments");
        argv[n++] = a;
    }
    Exception *exc = nullptr;
    Object *result = il2cpp_runtime_invoke(m, self, argv, &exc);
    if (exc) {
        throw EngineError(std::string(il2cpp_method_get_name(m)) + " threw " +
                          class_name(reinterpret_cast<Object *>(exc)));
    }
    return result;
}

std::string to_utf8(String *s) {
    if (!s) return "";
    int32_t len = il2cpp_string_length(s);
    const char16_t *c = il2cpp_string_chars(s);
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (int32_t i = 0; i < len; ++i) {
        uint32_t cp = c[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < len) {
            uint32_t lo = c[i + 1];
            if (lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

String *to_string(const std::string &utf8) { return il2cpp_string_new(utf8.c_str()); }

bool is_a(Object *obj, Class *klass) {
    if (!obj || !klass) return false;
    Class *k = il2cpp_object_get_class(obj);
    return k == klass || il2cpp_class_is_subclass_of(k, klass, false);
}

Object *type_object(Class *klass) {
    static std::map<Class *, Object *> cache;
    auto it = cache.find(klass);
    if (it != cache.end()) return it->second;
    Object *t = il2cpp_type_get_object(il2cpp_class_get_type(klass));
    if (t) il2cpp_gchandle_new(t, false);  // keep it alive for the session
    cache[klass] = t;
    return t;
}

}  // namespace ltm::engine
