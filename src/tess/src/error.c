#include "error.h"

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *tl_error_tag_to_string(tl_error_tag tag) {
    static char const *const strings[] = {TESS_ERROR_TAG_LIST(MOS_TAG_STRING)};
    return strings[tag];
}

// User-facing error descriptions. Returns a human-readable prose string for
// error tags used in type-checker errors. Falls back to the raw tag name for
// tags that don't have a custom description (e.g. parser-only tags).

#define TESS_USER_ERROR_LIST(X)                                                                            \
    X(tl_err_type_error, "type mismatch")                                                                  \
    X(tl_err_unresolved_type, "unresolved type")                                                           \
    X(tl_err_unknown_type, "unknown type")                                                                 \
    X(tl_err_expected_type, "unknown type")                                                                \
    X(tl_err_const_violation, "cannot mutate const value")                                                 \
    X(tl_err_closure_escape, "closure escapes its scope")                                                  \
    X(tl_err_capture_without_alloc, "closure captures variable without allocator")                         \
    X(tl_err_alloc_missing_capture, "alloc expression missing capture")                                    \
    X(tl_err_capture_unlisted_var, "captured variable not listed")                                         \
    X(tl_err_capture_unused_var, "captured variable unused")                                               \
    X(tl_err_capture_not_in_scope, "captured variable not in scope")                                       \
    X(tl_err_alloc_expr_type_mismatch, "alloc expression type mismatch")                                   \
    X(tl_err_alloc_requires_import, "[[alloc]] requires #import <Alloc.tl>")                               \
    X(tl_err_trait_bound_not_satisfied, "trait bound not satisfied")                                       \
    X(tl_err_trait_circular_inheritance, "circular trait inheritance")                                     \
    X(tl_err_type_exists, "type already defined")                                                          \
    X(tl_err_function_exists, "function already defined")                                                  \
    X(tl_err_function_not_found, "function not found")                                                     \
    X(tl_err_field_not_found, "field not found")                                                           \
    X(tl_err_free_variable_not_found, "variable not found")                                                \
    X(tl_err_undeclared_reassignment, "undeclared variable")                                               \
    X(tl_err_arity, "wrong number of arguments")                                                           \
    X(tl_err_too_many_arguments, "too many arguments")                                                     \
    X(tl_err_no_main_function, "no main function")                                                         \
    X(tl_err_main_function_bad_type, "main function has wrong type")                                       \
    X(tl_err_expected_pointer, "expected pointer type")                                                    \
    X(tl_err_expected_integer, "expected integer type")                                                    \
    X(tl_err_unused_type_parameter, "unused type parameter")                                               \
    X(tl_err_not_compatible, "incompatible types")                                                         \
    X(tl_err_tagged_union_missing_case, "missing case in tagged union")                                    \
    X(tl_err_tagged_union_unknown_variant, "unknown variant")                                              \
    X(tl_err_try_requires_two_variant_union, "try requires a two-variant union")                           \
    X(tl_err_try_requires_single_field_variant, "try requires single-field variant")                       \
    X(tl_err_else_binding_requires_two_variant_union, "else binding requires a two-variant union")         \
    X(tl_err_void_else_requires_two_variant_union, "void-else requires a two-variant union")               \
    X(tl_err_nested_module_parent_not_found, "parent module not found")                                    \
    X(tl_err_auto_collapse_ambiguous, "ambiguous type (auto-collapse)")                                    \
    X(tl_err_invalid_format_spec, "invalid format specifier")                                              \
    X(tl_err_alias_source_not_found, "alias source not found")                                             \
    X(tl_err_alias_conflicts_with_module, "alias conflicts with module name")                              \
    X(tl_err_type_name_already_defined, "type name is already defined")                                    \
    X(tl_err_alias_already_defined, "alias already defined")                                               \
    X(tl_err_alias_self_alias, "cannot alias to self")                                                     \
    X(tl_err_alias_invalid_name, "invalid alias name")                                                     \
    X(tl_err_alias_source_is_alias, "alias source is itself an alias")                                     \
    X(tl_err_alias_source_is_main, "cannot alias main module")                                             \
    X(tl_err_unalias_not_found, "unalias target not found")                                                \
    X(tl_err_double_underscore_in_identifier, "double underscore in identifier")                           \
    X(tl_err_discarded_variant_union, "return value ignored: two-variant union must be used")

#define USER_ERROR_ENTRY(tag, user_str) [tag] = user_str,

char const *tl_error_tag_to_user_string(tl_error_tag t) {
    static char const *const strings[] = {TESS_USER_ERROR_LIST(USER_ERROR_ENTRY)};
    if ((unsigned)t < sizeof strings / sizeof *strings && strings[t]) return strings[t];
    return tl_error_tag_to_string(t);
}
