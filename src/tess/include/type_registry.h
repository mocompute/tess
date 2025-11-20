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
    hashmap *type_arguments;    // str => tl_polytype*
    hashmap *lexical_monotypes; // str => tl_monotype_pair
    int      is_value_context;
} tl_type_registry_parse_type_ctx;

nodiscard tl_type_registry *tl_type_registry_create(allocator *, allocator *, tl_type_subs *) mallocfun;
tl_polytype *tl_type_constructor_def_create(tl_type_registry *, str name, tl_type_variable_sized tvs,
                                            str_sized fields, tl_monotype_sized) mallocfun;
tl_monotype *tl_type_registry_instantiate(tl_type_registry *, str);
tl_monotype *tl_type_registry_instantiate_with(tl_type_registry *, str, tl_monotype_sized);
tl_monotype *tl_type_registry_instantiate_union(tl_type_registry *, tl_monotype_sized);
tl_monotype *tl_type_registry_specialize(tl_type_registry *, str, str, tl_monotype_sized);
tl_monotype *tl_type_registry_get_cached_specialization(tl_type_registry *, str, tl_monotype_sized);
void         tl_type_registry_type_alias_insert(tl_type_registry *, str, tl_polytype *);

tl_polytype *tl_type_registry_parse_type(tl_type_registry *, ast_node const *);
tl_polytype *tl_type_registry_parse_type_lexical(tl_type_registry *, ast_node const *, hashmap *);
tl_polytype *tl_type_registry_parse_type_out_ctx(tl_type_registry *, ast_node const *, allocator *,
                                                 tl_type_registry_parse_type_ctx *out);
hashmap     *tl_type_registry_parse_parameters(tl_type_registry *, allocator *, ast_node const *);

tl_monotype *tl_type_registry_nil(tl_type_registry *);
tl_monotype *tl_type_registry_int(tl_type_registry *);
tl_monotype *tl_type_registry_float(tl_type_registry *);
tl_monotype *tl_type_registry_bool(tl_type_registry *);
tl_monotype *tl_type_registry_string(tl_type_registry *);
tl_monotype *tl_type_registry_char(tl_type_registry *);
tl_monotype *tl_type_registry_ptr(tl_type_registry *, tl_monotype *);
tl_monotype *tl_type_registry_ptr_or_null(tl_type_registry *, tl_monotype *);
tl_monotype *tl_type_registry_type_literal(tl_type_registry *);
tl_polytype *tl_type_registry_get(tl_type_registry *, str);
tl_polytype *tl_type_registry_get_nullary(tl_type_registry *, str);
int          tl_type_registry_exists(tl_type_registry *, str);
int          tl_type_registry_is_nullary_type(tl_type_registry *, str);

// TODO: rename these
nodiscard tl_monotype *tl_polytype_specialize_cons(allocator *, tl_polytype *, tl_monotype_sized,
                                                   tl_type_registry *, str);
tl_polytype           *tl_polytype_nil(allocator *, tl_type_registry *);

#endif
