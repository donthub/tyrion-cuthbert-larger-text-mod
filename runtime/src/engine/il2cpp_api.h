// The exported IL2CPP C API, resolved at runtime from GameAssembly.dll.
//
// These exports are the engine's stable embedding API. Unlike tools that parse
// global-metadata.dat, using them does not depend on the metadata version, so
// the runtime keeps working when Unity changes that file format.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ltm::il2cpp {

struct Domain;
struct Assembly;
struct Image;
struct Class;
struct Type;
struct MethodInfo;
struct FieldInfo;
struct PropertyInfo;
struct Object;
struct String;
struct Thread;
struct Exception;

// GC handles are pointer-sized since Unity 2023 (they were uint32_t before).
using GCHandle = uintptr_t;

#define LTM_IL2CPP_API(X)                                                                  \
    X(Domain *, il2cpp_domain_get, ())                                                     \
    X(const Assembly **, il2cpp_domain_get_assemblies, (const Domain *, size_t *))         \
    X(const Image *, il2cpp_assembly_get_image, (const Assembly *))                        \
    X(const char *, il2cpp_image_get_name, (const Image *))                                \
    X(size_t, il2cpp_image_get_class_count, (const Image *))                               \
    X(const Class *, il2cpp_image_get_class, (const Image *, size_t))                      \
    X(Class *, il2cpp_class_from_name, (const Image *, const char *, const char *))        \
    X(const char *, il2cpp_class_get_name, (const Class *))                                \
    X(const char *, il2cpp_class_get_namespace, (const Class *))                           \
    X(Class *, il2cpp_class_get_parent, (Class *))                                         \
    X(const MethodInfo *, il2cpp_class_get_methods, (Class *, void **))                    \
    X(const MethodInfo *, il2cpp_class_get_method_from_name, (Class *, const char *, int)) \
    X(FieldInfo *, il2cpp_class_get_fields, (Class *, void **))                            \
    X(FieldInfo *, il2cpp_class_get_field_from_name, (Class *, const char *))              \
    X(const PropertyInfo *, il2cpp_class_get_properties, (Class *, void **))               \
    X(const Type *, il2cpp_class_get_type, (Class *))                                      \
    X(bool, il2cpp_class_is_subclass_of, (Class *, Class *, bool))                         \
    X(bool, il2cpp_class_is_valuetype, (const Class *))                                    \
    X(const char *, il2cpp_method_get_name, (const MethodInfo *))                          \
    X(uint32_t, il2cpp_method_get_param_count, (const MethodInfo *))                       \
    X(const Type *, il2cpp_method_get_param, (const MethodInfo *, uint32_t))               \
    X(const Type *, il2cpp_method_get_return_type, (const MethodInfo *))                   \
    X(uint32_t, il2cpp_method_get_flags, (const MethodInfo *, uint32_t *))                 \
    X(const char *, il2cpp_field_get_name, (FieldInfo *))                                  \
    X(size_t, il2cpp_field_get_offset, (FieldInfo *))                                      \
    X(const Type *, il2cpp_field_get_type, (FieldInfo *))                                  \
    X(int, il2cpp_field_get_flags, (FieldInfo *))                                          \
    X(void, il2cpp_field_get_value, (Object *, FieldInfo *, void *))                       \
    X(void, il2cpp_field_set_value, (Object *, FieldInfo *, void *))                       \
    X(void, il2cpp_field_static_get_value, (FieldInfo *, void *))                          \
    X(const char *, il2cpp_property_get_name, (const PropertyInfo *))                      \
    X(char *, il2cpp_type_get_name, (const Type *))                                        \
    X(Object *, il2cpp_type_get_object, (const Type *))                                    \
    X(Class *, il2cpp_class_from_type, (const Type *))                                     \
    X(Class *, il2cpp_object_get_class, (Object *))                                        \
    X(void *, il2cpp_object_unbox, (Object *))                                             \
    X(Object *, il2cpp_value_box, (Class *, void *))                                       \
    X(Object *, il2cpp_runtime_invoke, (const MethodInfo *, void *, void **, Exception **)) \
    X(String *, il2cpp_string_new, (const char *))                                         \
    X(int32_t, il2cpp_string_length, (String *))                                           \
    X(char16_t *, il2cpp_string_chars, (String *))                                         \
    X(Thread *, il2cpp_thread_attach, (Domain *))                                          \
    X(GCHandle, il2cpp_gchandle_new, (Object *, bool))                                     \
    X(Object *, il2cpp_object_new, (const Class *))                                        \
    X(Object *, il2cpp_gchandle_get_target, (GCHandle))                                    \
    X(void, il2cpp_gchandle_free, (GCHandle))                                              \
    X(void *, il2cpp_resolve_icall, (const char *))                                        \
    X(void, il2cpp_free, (void *))

#define LTM_IL2CPP_DECLARE(ret, name, args) using name##_t = ret(*) args; extern name##_t name;
LTM_IL2CPP_API(LTM_IL2CPP_DECLARE)
#undef LTM_IL2CPP_DECLARE

// Resolves every export from the given module. Returns the name of the first
// missing export, or nullptr on success.
const char *resolve(void *game_assembly);

}  // namespace ltm::il2cpp
