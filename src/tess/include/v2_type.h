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
// clang-format on

typedef struct {
    str name;
    u32 arity;
} tl_type_constructor_def;

typedef struct {
    tl_type_constructor_def const *def;
    struct tl_monotype            *args; // list of arguments
} tl_type_constructor_inst;

typedef struct {
    allocator *alloc;       // manages lifetime of all type constructors
    hashmap   *definitions; // map str => tl_type_constructor_def*
    hashmap   *instances;   // map tl_type_constructor_def => tl_type_constructor_inst*
} tl_type_registry;

typedef struct tl_monotype {
    struct tl_monotype *next;
    union {
        tl_type_variable          var;
        tl_type_constructor_inst *cons;
        struct {
            struct tl_monotype *head;
            str_sized          *fvs;
        } list;
    };
    enum { tl_var, tl_cons, tl_list } tag;
} tl_monotype;

typedef struct {
    tl_type_variable_sized quantifiers;
    tl_monotype           *type;
} tl_polytype;

typedef tl_polytype tl_type_v2;

typedef struct {
    // A map of name : type assignments. Manages lifetimes of its elements.
    allocator *alloc;
    allocator *transient;
    hashmap   *map; // str => tl_polytype*
    int        verbose;
} tl_type_env;

typedef struct {
    tl_type_variable parent;
    tl_monotype     *type; // null if unresolved
    u32              rank;
} tl_type_uf_node;

typedef struct {
    array_header;
    tl_type_uf_node *v;
} tl_type_subs;

// -- type constructor and registry --

nodiscard tl_type_registry *tl_type_registry_create(allocator *) mallocfun;
tl_type_constructor_def    *tl_type_constructor_def_create(tl_type_registry *, str, u32) mallocfun;
tl_type_constructor_inst   *tl_type_registry_instantiate(tl_type_registry *, str,
                                                         tl_monotype const *) mallocfun;
tl_type_constructor_inst   *tl_type_registry_get(tl_type_registry *, str, tl_monotype const *);
tl_monotype                *tl_type_registry_create_type(tl_type_registry *, str, tl_monotype *);
tl_polytype                *tl_type_registry_create_type_poly(tl_type_registry *, str, tl_monotype *);

// -- type environment --

nodiscard tl_type_env *tl_type_env_create(allocator *, allocator *) mallocfun;
void                   tl_type_env_insert(tl_type_env *, str, tl_polytype const *);
void                   tl_type_env_insert_mono(tl_type_env *, str, tl_monotype const *);
tl_polytype           *tl_type_env_lookup(tl_type_env *, str);

typedef void (*missing_fv_cb)(void *, str fun, str var);
int  tl_type_env_check_missing_fvs(tl_type_env const *, missing_fv_cb, void *);

void tl_type_env_log(tl_type_env *);

// -- monotype --

nodiscard tl_monotype *tl_monotype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_list(allocator *, tl_monotype *);
nodiscard tl_monotype *tl_monotype_create_arrow(allocator *, tl_monotype const *, tl_monotype const *);
nodiscard tl_monotype *tl_monotype_create_cons(allocator *, tl_type_constructor_inst *) mallocfun;
nodiscard tl_monotype *tl_monotype_clone(allocator *, tl_monotype const *) mallocfun;
nodiscard tl_monotype *tl_monotype_clone_list_element(allocator *, tl_monotype const *) mallocfun;
u32                    tl_monotype_list_length(tl_monotype const *);
tl_monotype           *tl_monotype_list_copy(allocator *, tl_monotype const *);
tl_monotype           *tl_monotype_list_last(tl_monotype *);
tl_monotype            tl_monotype_wrap_list_el(tl_monotype const *); // extracted element from list
void                   tl_monotype_substitute(allocator *, tl_monotype *, tl_type_subs const *, hashmap *);
void                   tl_monotype_sort_fvs(tl_monotype *);
str_sized              tl_monotype_fvs(tl_monotype const *);
void                   tl_monotype_absorb_fvs(allocator *, tl_monotype *, str_sized);

str                    tl_monotype_to_string(allocator *, tl_monotype const *);
int                    tl_monotype_is_nil(tl_monotype const *);
int                    tl_monotype_is_list(tl_monotype const *);
int                    tl_monotype_is_concrete_no_arrow(tl_monotype const *); // constructed non-arrow type
int                    tl_monotype_is_arrow(tl_monotype const *);

// -- polytype --

nodiscard tl_polytype *tl_polytype_absorb_mono(allocator *, tl_monotype *) mallocfun; // no clone
nodiscard tl_polytype *tl_polytype_create_qv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype *tl_polytype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype *tl_polytype_create_fresh_qv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype *tl_polytype_create_fresh_tv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype *tl_polytype_clone(allocator *, tl_polytype const *) mallocfun;
nodiscard tl_polytype *tl_polytype_clone_list_element(allocator *, tl_monotype const *) mallocfun;

void                   tl_polytype_list_append(allocator *, tl_polytype *, tl_polytype *);
nodiscard tl_monotype *tl_polytype_instantiate(allocator *, tl_polytype const *, tl_type_subs *);
void                   tl_polytype_substitute(allocator *, tl_polytype *, tl_type_subs const *);
void                   tl_polytype_generalize(tl_polytype *, tl_type_env const *, tl_type_subs const *);

tl_polytype            tl_polytype_wrap(tl_monotype *);
str                    tl_polytype_to_string(allocator *, tl_polytype const *);

int                    tl_polytype_is_scheme(tl_polytype const *);

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

// ---------------------
#if 0

// types are leaky: use an arena
nodiscard tl_monotype    *tl_monotype_create(allocator *, tl_monotype) mallocfun;
nodiscard tl_monotype    *tl_monotype_create_nil(allocator *) mallocfun;
nodiscard tl_monotype    *tl_monotype_create_arrow(allocator *, tl_monotype *, tl_monotype *) mallocfun;
nodiscard tl_monotype    *tl_monotype_clone(allocator *, tl_monotype const *) mallocfun;

tl_monotype               tl_monotype_init_nil();
tl_monotype               tl_monotype_init_tv(tl_type_variable);
tl_monotype               tl_monotype_init_arrow(tl_type_v2_arrow);
tl_monotype               tl_monotype_init_constructor_inst(tl_type_constructor_inst);
int                       tl_monotype_eq(tl_monotype const *, tl_monotype const *);
int                       tl_monotype_occurs(tl_monotype const *, tl_monotype const *);
int                       tl_monotype_occurs_tv(tl_type_variable, tl_monotype const *);
int                       tl_monotype_is_nil(tl_monotype const *);
int                       tl_monotype_is_monomorphic(tl_monotype const *);
u64                       tl_monotype_hash64(tl_monotype const *);
void                      tl_monotype_union_fv(tl_monotype *dst, tl_monotype const *src);

void                      tl_type_v2_arrow_sort_fvs(tl_type_v2_arrow *);
tl_monotype              *tl_type_v2_arrow_rightmost(tl_monotype *);

nodiscard tl_type_scheme *tl_type_scheme_create(allocator *, tl_type_scheme) mallocfun;

// -- type --

nodiscard tl_type_v2 *tl_type_v2_create(allocator *, tl_type_v2) mallocfun;
nodiscard tl_type_v2 *tl_type_alloc_mono(allocator *, tl_monotype *) mallocfun;
nodiscard tl_type_v2 *tl_type_alloc_scheme(allocator *, tl_type_scheme *) mallocfun;

tl_type_v2            tl_type_init_mono(tl_monotype *);
tl_type_v2            tl_type_init_scheme(tl_type_scheme *);
tl_type_v2           *tl_type_v2_clone(allocator *, tl_type_v2 const *);
int                   tl_type_v2_is_arrow(tl_type_v2 const *);
int                   tl_type_v2_is_scheme(tl_type_v2 const *);
int                   tl_type_v2_is_mono(tl_type_v2 const *);
int                   tl_type_v2_is_monomorphic(tl_type_v2 const *);
str_sized             tl_type_v2_free_variables(tl_type_v2 const *);

#endif

// -- environment --

#endif
