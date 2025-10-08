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

typedef struct {
    struct tl_monotype *lhs;
    struct tl_monotype *rhs;
    str_array           fvs;
} tl_type_v2_arrow;

typedef struct tl_monotype {
    struct tl_monotype       *next; // a list/arrow of cons/vars
    str_sized                *fvs;  // if head of arrow (first element), fvs is a pointer to free variables
    tl_type_constructor_inst *cons; // if not null, a concrete type
    tl_type_variable          var;  // else a type variable
} tl_monotype;

typedef struct {
    tl_type_variable_sized quantifiers;
    tl_monotype            body;
} tl_polytype;

// -- type constructor and registry --

nodiscard tl_type_registry *tl_type_registry_create(allocator *) mallocfun;
tl_type_constructor_def    *tl_type_constructor_def_create(tl_type_registry *, str, u32) mallocfun;
tl_type_constructor_inst   *tl_type_registry_instantiate(tl_type_registry *, str,
                                                         tl_monotype const *) mallocfun;
tl_type_constructor_inst   *tl_type_registry_get(tl_type_registry *, str, tl_monotype const *);

tl_monotype                *tl_type_registry_create_type(tl_type_registry *, str, tl_monotype *);

// -- monotype --

u32          tl_monotype_list_length(tl_monotype const *);
tl_monotype *tl_monotype_list_copy(allocator *, tl_monotype const *);

//

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

// -- substitution --

typedef struct {
    tl_type_variable parent;
    tl_monotype     *type; // null if unresolved
    u32              rank;
} tl_type_uf_node;

typedef struct {
    array_header;
    tl_type_uf_node *v;
} tl_type_subs;

typedef void (*type_error_cb_fun)(void *ctx, tl_monotype *, tl_monotype *);

nodiscard tl_type_subs *tl_type_subs_create(allocator *) mallocfun;
void                    tl_type_subs_destroy(allocator *, tl_type_subs **);

tl_type_variable        tl_type_subs_fresh(tl_type_subs *);
int tl_type_subs_unify(allocator *, tl_type_subs *, tl_type_variable, tl_monotype *, type_error_cb_fun,
                       void *);
int tl_monotype_unify(allocator *, tl_type_subs *, tl_monotype *, tl_monotype *, type_error_cb_fun, void *);
void tl_type_subs_log(allocator *, tl_type_subs *);

// apply subs to a single type
void tl_type_v2_apply_subs(allocator *, tl_type_v2 *, tl_type_subs const *);

// -- type_constructor --

tl_type_constructor_inst tl_type_constructor_instantiate(tl_type_constructor const *, tl_type_subs);

// -- context --

typedef struct {
    u32 next_quant;
} tl_type_context;

tl_type_context tl_type_context_empty();
tl_monotype     tl_type_context_new_quantifier(tl_type_context *);

// -- environment --

typedef struct {
    // a set of name : type assignments
    str_array        names;
    tl_type_v2_array types;
    hashmap         *index; // name str => index
} tl_type_env;

nodiscard tl_type_env *tl_type_env_create(allocator *) mallocfun;
nodiscard tl_type_env *tl_type_env_copy(tl_type_env const *) mallocfun;
void                   tl_type_env_destroy(allocator *, tl_type_env **);
u32                    tl_type_env_add(tl_type_env *, str, tl_type_v2 const *);
u32                    tl_type_env_add_mono(tl_type_env *, str, tl_monotype *);
tl_type_v2            *tl_type_env_lookup(tl_type_env *, str);
void                   tl_type_env_erase(tl_type_env *, u32);
void                   tl_type_env_reindex(tl_type_env *);
void                   tl_type_env_subs_apply(allocator *, tl_type_env *, tl_type_subs const *);
int                    tl_type_subs_cleanup(allocator *, tl_type_subs *, tl_type_env *);

// -- strings --

str tl_type_subs_to_string(allocator *, tl_type_subs const *);
str tl_type_variable_to_string(allocator *, tl_type_variable const *);
str tl_type_quantifier_to_string(allocator *, tl_type_quantifier const *);
str tl_type_constructor_inst_to_string(allocator *, tl_type_constructor_inst const *);
str tl_type_arrow_to_string(allocator *, tl_type_v2_arrow const *);
str tl_monotype_to_string(allocator *, tl_monotype const *);
str tl_type_scheme_to_string(allocator *, tl_type_scheme const *);
str tl_type_v2_to_string(allocator *, tl_type_v2 const *);

#endif
