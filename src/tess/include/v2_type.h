#ifndef TESS_TYPE_H_V2
#define TESS_TYPE_H_V2

#include "str.h"

typedef str tl_type_variable;

// clang-format off
typedef struct {array_header; tl_type_variable *v;} tl_type_variable_array;
typedef struct {array_sized;  tl_type_variable *v;} tl_type_variable_sized;
typedef struct {array_sized;  struct tl_monotype *v;} tl_monotype_sized;

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

#endif
