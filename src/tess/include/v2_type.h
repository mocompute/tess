#ifndef TESS_TYPE_H_V2
#define TESS_TYPE_H_V2

#include "alloc.h"
#include "hashmap.h"
#include "nodiscard.h"
#include "str.h"

typedef u32 tl_type_variable;   // t0, t1, etc
typedef u32 tl_type_quantifier; // forall a. b. etc

// clang-format off
typedef struct {array_header; tl_type_variable *v;}                   tl_type_variable_array;
typedef struct {array_header; tl_type_quantifier *v;}                 tl_type_quantifier_array;
typedef struct {array_header; struct tl_monotype *v;}                 tl_monotype_array;
typedef struct {array_header; struct tl_type_v2 *v;}                  tl_type_v2_array;
typedef struct {str name; tl_type_quantifier_array vars;}             tl_type_constructor;
typedef struct {str name; tl_monotype_array        args;}             tl_type_constructor_inst;
// clang-format on

typedef struct {
    struct tl_monotype *lhs;
    struct tl_monotype *rhs;
    str_array           fvs;
} tl_type_v2_arrow;

typedef struct tl_monotype {
    union {
        tl_type_constructor_inst cons;
        tl_type_variable         var;
        tl_type_quantifier       quant; // because we use monotype struct for type schemes too
        tl_type_v2_arrow         arrow;
    };
    enum { tl_nil, tl_cons, tl_var, tl_quant, tl_arrow } tag;
} tl_monotype;

typedef struct {
    tl_type_quantifier_array quantifiers;
    tl_monotype              type;
} tl_type_scheme;

typedef struct tl_type_v2 {
    union {
        tl_monotype    mono;
        tl_type_scheme scheme;
    };
    enum { tl_mono, tl_scheme } tag;
} tl_type_v2;

// collect all free variables in type into new type variable set
void tl_type_v2_collect_free_variables(tl_type_variable_array *, tl_type_v2 const *);

// -- monotype --

tl_monotype            tl_monotype_init_nil();
tl_monotype            tl_monotype_init_tv(tl_type_variable);
tl_monotype            tl_monotype_init_quant(tl_type_quantifier);
tl_monotype            tl_monotype_init_arrow(tl_type_v2_arrow);
nodiscard tl_monotype  tl_monotype_alloc_arrow(allocator *, tl_monotype, tl_monotype);
void                   tl_monotype_dealloc(allocator *, tl_monotype *);
tl_monotype            tl_monotype_clone(allocator *, tl_monotype);
tl_monotype            tl_monotype_init_constructor_inst(tl_type_constructor_inst);
nodiscard tl_monotype *tl_monotype_create(allocator *, tl_monotype) mallocfun;
void                   tl_monotype_destroy(allocator *, tl_monotype **);
int                    tl_monotype_eq(tl_monotype, tl_monotype);
int                    tl_monotype_occurs(tl_monotype, tl_monotype);
u64                    tl_monotype_hash64(tl_monotype);
void                   tl_monotype_union_fv(tl_monotype *dst, tl_monotype src);

// -- type --

tl_type_v2 tl_type_init_mono(tl_monotype);
tl_type_v2 tl_type_init_scheme(tl_type_scheme);
tl_type_v2 tl_type_v2_clone(allocator *, tl_type_v2);

// -- substitution --

typedef struct {
    hashmap *map;
} tl_type_subs;

nodiscard tl_type_subs *tl_type_subs_create(allocator *) mallocfun;
void                    tl_type_subs_destroy(allocator *, tl_type_subs **);
void                    tl_type_subs_add(tl_type_subs *, tl_type_variable from, tl_monotype to);
tl_monotype            *tl_type_subs_get(tl_type_subs *, tl_type_variable);

// apply subs to a single type
void tl_type_v2_apply_subs(tl_type_v2 *, tl_type_subs const *);

// -- type_constructor --

tl_type_constructor_inst tl_type_constructor_instantiate(tl_type_constructor const *, tl_type_subs);

// -- context --

typedef struct {
    u32 next_var;
    u32 next_quant;
} tl_type_context;

tl_type_context  tl_type_context_empty();
tl_type_variable tl_type_context_new_variable(tl_type_context *);
tl_monotype      tl_type_context_new_quantifier(tl_type_context *);

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
u32                    tl_type_env_add(tl_type_env *, str, tl_type_v2);
tl_type_v2            *tl_type_env_lookup(tl_type_env *, str);
void                   tl_type_env_subs_apply(tl_type_env *, tl_type_subs const *);

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
