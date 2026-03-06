#ifndef TESS_TYPE_H_V2
#define TESS_TYPE_H_V2

#include "alloc.h"
#include "array.h"
#include "hashmap.h"
#include "nodiscard.h"
#include "str.h"

typedef u32 tl_type_variable; // t0, t1, etc

defarray(tl_type_variable_array, tl_type_variable);
defsized(tl_type_variable_sized, tl_type_variable);
defarray(tl_monotype_array, struct tl_monotype *);
defsized(tl_monotype_sized, struct tl_monotype *);
defarray(tl_monotype_ptr_array, struct tl_monotype **);
defarray(tl_polytype_array, struct tl_polytype *);
defsized(tl_polytype_sized, struct tl_polytype *);

// Integer sub-chain IDs for width ordering
enum {
    TL_INTEGER_SUBCHAIN_NONE            = 0, // not an integer type
    TL_INTEGER_SUBCHAIN_C_SIGNED        = 1, // CSignedChar < CShort < CInt < CLong < CLongLong
    TL_INTEGER_SUBCHAIN_C_UNSIGNED      = 2, // CUnsignedChar < CUnsignedShort < CUnsignedInt < CUnsignedLong < CUnsignedLongLong
    TL_INTEGER_SUBCHAIN_FIXED_SIGNED    = 3, // CInt8 < CInt16 < CInt32 < CInt64
    TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED  = 4, // CUInt8 < CUInt16 < CUInt32 < CUInt64
    TL_INTEGER_SUBCHAIN_CSIZE           = 5, // CSize (standalone)
    TL_INTEGER_SUBCHAIN_CPTRDIFF        = 6, // CPtrDiff (standalone)
    TL_INTEGER_SUBCHAIN_CCHAR           = 7, // CChar (standalone)
    TL_INTEGER_SUBCHAIN_FLOAT           = 8, // CFloat < CDouble < CLongDouble
};

typedef struct {
    str       name;
    str       generic_name;     // used to recover canonical name, eg Ptr_1 -> Ptr
    str_sized field_names;      // for user types
    int       is_variable_args; // non-zero if type allows a variable number of arguments, e.g. Union(...)
    int       is_signed_integer;      // unifies with other signed integers
    int       is_unsigned_integer;    // unifies with other unsigned integers
    int       is_narrow_integer;     // same-family unification OK, but preserve C ABI type (no canonicalization)
    int       is_float_convertible;   // the type is implicitly convertible to any other float type
    int       integer_subchain;       // 0 = not an integer, 1-7 = sub-chain ID (TL_INTEGER_SUBCHAIN_*)
    int       integer_width_rank;     // ordering within sub-chain (0 = narrowest), -1 if not integer
    str       c_type_name;            // C type string, e.g. "long long", empty if not a builtin
    char const *c_min_macro;          // C MIN macro, e.g. "INT_MIN", NULL if unsigned or non-integer
    char const *c_max_macro;          // C MAX macro, e.g. "INT_MAX", NULL if non-integer
    i64       integer_min_value;      // minimum value for compile-time range checking
    u64       integer_max_value;      // maximum value for compile-time range checking (u64 for unsigned)
    int       has_integer_range;      // non-zero if integer_min_value/max_value are valid
    str       module;                 // module name (e.g., "Math"), empty for main module
} tl_type_constructor_def;

// Trait function signature (stored in trait registry)
typedef struct {
    str name;  // function name (e.g., "eq")
    u8  arity; // parameter count
} tl_trait_sig;

defarray(tl_trait_sig_array, tl_trait_sig);

// Trait definition (stored in trait registry)
typedef struct {
    str                name;       // module-mangled trait name (e.g., Math__Sortable)
    str                generic_name; // unmangled name (Sortable)
    str_array          parents;    // parent trait names (module-mangled)
    tl_trait_sig_array sigs;       // function signatures
} tl_trait_def;

typedef struct {
    tl_type_constructor_def *def;
    tl_monotype_sized        args;
    // name of specialized instance, e.g. Point(a) => Point_0(Int).
    str special_name;
} tl_type_constructor_inst;

typedef struct tl_monotype {
    union {
        tl_type_variable          var;
        tl_type_constructor_inst *cons_inst;
        struct {
            tl_monotype_sized xs;
            str_sized         fvs;
        } list;

        i32 integer;
        str placeholder;
    };
    enum {
        tl_any,
        tl_ellipsis,
        tl_integer,
        tl_var,
        tl_weak,
        tl_weak_int_signed,
        tl_weak_int_unsigned,
        tl_weak_float,
        tl_cons_inst,
        tl_arrow,
        tl_tuple,
        tl_placeholder
    } tag;
    u32 visited_gen; // generation counter for cycle detection in traversals
    u32 hash_gen;    // generation when cached_hash was computed
    u64 cached_hash; // memoized hash value
} tl_monotype;

typedef struct tl_polytype {
    tl_type_variable_sized quantifiers;
    tl_monotype           *type;
} tl_polytype;

typedef struct {
    // A map of name : type assignments. Manages lifetimes of its elements.
    allocator *alloc;
    hashmap   *map; // str => tl_polytype*
    int        verbose;
} tl_type_env;

typedef struct {
    tl_type_variable parent;
    tl_monotype     *type; // null if unresolved
    u32              rank;
    i64              literal_value;     // for weak-int: the source literal's value
    int              has_literal_value; // non-zero if literal_value is set
} tl_type_uf_node;

defarray(tl_type_uf_node_array, tl_type_uf_node);

typedef struct {
    tl_type_uf_node_array data;
} tl_type_subs;

typedef struct {
    tl_monotype *left;
    tl_monotype *right;
} tl_monotype_pair;

defarray(tl_monotype_pair_array, tl_monotype_pair);

// -- type environment --

nodiscard tl_type_env *tl_type_env_create(allocator *) mallocfun;
void                   tl_type_env_insert(tl_type_env *, str, tl_polytype *);
void                   tl_type_env_insert_mono(tl_type_env *, str, tl_monotype *);
tl_polytype           *tl_type_env_lookup(tl_type_env *, str);

typedef void (*missing_fv_cb)(void *, str fun, str var);
int  tl_type_env_check_missing_fvs(tl_type_env *, missing_fv_cb, void *);
void tl_type_env_remove_unknown_symbols(tl_type_env *, hashmap *);

void tl_type_env_log(tl_type_env *);

// -- monotype --

nodiscard tl_monotype *tl_monotype_create_any(allocator *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_placeholder(allocator *, str) mallocfun;
nodiscard tl_monotype *tl_monotype_create_ellipsis(allocator *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_integer(allocator *, i32) mallocfun;
nodiscard tl_monotype *tl_monotype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_tv(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_weak(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_weak(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_weak_int_signed(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_weak_int_unsigned(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_weak_int_signed(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_weak_int_unsigned(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_weak_float(allocator *, tl_type_variable) mallocfun;
nodiscard tl_monotype *tl_monotype_create_fresh_weak_float(tl_type_subs *) mallocfun;
nodiscard tl_monotype *tl_monotype_create_list(allocator *, tl_monotype_sized);
nodiscard tl_monotype *tl_monotype_create_tuple(allocator *, tl_monotype_sized);
nodiscard tl_monotype *tl_monotype_create_cons(allocator *, tl_type_constructor_inst *) mallocfun;
nodiscard tl_monotype *tl_monotype_clone(allocator *, tl_monotype *) mallocfun;
nodiscard tl_polytype *tl_monotype_generalize(allocator *, tl_monotype *) mallocfun;

void                   tl_monotype_substitute(allocator *, tl_monotype *, tl_type_subs *, hashmap *);
void                   tl_monotype_sort_fvs(tl_monotype *);
str_sized              tl_monotype_fvs(tl_monotype *);
void                   tl_monotype_absorb_fvs(tl_monotype *, str_sized);
u64                    tl_monotype_hash64(tl_monotype *);
tl_monotype           *tl_monotype_arrow_result(tl_monotype *);

str                    tl_monotype_to_string(allocator *, tl_monotype *);
int                    tl_monotype_is_any(tl_monotype *);
int                    tl_monotype_is_integer(tl_monotype *);
int                    tl_monotype_is_void(tl_monotype *);
int                    tl_monotype_is_list(tl_monotype *);
int                    tl_monotype_is_inst(tl_monotype *);
int                    tl_monotype_is_inst_specialized(tl_monotype *);
int                    tl_monotype_is_inst_of(tl_monotype *, str);
int                    tl_monotype_is_enum(tl_monotype *);
int                    tl_monotype_is_tuple(tl_monotype *);
int                    tl_monotype_is_concrete(tl_monotype *);
int                    tl_monotype_arrow_is_concrete(tl_monotype *);
int                    tl_monotype_is_weak(tl_monotype *);
int                    tl_monotype_is_weak_int(tl_monotype *);
int                    tl_monotype_is_weak_int_signed(tl_monotype *);
int                    tl_monotype_is_weak_int_unsigned(tl_monotype *);
int                    tl_monotype_is_weak_float(tl_monotype *);
int                    tl_monotype_is_any_weak(tl_monotype *);
int                    tl_monotype_is_weak_deep(tl_monotype *);
int                    tl_monotype_sized_is_concrete(tl_monotype_sized);
int                    tl_monotype_is_concrete_inst(tl_monotype *); // concrete constructor instance (non-arrow, non-var)
int                    tl_monotype_is_concrete_no_weak(tl_monotype *);
int                    tl_monotype_sized_is_concrete_no_weak(tl_monotype_sized);
int                    tl_monotype_is_arrow(tl_monotype *);

// -- Pointer, Const, and CArray type queries --
//
// Ptr(T):       single-indirection pointer. Target via ptr_target().
// Const(T):     const wrapper. Transparent in unification; meaningful
//               only inside Ptr (Ptr(Const(T)) -> "const T*" in C).
// CArray(T,N):  fixed-size C array. Element via carray_element(),
//               count via carray_count(). Decays to Ptr(T) in values.
// has_ptr()     traverses Union variants; is_ptr() does not.
// ptr_target()  works on Ptr, PtrOrNull, and Union containing Ptr.

int                    tl_monotype_is_ptr(tl_monotype *);
int                    tl_monotype_is_const(tl_monotype *);
tl_monotype           *tl_monotype_const_target(tl_monotype *);
int                    tl_monotype_is_ptr_to_const(tl_monotype *);
int                    tl_monotype_is_carray(tl_monotype *);
tl_monotype           *tl_monotype_carray_element(tl_monotype *);
i32                    tl_monotype_carray_count(tl_monotype *);
int                    tl_monotype_is_unary(tl_monotype *);
int                    tl_monotype_arrow_has_arrow(tl_monotype *);
int                    tl_monotype_has_ptr(tl_monotype *);
int                    tl_monotype_is_union(tl_monotype *);
int                    tl_monotype_is_tv(tl_monotype *);
int                    tl_monotype_is_signed_integer(tl_monotype *);
int                    tl_monotype_is_unsigned_integer(tl_monotype *);
int                    tl_monotype_is_integer_convertible(tl_monotype *);
int                    tl_monotype_is_float_convertible(tl_monotype *);
void                   tl_monotype_set_signed_integer(tl_monotype *);
void                   tl_monotype_set_unsigned_integer(tl_monotype *);
void                   tl_monotype_set_float_convertible(tl_monotype *);

// Integer sub-chain queries
int                    tl_monotype_integer_subchain(tl_monotype *);        // returns 0 if not integer
int                    tl_monotype_integer_width_rank(tl_monotype *);      // returns -1 if not integer
int                    tl_monotype_compare_integer_width(tl_monotype *left, tl_monotype *right); // -1/0/1/2
int                    tl_monotype_same_integer_subchain(tl_monotype *left, tl_monotype *right);
int                    tl_monotype_integer_value_fits(tl_monotype *, i64);
char const            *tl_monotype_integer_c_min(tl_monotype *);  // e.g. "SHRT_MIN", NULL if unsigned
char const            *tl_monotype_integer_c_max(tl_monotype *);  // e.g. "SHRT_MAX"
int                    tl_monotype_is_unsigned_family(tl_monotype *); // unsigned subchains (2, 4, 5=CSize)
tl_monotype           *tl_monotype_unary_target(tl_monotype *);
tl_monotype           *tl_monotype_ptr_target(tl_monotype *);
void                   tl_monotype_ptr_set_target(tl_monotype *, tl_monotype *);
tl_monotype           *tl_monotype_arrow_args(tl_monotype *);
tl_monotype_sized      tl_monotype_arrow_get_args(tl_monotype *);
i32                    tl_monotype_type_constructor_field_index(tl_monotype *, str);
int                    tl_monotype_is_ptr_to_char(tl_monotype *);
int                    tl_monotype_is_ptr_to_tv(tl_monotype *);
i32                    tl_monotype_integer(tl_monotype *);
tl_type_variable       tl_monotype_tv(tl_monotype *);

u64                    tl_type_constructor_def_hash64(tl_type_constructor_def *);

// -- polytype --

nodiscard tl_polytype *tl_polytype_absorb_mono(allocator *,
                                               tl_monotype *) mallocfun; // no clone
nodiscard tl_polytype *tl_polytype_create(allocator *, tl_type_variable_sized, tl_monotype *) mallocfun;
nodiscard tl_polytype *tl_polytype_create_qv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype *tl_polytype_create_tv(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype *tl_polytype_create_weak(allocator *, tl_type_variable) mallocfun;
nodiscard tl_polytype *tl_polytype_create_fresh_qv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype *tl_polytype_create_fresh_tv(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype *tl_polytype_create_fresh_weak(allocator *, tl_type_subs *) mallocfun;
nodiscard tl_polytype *tl_polytype_clone(allocator *, tl_polytype *) mallocfun;
nodiscard tl_polytype *tl_polytype_clone_mono(allocator *, tl_monotype *) mallocfun;

void                   tl_polytype_list_append(allocator *, tl_polytype *, tl_polytype *);
nodiscard tl_monotype *tl_polytype_instantiate(allocator *, tl_polytype *, tl_type_subs *);
nodiscard tl_monotype *tl_polytype_instantiate_with(allocator *, tl_polytype *, tl_monotype_sized,
                                                    tl_type_subs *);
nodiscard tl_monotype *tl_polytype_specialize(allocator *, tl_polytype *, tl_monotype_sized);
void                   tl_polytype_substitute(allocator *, tl_polytype *, tl_type_subs *);
void                   tl_polytype_generalize(tl_polytype *, tl_type_env *, tl_type_subs *);

tl_monotype           *tl_polytype_concrete(tl_polytype *);

// Warning: must use same allocator as that which created self's array.
void        tl_polytype_merge_quantifiers(allocator *, tl_polytype *, tl_polytype *);
void        tl_polytype_merge_quantifiers_sized(allocator *, tl_polytype *, tl_polytype_sized);

tl_polytype tl_polytype_wrap(tl_monotype *);
str         tl_polytype_to_string(allocator *, tl_polytype *);

int         tl_polytype_is_scheme(tl_polytype *);
int         tl_polytype_is_concrete(tl_polytype *);
int         tl_polytype_is_concrete_inst(tl_polytype *);
int         tl_polytype_is_concrete_no_weak(tl_polytype *);
int         tl_polytype_is_type_constructor(tl_polytype *);
int         tl_polytype_is_nullary(tl_polytype *);
int         tl_polytype_is_unary(tl_polytype *);
int         tl_polytype_type_constructor_has_field(tl_polytype *, str);

// -- substitution --

typedef enum {
    TL_UNIFY_SYMMETRIC = 0, // legacy: no directional integer checking
    TL_UNIFY_DIRECTED  = 1, // left=expected, right=actual; widening OK
    TL_UNIFY_EXACT     = 2, // same concrete integer type required
} tl_unify_direction;

typedef void (*type_error_cb_fun)(void *ctx, tl_monotype *, tl_monotype *);

nodiscard tl_type_subs *tl_type_subs_create(allocator *) mallocfun;
void                    tl_type_subs_destroy(allocator *, tl_type_subs **);

tl_type_variable        tl_type_subs_fresh(tl_type_subs *);
void                    tl_type_subs_set_literal_value(tl_type_subs *, tl_type_variable, i64);
int                     tl_type_subs_monotype_occurs(tl_type_subs *, tl_type_variable, tl_monotype *);

int  tl_type_subs_unify_tv_mono(tl_type_subs *, tl_type_variable, tl_monotype *, type_error_cb_fun, void *,
                                hashmap **, tl_unify_direction, int tv_is_left);
int  tl_type_subs_unify_mono(tl_type_subs *, tl_monotype *, tl_monotype *, type_error_cb_fun, void *,
                             hashmap **, tl_unify_direction);
void tl_type_subs_apply(tl_type_subs *, tl_type_env *);
void tl_type_subs_default_weak_ints(tl_type_subs *, tl_monotype *int_type, tl_monotype *uint_type,
                                    tl_monotype *float_type);
void tl_type_subs_log(tl_type_subs *);

// -- utilities --

u64               tl_monotype_sized_hash64(u64, tl_monotype_sized);
tl_monotype_sized tl_monotype_sized_clone(allocator *, tl_monotype_sized, hashmap **);
tl_polytype_sized tl_monotype_sized_clone_poly(allocator *, tl_monotype_sized);
tl_monotype      *tl_monotype_sized_last(tl_monotype_sized);
tl_monotype_sized tl_polytype_sized_concrete(allocator *, tl_polytype_sized);
tl_polytype_sized tl_polytype_sized_clone(allocator *, tl_polytype_sized);

#endif
