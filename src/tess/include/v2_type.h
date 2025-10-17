#ifndef TESS_TYPE_H_V2
#define TESS_TYPE_H_V2

#include "alloc.h"
#include "array.h"
#include "hashmap.h"
#include "nodiscard.h"
#include "str.h"

typedef u32 tl_type_variable; // t0, t1, etc

// clang-format off
typedef struct {array_header; tl_type_variable *v;} tl_type_variable_array;
typedef struct {array_sized;  tl_type_variable *v;} tl_type_variable_sized;
typedef struct {array_header; struct tl_monotype const **v;} tl_monotype_array;
typedef struct {array_sized;  struct tl_monotype const**v;} tl_monotype_sized;
typedef struct {array_header; struct tl_polytype **v;} tl_polytype_array;
typedef struct {array_sized;  struct tl_polytype const**v;} tl_polytype_sized;
// clang-format on

typedef struct {
    str                    name;
    tl_type_variable_sized type_variables; // FIXME: move these into polytype
    str_sized              field_names;    // user types
    tl_monotype_sized      field_types;    // array, size = field_names.size
} tl_type_constructor_def;

typedef struct {
    tl_type_constructor_def const *def;
    tl_monotype_sized              args; // FIXME: what about polytype args?

} tl_type_constructor_inst;

typedef struct {
    allocator *alloc;       // manages lifetime of all type constructors
    hashmap   *definitions; // str => tl_type_constructor_def*
    hashmap   *instances;   // tl_type_constructor_def => tl_type_constructor_inst*
} tl_type_registry;

typedef struct tl_monotype {
    union {
        tl_type_variable                var;
        tl_type_constructor_inst const *cons_inst;
        struct {
            tl_monotype_sized xs;
            str_sized         fvs;
        } list;
    };
    enum { tl_var, tl_weak, tl_cons_inst, tl_list, tl_tuple } tag;
} tl_monotype;

typedef struct {
    tl_type_variable_sized quantifiers;
    union {
        tl_monotype const             *type;
        tl_type_constructor_def const *def;
    };
    enum { tl_poly_mono, tl_poly_def } tag;
} tl_polytype;

typedef struct {
    // A map of name : type assignments. Manages lifetimes of its elements.
    allocator *alloc;
    allocator *transient;
    hashmap   *map; // str => tl_polytype*
    int        verbose;
} tl_type_env;

typedef struct {
    tl_type_variable   parent;
    tl_monotype const *type; // null if unresolved
    u32                rank;
} tl_type_uf_node;

typedef struct {
    array_header;
    tl_type_uf_node *v;
} tl_type_subs;

// -- type constructor and registry --

nodiscard tl_type_registry    *tl_type_registry_create(allocator *, tl_type_subs *) mallocfun;
tl_type_constructor_def const *tl_type_constructor_def_create(tl_type_registry *, str name,
                                                              tl_type_variable_sized tvs, str_sized fields,
                                                              tl_monotype_sized) mallocfun;
tl_monotype const             *tl_type_registry_instantiate(tl_type_registry *, str, tl_monotype_sized);
tl_type_constructor_def const *tl_type_registry_get_def(tl_type_registry *, str);
tl_monotype const             *tl_type_registry_nil(tl_type_registry *);
tl_monotype const             *tl_type_registry_int(tl_type_registry *);
tl_monotype const             *tl_type_registry_float(tl_type_registry *);
tl_monotype const             *tl_type_registry_bool(tl_type_registry *);
tl_monotype const             *tl_type_registry_string(tl_type_registry *);
tl_monotype const             *tl_type_registry_ptr(tl_type_registry *, tl_monotype const *);

nodiscard tl_monotype const   *tl_type_constructor_instantiate(allocator *, tl_type_constructor_def const *,
                                                               tl_type_subs *);

// -- type environment --

nodiscard tl_type_env *tl_type_env_create(allocator *, allocator *) mallocfun;
void                   tl_type_env_insert(tl_type_env *, str, tl_polytype const *);
void                   tl_type_env_insert_mono(tl_type_env *, str, tl_monotype const *);
tl_polytype const     *tl_type_env_lookup(tl_type_env *, str);

typedef void (*missing_fv_cb)(void *, str fun, str var);
int  tl_type_env_check_missing_fvs(tl_type_env const *, missing_fv_cb, void *);

void tl_type_env_log(tl_type_env *);

// -- monotype --

nodiscard tl_monotype *tl_monotype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_weak(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_weak(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_list(allocator *, tl_monotype_sized);
nodiscard tl_monotype *tl_monotype_create_tuple(allocator *, tl_monotype_sized);
nodiscard tl_monotype *tl_monotype_create_arrow(allocator *, tl_monotype const *, tl_monotype const *);
nodiscard tl_monotype *tl_monotype_create_cons(allocator *, tl_type_constructor_inst const *) mallocfun;
nodiscard tl_monotype *tl_monotype_clone(allocator *, tl_monotype const *) mallocfun;
// u32                          tl_monotype_list_length(tl_monotype const *);
// nodiscard tl_monotype const *tl_monotype_list_copy(allocator *, tl_monotype const *);
// tl_monotype const           *tl_monotype_list_last(tl_monotype const *);
// void                         tl_monotype_list_concat(tl_monotype *, tl_monotype const *);

void      tl_monotype_substitute(allocator *, tl_monotype *, tl_type_subs const *, hashmap *);
void      tl_monotype_sort_fvs(tl_monotype *);
str_sized tl_monotype_fvs(tl_monotype const *);
void      tl_monotype_absorb_fvs(tl_monotype *, str_sized);
u64       tl_monotype_hash64(tl_monotype const *);
// u64       tl_monotype_list_hash64(u64, tl_monotype const *);

str tl_monotype_to_string(allocator *, tl_monotype const *);
int tl_monotype_is_nil(tl_monotype const *);
int tl_monotype_is_list(tl_monotype const *);
int tl_monotype_is_inst(tl_monotype const *);
int tl_monotype_is_tuple(tl_monotype const *);
int tl_monotype_is_concrete(tl_monotype const *);
int tl_monotype_is_concrete_no_arrow(tl_monotype const *); // constructed non-arrow type
int tl_monotype_is_arrow(tl_monotype const *);
int tl_monotype_is_ptr(tl_monotype const *);

// -- polytype --

nodiscard tl_polytype const *tl_polytype_absorb_mono(allocator *,
                                                     tl_monotype const *) mallocfun; // no clone
nodiscard tl_polytype const *tl_polytype_create_qv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype const *tl_polytype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype const *tl_polytype_create_weak(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype const *tl_polytype_create_def(allocator *, tl_type_constructor_def const *) mallocfun;
nodiscard tl_polytype const *tl_polytype_create_fresh_qv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype const *tl_polytype_create_fresh_tv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype const *tl_polytype_clone(allocator *, tl_polytype const *) mallocfun;

void                         tl_polytype_list_append(allocator *, tl_polytype *, tl_polytype const *);
nodiscard tl_monotype const *tl_polytype_instantiate(allocator *, tl_polytype const *, tl_type_subs *);
void                         tl_polytype_substitute(allocator *, tl_polytype *, tl_type_subs const *);
void                         tl_polytype_generalize(tl_polytype *, tl_type_env const *, tl_type_subs *);

// Warning: must use same allocator as that which created self's array.
void        tl_polytype_merge_quantifiers(allocator *, tl_polytype *, tl_polytype const *);

tl_polytype tl_polytype_wrap(tl_monotype const *);
str         tl_polytype_to_string(allocator *, tl_polytype const *);

int         tl_polytype_is_scheme(tl_polytype const *);
int         tl_polytype_is_concrete(tl_polytype const *);

// -- substitution --

typedef void (*type_error_cb_fun)(void *ctx, tl_monotype const *, tl_monotype const *);

nodiscard tl_type_subs *tl_type_subs_create(allocator *) mallocfun;
void                    tl_type_subs_destroy(allocator *, tl_type_subs **);

tl_type_variable        tl_type_subs_fresh(tl_type_subs *);

int  tl_type_subs_unify(tl_type_subs *, tl_type_variable, tl_monotype const *, type_error_cb_fun, void *);
int  tl_type_subs_unify_mono(tl_type_subs *, tl_monotype const *, tl_monotype const *, type_error_cb_fun,
                             void *);
void tl_type_subs_apply(tl_type_subs *, tl_type_env *);
void tl_type_subs_log(allocator *, tl_type_subs *);

// -- utilities --

u64                tl_monotype_sized_hash64(u64, tl_monotype_sized);
tl_monotype_sized  tl_monotype_sized_clone(allocator *, tl_monotype_sized);
tl_monotype const *tl_monotype_sized_last(tl_monotype_sized);

#endif
