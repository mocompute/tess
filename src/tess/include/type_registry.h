#ifndef TESS_TYPE_REGISTRY_H
#define TESS_TYPE_REGISTRY_H

#include "ast.h"
#include "type.h"

typedef struct {
    allocator    *alloc; // manages lifetime of all type constructors
    allocator    *transient;
    tl_type_subs *subs;         // needed for instantiation
    hashmap      *definitions;  // str => tl_polytype*
    hashmap      *specialized;  // registry_key => tl_monotype*
    hashmap      *type_aliases; // str => polytype*
} tl_type_registry;

typedef struct {
    hashmap *memoize;

    // Type arguments discovered during recursive parsing
    hashmap *type_arguments; // str => tl_monotype*

    // Nodes which are deferred in first pass of parse.
    hashmap *deferred_parse; // str => ast_node*

    // Type names which are in progress of being parsed
    hashmap *in_progress; // str hset

    // When parsing an annotation, this is the node which is being annotated.
    ast_node const *annotation_target;
} tl_type_registry_parse_type_ctx;

typedef struct {
    u64 name_hash;
    u64 args_hash;
} registry_key;

typedef struct {
    tl_monotype *specialized;
    str          name;
    str          special_name;
    registry_key key;
    int          is_existing;
} tl_type_registry_specialize_ctx;

nodiscard tl_type_registry *tl_type_registry_create(allocator *, allocator *, tl_type_subs *) mallocfun;
tl_polytype *tl_type_constructor_def_create(tl_type_registry *, str name, tl_type_variable_sized tvs,
                                            str_sized fields, tl_monotype_sized) mallocfun;
tl_monotype *tl_type_registry_instantiate(tl_type_registry *, str);
tl_monotype *tl_type_registry_instantiate_with(tl_type_registry *, str, tl_monotype_sized);
tl_monotype *tl_type_registry_instantiate_union(tl_type_registry *, tl_monotype_sized);
tl_monotype *tl_type_registry_instantiate_carray(tl_type_registry *, tl_monotype *, i32);
tl_type_registry_specialize_ctx tl_type_registry_specialize_begin(tl_type_registry *, str, str,
                                                                  tl_monotype_sized);
tl_monotype *tl_type_registry_specialize_commit(tl_type_registry *, tl_type_registry_specialize_ctx);
tl_monotype *tl_type_registry_specialize(tl_type_registry *, str, str, tl_monotype_sized);
tl_monotype *tl_type_registry_get_cached_specialization(tl_type_registry *, str, tl_monotype_sized);
void         tl_type_registry_type_alias_insert(tl_type_registry *, str, tl_polytype *);
int          tl_type_registry_is_type_alias(tl_type_registry *, str);

tl_monotype *tl_type_registry_parse_type(tl_type_registry *, ast_node const *);
tl_monotype *tl_type_registry_parse_type_with_ctx(tl_type_registry *, ast_node const *,
                                                  tl_type_registry_parse_type_ctx *);
tl_monotype *tl_type_registry_parse_type_out_ctx(tl_type_registry *self, ast_node const *node,
                                                 allocator *alloc, hashmap *outer_type_arguments,
                                                 tl_type_registry_parse_type_ctx *out_ctx);

void         tl_type_registry_parse_type_ctx_init(allocator *, tl_type_registry_parse_type_ctx *,
                                                  hashmap *type_arguments);
void         tl_type_registry_parse_type_ctx_reset(tl_type_registry_parse_type_ctx *);

void         tl_type_registry_insert(tl_type_registry *, str, tl_polytype *);
void         tl_type_registry_insert_mono(tl_type_registry *, str, tl_monotype *);

tl_monotype *tl_type_registry_create_arrow(tl_type_registry *, tl_monotype *lhs, tl_monotype *rhs);

tl_monotype *tl_type_registry_nil(tl_type_registry *);
tl_monotype *tl_type_registry_int(tl_type_registry *);
tl_monotype *tl_type_registry_float(tl_type_registry *);
tl_monotype *tl_type_registry_bool(tl_type_registry *);
tl_monotype *tl_type_registry_string(tl_type_registry *);
tl_monotype *tl_type_registry_char(tl_type_registry *);
tl_monotype *tl_type_registry_ptr(tl_type_registry *, tl_monotype *);
tl_monotype *tl_type_registry_ptr_char(tl_type_registry *);
tl_monotype *tl_type_registry_ptr_any(tl_type_registry *);
tl_monotype *tl_type_registry_ptr_or_null(tl_type_registry *, tl_monotype *);
tl_monotype *tl_type_registry_type_literal(tl_type_registry *);
tl_polytype *tl_type_registry_get(tl_type_registry *, str);
tl_polytype *tl_type_registry_get_nullary(tl_type_registry *, str);
tl_polytype *tl_type_registry_get_unary(tl_type_registry *, str);
int          tl_type_registry_exists(tl_type_registry *, str);
int          tl_type_registry_is_nullary_type(tl_type_registry *, str);
int          tl_type_registry_is_unary_type(tl_type_registry *, str);

// TODO: rename these
nodiscard tl_monotype *tl_polytype_specialize_cons(allocator *, tl_polytype *, tl_monotype_sized,
                                                   tl_type_registry *, str);
tl_polytype           *tl_polytype_nil(allocator *, tl_type_registry *);
tl_polytype           *tl_polytype_bool(allocator *, tl_type_registry *);

#endif
