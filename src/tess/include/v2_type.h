#ifndef TESS_TYPE_H_V2
#define TESS_TYPE_H_V2

#include "alloc.h"
#include "nodiscard.h"
#include "str.h"

typedef str tl_type_variable;

// clang-format off
typedef struct {array_header; tl_type_variable *v;}     tl_type_variable_array;
typedef struct {array_sized;  tl_type_variable *v;}     tl_type_variable_sized;
typedef struct {array_header; struct tl_monotype *v;}   tl_monotype_array;
typedef struct {array_sized;  struct tl_monotype *v;}   tl_monotype_sized;
typedef struct {array_header; struct tl_type_v2 *v;}    tl_type_v2_array;
typedef struct {array_sized;  struct tl_type_v2 *v;}    tl_type_v2_sized;
typedef struct {str name; tl_type_variable_sized vars;} tl_type_constructor;
typedef struct {str name; tl_monotype_sized      args;} tl_type_constructor_inst;

typedef struct {struct tl_monotype *left; struct tl_monotype *right;} tl_type_arrow;
// clang-format on

typedef struct {
    union {
        tl_type_constructor_inst cons;
        tl_type_variable         var;
        tl_type_arrow            arrow;
    };
    enum { tl_cons, tl_var, tl_arrow } tag;
} tl_monotype;

typedef struct {
    tl_type_variable_sized quantifiers;
    struct tl_monotype    *type;
} tl_type_scheme;

typedef struct {
    union {
        tl_monotype    mono;
        tl_type_scheme scheme;
    };
    enum { tl_mono, tl_poly } tag;
} tl_type_v2;

// -- substitution

typedef struct {
    tl_monotype_array froms;
    tl_monotype_array tos;
} tl_type_subs;

nodiscard tl_type_subs *tl_type_subs_create(allocator *) mallocfun;
void                    tl_type_subs_destroy(tl_type_subs **);
u32                     tl_type_subs_add(tl_type_subs *, tl_monotype from, tl_monotype to);

// apply subs to base and return new set
nodiscard tl_type_subs *tl_type_subs_apply(tl_type_subs const *base, tl_type_subs const *subs);

// -- context --

typedef struct {
    u32 next_var;
} tl_type_context;

// -- environment --

typedef struct {
    // a set of name : type assignments
    str_array        names;
    tl_type_v2_array types;
} tl_type_env;

nodiscard tl_type_env *tl_type_env_create(allocator *) mallocfun;
void                   tl_type_env_destroy(tl_type_env **);
u32                    tl_type_env_add(tl_type_env *, str, tl_type_v2);

#endif
