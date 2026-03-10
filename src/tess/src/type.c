#include "type.h"
#include "ast.h"
#include "type_registry.h"

#include "alloc.h"
#include "array.h"
#include "hash.h"
#include "hashmap.h"
#include "infer.h"
#include "str.h"
#include "util.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

// Thread-local storage compatibility
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

#define DEBUG_ENV             1
#define DEBUG_RECURSIVE_TYPES 0 // Trace recursive type parsing, deferral, and placeholder resolution

// Helper for cycle detection in recursive type traversals.
// Returns 1 if ptr was already visited (cycle), 0 otherwise.
// Always inserts ptr into the seen set.
static inline int seen_set_visit(hashmap **seen, void *ptr) {
    if (ptr_hset_contains(*seen, ptr)) return 1;
    ptr_hset_insert(seen, ptr);
    return 0;
}

static void                      dbg(tl_type_env *, char const *restrict fmt, ...);
static void                      make_unary_tc(tl_type_registry *, str);
static void                      make_variable_arity_tc(tl_type_registry *, str);
static tl_type_constructor_def  *make_tc_def(tl_type_registry *, str);
static tl_type_constructor_inst *make_tc_inst_args(tl_type_registry *, tl_type_constructor_def *,
                                                   tl_monotype_sized);
static void                      make_nullary_inst(tl_type_registry *, str);
static tl_polytype              *make_generic_inst(tl_type_registry *, tl_type_constructor_inst *,
                                                   tl_type_variable_sized);
static void                      make_carray(tl_type_registry *);

static void                      mark_signed_integer_type(tl_type_registry *, str);
static void                      mark_unsigned_integer_type(tl_type_registry *, str);
static void                      mark_narrow_integer_type(tl_type_registry *, str);
static void                      mark_float_type(tl_type_registry *, str);
static void                      mark_integer_subchain(tl_type_registry *, str, int subchain, int rank);

static int                       integer_value_fits(tl_type_constructor_def *, i64);

// Forward declarations for post-resolution fixup
static void generalize(tl_monotype *, tl_type_variable_array *, hashmap **);
static int  tl_type_subs_unify_tv_tv(tl_type_subs *, tl_type_variable, tl_type_variable, type_error_cb_fun,
                                     void *, hashmap **, tl_unify_direction);

// Check if a monotype tree contains a specific type variable (not via subs, just structurally).
static int monotype_contains_tv_(tl_monotype *self, tl_type_variable tv, hashmap **seen) {
    if (!self || seen_set_visit(seen, self)) return 0;
    switch (self->tag) {
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        return self->var == tv;
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:          return 0;
    case tl_cons_inst:         {
        forall(i, self->cons_inst->args) if (monotype_contains_tv_(self->cons_inst->args.v[i], tv,
                                                                   seen)) return 1;
        return 0;
    }
    case tl_arrow:
    case tl_tuple: {
        forall(i, self->list.xs) if (monotype_contains_tv_(self->list.xs.v[i], tv, seen)) return 1;
        return 0;
    }
    }
    return 0;
}

// Forward declaration for recursive parse_type_ helpers
static tl_monotype *tl_type_registry_parse_type_(tl_type_registry *, tl_type_registry_parse_type_ctx *,
                                                 ast_node const *);

// Return the child array of a monotype (cons_inst args, or arrow/tuple list).
static inline tl_monotype_sized tl_monotype_children(tl_monotype *self) {
    if (tl_cons_inst == self->tag) return self->cons_inst->args;
    return self->list.xs;
}

// ============================================================================
// Type registry: type constructor definitions and built-in types
// ============================================================================
//
// The type registry owns all type constructor definitions (structs, enums,
// built-in types).  tl_type_registry_create initializes built-in types (Void,
// Int, Bool, Float, Ptr, Const, Union, C integer/float types, CArray) and marks
// integer/float convertibility.  Factory functions create nullary, unary, and
// variable-arity constructors.

static THREAD_LOCAL allocator *transient_allocator; // initialized by tl_type_registry_create
static THREAD_LOCAL u32        substitute_gen = 1;  // generation counter for substitute cycle detection
static THREAD_LOCAL u32        hash_gen       = 1;  // generation counter for hash memoization
static THREAD_LOCAL tl_type_constructor_inst
  *canonical_signed; // canonical signed integer cons_inst (CLongLong)
static THREAD_LOCAL tl_type_constructor_inst
  *canonical_unsigned; // canonical unsigned integer cons_inst (CUnsignedLongLong)

#define HASH_CYCLE_STACK_CAP 32

typedef struct {
    u64 entries[HASH_CYCLE_STACK_CAP];
    u32 count;
} hash_cycle_stack;

static u64 *hash_cycle_find(hash_cycle_stack *s, u64 key) {
    for (u32 i = 0; i < s->count; i++) {
        if (s->entries[i] == key) return &s->entries[i];
    }
    return null;
}

static int hash_cycle_push(hash_cycle_stack *s, u64 key) {
    assert(s->count < HASH_CYCLE_STACK_CAP);
    if (s->count >= HASH_CYCLE_STACK_CAP) return 0;
    s->entries[s->count++] = key;
    return 1;
}

static void hash_cycle_pop(hash_cycle_stack *s) {
    assert(s->count > 0);
    if (s->count > 0) s->count--;
}

tl_type_registry *tl_type_registry_create(allocator *alloc, allocator *transient, tl_type_subs *subs) {
    tl_type_registry *self = alloc_malloc(alloc, sizeof *self);
    self->alloc            = alloc;
    self->transient        = transient;

    transient_allocator    = transient;

    self->subs             = subs;
    self->definitions      = map_new(self->alloc, str, tl_polytype *, 1024);       // key: str
    self->specialized      = map_create(self->alloc, sizeof(tl_monotype *), 1024); // key: registry_key
    self->type_aliases     = map_new(self->alloc, str, tl_polytype *, 1024);

    // Nullary built-in types with data-driven properties.
    // c_type: the C type string for transpilation (NULL if not a builtin C type)
    // c_min/c_max: C limit macros (NULL if not applicable)
    // min_val/max_val: compile-time range for integer_value_fits
    // has_range: non-zero if min_val/max_val are valid (0 for full-range types that always fit)
    static const struct {
        char       *name;
        u32         len;
        int         signed_int;
        int         unsigned_int;
        int         narrow;
        int         floating;
        int         subchain;
        int         rank;
        char const *c_type;
        char const *c_min;
        char const *c_max;
        i64         min_val;
        u64         max_val;
        int         has_range;
    } builtin_nullary[] = {
    // clang-format off
#define BUILTIN(n, s, u, w, f, sc, r, ct, cmin, cmax, mn, mx, hr) \
    {n, sizeof(n) - 1, s, u, w, f, sc, r, ct, cmin, cmax, mn, mx, hr}
      //                     name                  signed unsigned narrow float  subchain                          rank  c_type               c_min           c_max            min_val             max_val              has_range
      BUILTIN("Void",                                0,    0,      0,    0,     TL_INTEGER_SUBCHAIN_NONE,           -1,  "void",              NULL,           NULL,            0,                  0,                   0),
      BUILTIN("Bool",                                0,    0,      0,    0,     TL_INTEGER_SUBCHAIN_NONE,           -1,  "/*bool*/int",       NULL,           NULL,            0,                  0,                   0),
      BUILTIN("CChar",                               0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_CCHAR,           0,  "char",              "CHAR_MIN",     "CHAR_MAX",      -128,               127,                 1),
      BUILTIN("CUnsignedChar",                       0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_C_UNSIGNED,      0,  "unsigned char",     NULL,           "UCHAR_MAX",     0,                  255,                 1),
      BUILTIN("CSignedChar",                         1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_C_SIGNED,        0,  "signed char",       "SCHAR_MIN",    "SCHAR_MAX",     -128,               127,                 1),
      BUILTIN("CShort",                              1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_C_SIGNED,        1,  "short",             "SHRT_MIN",     "SHRT_MAX",      -32768,             32767,               1),
      BUILTIN("CUnsignedShort",                      0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_C_UNSIGNED,      1,  "unsigned short",    NULL,           "USHRT_MAX",     0,                  65535,               1),
      BUILTIN("CInt",                                1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_C_SIGNED,        2,  "int",               "INT_MIN",      "INT_MAX",       -2147483648LL,      2147483647ULL,       1),
      BUILTIN("CUnsignedInt",                        0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_C_UNSIGNED,      2,  "unsigned int",      NULL,           "UINT_MAX",      0,                  4294967295ULL,       1),
      BUILTIN("CLong",                               1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_C_SIGNED,        3,  "long",              "LONG_MIN",     "LONG_MAX",      0,                  0,                   0),
      BUILTIN("CUnsignedLong",                       0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_C_UNSIGNED,      3,  "unsigned long",     NULL,           "ULONG_MAX",     0,                  0,                   0),
      BUILTIN("CLongLong",                           1,    0,      0,    0,     TL_INTEGER_SUBCHAIN_C_SIGNED,        4,  "long long",         "LLONG_MIN",    "LLONG_MAX",     0,                  0,                   0),
      BUILTIN("CUnsignedLongLong",                   0,    1,      0,    0,     TL_INTEGER_SUBCHAIN_C_UNSIGNED,      4,  "unsigned long long",NULL,           "ULLONG_MAX",    0,                  0,                   0),
      BUILTIN("CSize",                               0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_CSIZE,           0,  "size_t",            NULL,           "SIZE_MAX",      0,                  0,                   0),
      BUILTIN("CPtrDiff",                            1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_CPTRDIFF,        0,  "ptrdiff_t",         "PTRDIFF_MIN",  "PTRDIFF_MAX",   0,                  0,                   0),
      BUILTIN("CInt8",                               1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_SIGNED,    0,  "int8_t",            "INT8_MIN",     "INT8_MAX",      -128,               127,                 1),
      BUILTIN("CUInt8",                              0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED,  0,  "uint8_t",           NULL,           "UINT8_MAX",     0,                  255,                 1),
      BUILTIN("CInt16",                              1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_SIGNED,    1,  "int16_t",           "INT16_MIN",    "INT16_MAX",     -32768,             32767,               1),
      BUILTIN("CUInt16",                             0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED,  1,  "uint16_t",          NULL,           "UINT16_MAX",    0,                  65535,               1),
      BUILTIN("CInt32",                              1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_SIGNED,    2,  "int32_t",           "INT32_MIN",    "INT32_MAX",     -2147483648LL,      2147483647ULL,       1),
      BUILTIN("CUInt32",                             0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED,  2,  "uint32_t",          NULL,           "UINT32_MAX",    0,                  4294967295ULL,       1),
      BUILTIN("CInt64",                              1,    0,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_SIGNED,    3,  "int64_t",           "INT64_MIN",    "INT64_MAX",     0,                  0,                   0),
      BUILTIN("CUInt64",                             0,    1,      1,    0,     TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED,  3,  "uint64_t",          NULL,           "UINT64_MAX",    0,                  0,                   0),
      BUILTIN("CFloat",                              0,    0,      0,    1,     TL_INTEGER_SUBCHAIN_FLOAT,           0,  "float",             NULL,           NULL,            0,                  0,                   0),
      BUILTIN("CDouble",                             0,    0,      0,    1,     TL_INTEGER_SUBCHAIN_FLOAT,           1,  "double",            NULL,           NULL,            0,                  0,                   0),
      BUILTIN("CLongDouble",                         0,    0,      0,    1,     TL_INTEGER_SUBCHAIN_FLOAT,           2,  "long double",       NULL,           NULL,            0,                  0,                   0),
#undef BUILTIN
    };
    // clang-format on
    for (u32 i = 0; i < sizeof builtin_nullary / sizeof builtin_nullary[0]; i++) {
        str name = (str){.big = {.buf = builtin_nullary[i].name, .len = builtin_nullary[i].len}};
        make_nullary_inst(self, name);
        if (builtin_nullary[i].signed_int) mark_signed_integer_type(self, name);
        if (builtin_nullary[i].unsigned_int) mark_unsigned_integer_type(self, name);
        if (builtin_nullary[i].narrow) mark_narrow_integer_type(self, name);
        if (builtin_nullary[i].floating) mark_float_type(self, name);
        if (builtin_nullary[i].subchain)
            mark_integer_subchain(self, name, builtin_nullary[i].subchain, builtin_nullary[i].rank);

        // Set data-driven type properties on the constructor def
        tl_polytype *poly = tl_type_registry_get(self, name);
        assert(poly && tl_monotype_is_inst(poly->type));
        tl_type_constructor_def *def = poly->type->cons_inst->def;
        if (builtin_nullary[i].c_type) def->c_type_name = str_init_static(builtin_nullary[i].c_type);
        def->c_min_macro       = builtin_nullary[i].c_min;
        def->c_max_macro       = builtin_nullary[i].c_max;
        def->integer_min_value = builtin_nullary[i].min_val;
        def->integer_max_value = builtin_nullary[i].max_val;
        def->has_integer_range = builtin_nullary[i].has_range;
    }

    // Cache canonical cons_inst pointers for integer family canonicalization during unification.
    // Int = CLongLong (signed canonical), UInt = CUnsignedLongLong (unsigned canonical).
    {
        tl_polytype *p = tl_type_registry_get(self, S("CLongLong"));
        assert(p && tl_monotype_is_inst(p->type));
        canonical_signed = p->type->cons_inst;

        p                = tl_type_registry_get(self, S("CUnsignedLongLong"));
        assert(p && tl_monotype_is_inst(p->type));
        canonical_unsigned = p->type->cons_inst;
    }

    // Non-nullary built-in type constructors
    make_unary_tc(self, S("Ptr"));
    make_unary_tc(self, S("Const"));
    make_variable_arity_tc(self, S("Union"));
    make_carray(self);

    return self;
}

static tl_polytype *tl_type_constructor_create_ext(tl_type_registry *self, str name, str generic_name,
                                                   tl_type_variable_sized type_variables,
                                                   str_sized field_names, tl_monotype_sized field_types) {

    tl_type_constructor_def *def   = make_tc_def(self, name);
    def->field_names               = field_names;
    def->generic_name              = generic_name;

    tl_type_constructor_inst *inst = make_tc_inst_args(self, def, field_types);
    tl_polytype              *poly = make_generic_inst(self, inst, type_variables);

    return poly;
}

static tl_polytype *tl_type_constructor_create(tl_type_registry *self, str name,
                                               tl_type_variable_sized type_variables, str_sized field_names,
                                               tl_monotype_sized field_types) {

    return tl_type_constructor_create_ext(self, name, name, type_variables, field_names, field_types);
}

void tl_type_registry_insert(tl_type_registry *self, str name, tl_polytype *poly) {
#if DEBUG_RECURSIVE_TYPES
    str tmp = tl_polytype_to_string(transient_allocator, poly);
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] registry_insert: %s : %s\n", str_cstr(&name), str_cstr(&tmp));
    str_deinit(transient_allocator, &tmp);
#endif
    str_map_set_ptr(&self->definitions, name, poly);
}

void tl_type_registry_insert_mono(tl_type_registry *self, str name, tl_monotype *mono) {
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] registry_insert_mono: %s\n", str_cstr(&name));
#endif
    tl_polytype *poly = tl_monotype_generalize(self->alloc, mono);
    str_map_set_ptr(&self->definitions, name, poly);
}

tl_polytype *tl_type_constructor_def_create_ext(tl_type_registry *self, str name, str generic_name,
                                                tl_type_variable_sized type_variables,
                                                str_sized field_names, tl_monotype_sized field_types) {

    tl_polytype *poly =
      tl_type_constructor_create_ext(self, name, generic_name, type_variables, field_names, field_types);

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] constructor_def_create: %s\n", str_cstr(&name));
#endif
    tl_type_registry_insert(self, name, poly);
    return poly;
}

tl_polytype *tl_type_constructor_def_create(tl_type_registry *self, str name,
                                            tl_type_variable_sized type_variables, str_sized field_names,
                                            tl_monotype_sized field_types) {

    return tl_type_constructor_def_create_ext(self, name, name, type_variables, field_names, field_types);
}

static void make_unary_tc(tl_type_registry *self, str name) {
    str_sized              empty = {0};

    tl_type_variable_array unary = {.alloc = self->alloc};
    tl_type_variable       tv    = tl_type_subs_fresh(self->subs);
    array_push(unary, tv);
    tl_monotype      *tv_type = tl_monotype_create_tv(self->alloc, tv);
    tl_monotype_array mt_arr  = {.alloc = self->alloc};
    array_push(mt_arr, tv_type);

    tl_type_constructor_def_create(self, name, (tl_type_variable_sized)sized_all(unary), empty,
                                   (tl_monotype_sized)sized_all(mt_arr));
}

static void make_variable_arity_tc(tl_type_registry *self, str name) {
    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};
    tl_monotype_sized      empty_mt = {0};

    tl_polytype           *poly     = tl_type_constructor_def_create(self, name, empty_tv, empty, empty_mt);
    poly->type->cons_inst->def->is_variable_args = 1;
}

static tl_type_constructor_def *make_tc_def(tl_type_registry *self, str name) {
    tl_type_constructor_def *def = alloc_malloc(self->alloc, sizeof *def);
    def->name                    = str_copy(self->alloc, name);
    def->generic_name            = str_copy(self->alloc, name);
    def->field_names             = (str_sized){0};
    def->is_variable_args        = 0;
    def->is_signed_integer       = 0;
    def->is_unsigned_integer     = 0;
    def->is_narrow_integer       = 0;
    def->is_float_convertible    = 0;
    def->integer_subchain        = TL_INTEGER_SUBCHAIN_NONE;
    def->integer_width_rank      = -1;
    def->c_type_name             = str_empty();
    def->c_min_macro             = NULL;
    def->c_max_macro             = NULL;
    def->integer_min_value       = 0;
    def->integer_max_value       = 0;
    def->has_integer_range       = 0;
    def->module                  = str_empty();
    return def;
}

static tl_type_constructor_inst *make_tc_inst_args(tl_type_registry *self, tl_type_constructor_def *def,
                                                   tl_monotype_sized args) {
    tl_type_constructor_inst *inst = alloc_malloc(self->alloc, sizeof *inst);
    inst->def                      = def;
    inst->args                     = args;
    inst->special_name             = str_empty();
    return inst;
}

static tl_polytype *make_generic_inst(tl_type_registry *self, tl_type_constructor_inst *inst,
                                      tl_type_variable_sized quantifiers) {
    tl_monotype *mono = tl_monotype_create_cons(self->alloc, inst);
    tl_polytype *poly = tl_polytype_absorb_mono(self->alloc, mono);
    poly->quantifiers = quantifiers;
    return poly;
}

static void make_nullary_inst(tl_type_registry *self, str name) {

    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};
    tl_monotype_sized      empty_mt = {0};
    str                    blank    = str_empty();

    tl_type_constructor_def_create(self, name, empty_tv, empty, empty_mt);
    tl_type_registry_specialize(self, name, blank, empty_mt);
}

static void mark_signed_integer_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    tl_monotype_set_signed_integer(poly->type);
}

static void mark_unsigned_integer_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    tl_monotype_set_unsigned_integer(poly->type);
}

static void mark_narrow_integer_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    poly->type->cons_inst->def->is_narrow_integer = 1;
}

static void mark_float_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    tl_monotype_set_float_convertible(poly->type);
}

static void mark_integer_subchain(tl_type_registry *self, str name, int subchain, int rank) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    poly->type->cons_inst->def->integer_subchain   = subchain;
    poly->type->cons_inst->def->integer_width_rank = rank;
}

static void make_carray(tl_type_registry *self) {
    str       name                = S("CArray");
    str_sized field_names         = {.size = 2, .v = alloc_malloc(self->alloc, 2 * sizeof(str))};
    field_names.v[0]              = str_init(self->alloc, "type");
    field_names.v[1]              = str_init(self->alloc, "count");

    tl_monotype_sized field_types = {.size = 2, .v = alloc_malloc(self->alloc, 2 * sizeof(tl_monotype *))};
    field_types.v[0]              = tl_monotype_create_fresh_tv(self->subs);
    field_types.v[1]              = tl_type_registry_int(self);

    tl_type_variable_sized tvs    = {.size = 1, .v = alloc_malloc(self->alloc, sizeof(tl_type_variable))};
    tvs.v[0]                      = tl_monotype_tv(field_types.v[0]);

    tl_type_constructor_def_create(self, name, tvs, field_names, field_types);
}

// ============================================================================
// Type registry: instantiation, specialization, and queries
// ============================================================================
//
// Instantiation creates fresh monotype instances of registered constructors.
// Specialization (begin/commit two-phase API) handles recursive types by
// inserting a placeholder before resolving fields.  Query functions look up
// types by name and provide convenience accessors for common types (Int, Str,
// Ptr, etc.).

tl_monotype *tl_type_registry_instantiate(tl_type_registry *self, str name) {
    if (str_eq(name, S("Union"))) fatal("runtime error");
    if (str_eq(name, S("Ptr"))) fatal("runtime error");
    tl_monotype *type = null;
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) return null;

    type = tl_polytype_instantiate(self->alloc, poly, self->subs);

    return type;
}

tl_monotype *tl_type_registry_instantiate_with(tl_type_registry *self, str name, tl_monotype_sized args) {
    if (str_eq(name, S("Union"))) fatal("runtime error");
    if (str_eq(name, S("Ptr"))) fatal("runtime error");
    tl_monotype *type = null;
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) return null;

    type = tl_polytype_instantiate_with(self->alloc, poly, args, self->subs);

    return type;
}

tl_monotype *tl_type_registry_instantiate_union(tl_type_registry *self, tl_monotype_sized args) {
    str          name = S("Union");
    tl_monotype *type = null;

    if (!args.size) fatal("runtime error");

    tl_polytype *poly = str_map_get_ptr(self->definitions, name);
    if (!poly) fatal("runtime error");

    type = tl_polytype_instantiate(self->alloc, poly, self->subs);
    assert(tl_monotype_is_inst(type));
    type->cons_inst->args = args;

    return type;
}

tl_monotype *tl_type_registry_instantiate_carray(tl_type_registry *self, tl_monotype *type, i32 count) {
    if (count < 0) return null;
    tl_polytype *poly          = str_map_get_ptr(self->definitions, S("CArray"));
    tl_monotype *inst          = tl_polytype_instantiate(self->alloc, poly, self->subs);
    inst->cons_inst->args.v[0] = type;
    inst->cons_inst->args.v[1] = tl_monotype_create_integer(self->alloc, count);
    return inst;
}

// Compute args hash for the specialization cache, ignoring free variable annotations on arrow types.
// Arrow fvs are a closure implementation detail, not part of structural type identity. Without this,
// the same type constructor (e.g. Processor[(Int)->Int]) can't be found in the cache when referenced
// from contexts with different fvs annotations (function return type vs struct construction site).
static u64 registry_args_hash(tl_monotype_sized args) {
    // Temporarily strip fvs from arrow args before hashing.
    str_sized saved_fvs[16];
    int       n_saved = 0;
    forall(i, args) {
        if (tl_monotype_is_arrow(args.v[i]) && args.v[i]->list.fvs.size > 0 && n_saved < 16) {
            saved_fvs[n_saved++] = args.v[i]->list.fvs;
            args.v[i]->list.fvs  = (str_sized){0};
            args.v[i]->hash_gen  = 0; // Invalidate cached hash so it gets recomputed without fvs
        }
    }
    u64 hash = tl_monotype_sized_hash64(0, args);
    // Restore fvs
    int ri = 0;
    forall(i, args) {
        if (tl_monotype_is_arrow(args.v[i]) && ri < n_saved && saved_fvs[ri].size > 0) {
            args.v[i]->list.fvs = saved_fvs[ri++];
            args.v[i]->hash_gen = 0; // Invalidate again so normal hash recomputes with fvs
        }
    }
    return hash;
}

tl_monotype *tl_type_registry_get_cached_specialization(tl_type_registry *self, str name,
                                                        tl_monotype_sized args) {
    registry_key key = {.name_hash = str_hash64(name), .args_hash = registry_args_hash(args)};
    tl_monotype *out = map_get_ptr(self->specialized, &key, sizeof key);
    return out;
}

void tl_type_registry_type_alias_insert(tl_type_registry *self, str name, tl_polytype *type) {
    str_map_set_ptr(&self->type_aliases, name, type);
}

int tl_type_registry_is_type_alias(tl_type_registry *self, str name) {
    return str_map_contains(self->type_aliases, name);
}

int tl_type_registry_is_nullary_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    return tl_polytype_is_nullary(poly);
}

int tl_type_registry_is_unary_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    return tl_polytype_is_unary(poly);
}

int tl_polytype_is_nullary(tl_polytype *poly) {
    if (!poly) return 0;
    if (tl_polytype_is_scheme(poly)) return 0;
    if (!tl_monotype_is_inst(poly->type)) return 0;
    if (poly->quantifiers.size) return 0;
    return 1;
}

int tl_polytype_is_unary(tl_polytype *poly) {
    if (!poly) return 0;
    return tl_monotype_is_unary(poly->type);
}

int tl_monotype_is_unary(tl_monotype *mono) {
    if (!mono) return 0;
    if (!tl_monotype_is_inst(mono)) return 0;
    if (1 != mono->cons_inst->args.size) return 0;
    return 1;
}

tl_type_registry_specialize_ctx tl_type_registry_specialize_begin(tl_type_registry *self, str name,
                                                                  str               special_name,
                                                                  tl_monotype_sized args) {

    // Don't specialize nullary type constructors: they should always exist with their canonical generic
    // name, because they're not generic.
    if (!args.size) return (tl_type_registry_specialize_ctx){.specialized = null};

    tl_monotype *type = null;
    registry_key key  = {.name_hash = str_hash64(name), .args_hash = registry_args_hash(args)};
    if ((type = map_get_ptr(self->specialized, &key, sizeof key)))
        return (tl_type_registry_specialize_ctx){.specialized = type, .is_existing = 1};

    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) return (tl_type_registry_specialize_ctx){.specialized = null};

    assert(tl_monotype_is_inst(poly->type));
    tl_type_constructor_def *def = poly->type->cons_inst->def;
    if (!def->is_variable_args) {
        u32 arity = poly->type->cons_inst->args.size;
        if (args.size != arity) fatal("runtime error");
    }

    type = tl_polytype_specialize_cons(self->alloc, poly, args, self, special_name);

    return (tl_type_registry_specialize_ctx){
      .specialized = type, .name = name, .special_name = special_name, .key = key};
}

tl_monotype *tl_type_registry_specialize_commit(tl_type_registry               *self,
                                                tl_type_registry_specialize_ctx ctx) {

    if (!str_is_empty(ctx.special_name)) {
        // Important: don't cache in specialised table if there is no special_name
        map_set_ptr(&self->specialized, &ctx.key, sizeof ctx.key, ctx.specialized);
        tl_type_constructor_def_create_ext(self, ctx.special_name, ctx.name, (tl_type_variable_sized){0},
                                           ctx.specialized->cons_inst->def->field_names,
                                           ctx.specialized->cons_inst->args);
    }

    return ctx.specialized;
}

tl_monotype *tl_type_registry_specialize(tl_type_registry *self, str name, str special_name,
                                         tl_monotype_sized args) {

    tl_type_registry_specialize_ctx ctx = tl_type_registry_specialize_begin(self, name, special_name, args);
    return tl_type_registry_specialize_commit(self, ctx);
}

tl_polytype *tl_type_registry_get(tl_type_registry *self, str name) {
    tl_polytype *poly = str_map_get_ptr(self->type_aliases, name);
    if (!poly) poly = str_map_get_ptr(self->definitions, name);
    return poly;
}

tl_polytype *tl_type_registry_get_nullary(tl_type_registry *self, str name) {
    tl_polytype *out = tl_type_registry_get(self, name);
    if (!tl_polytype_is_nullary(out)) out = null;
    return out;
}

tl_polytype *tl_type_registry_get_unary(tl_type_registry *self, str name) {
    tl_polytype *out = tl_type_registry_get(self, name);
    if (!tl_polytype_is_unary(out)) out = null;
    return out;
}

int tl_type_registry_exists(tl_type_registry *self, str name) {
    return str_map_contains(self->type_aliases, name) || str_map_contains(self->definitions, name);
}

tl_monotype *tl_type_registry_nil(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Void"));
}
tl_monotype *tl_type_registry_int(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Int"));
}
tl_monotype *tl_type_registry_uint(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CUnsignedLongLong"));
}
tl_monotype *tl_type_registry_csize(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CSize"));
}
tl_monotype *tl_type_registry_cptrdiff(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CPtrDiff"));
}
tl_monotype *tl_type_registry_float(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CDouble"));
}
tl_monotype *tl_type_registry_bool(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Bool"));
}
tl_monotype *tl_type_registry_char(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CChar"));
}

tl_monotype *tl_type_registry_str(tl_type_registry *self) {
    tl_monotype *mono = tl_type_registry_instantiate(self, S("Str"));
    if (!mono) fatal("attempt to create a string without standard library <Str.tl>");
    return mono;
}

tl_polytype *tl_polytype_nil(allocator *alloc, tl_type_registry *self) {
    tl_monotype *nil = tl_type_registry_nil(self);
    return tl_polytype_absorb_mono(alloc, nil);
}

tl_polytype *tl_polytype_bool(allocator *alloc, tl_type_registry *self) {
    tl_monotype *bool_ = tl_type_registry_bool(self);
    return tl_polytype_absorb_mono(alloc, bool_);
}

tl_monotype *tl_type_registry_ptr(tl_type_registry *self, tl_monotype *arg) {
    tl_monotype **arr = alloc_malloc(self->alloc, sizeof(void *));
    arr[0]            = arg;
    tl_monotype *out  = tl_type_registry_specialize(self, S("Ptr"), str_empty(),
                                                    (tl_monotype_sized){
                                                      .size = 1,
                                                      .v    = arr,
                                                   });
    assert(out);
    return out;
}

tl_monotype *tl_type_registry_ptr_any(tl_type_registry *self) {
    tl_monotype *any = tl_monotype_create_any(self->alloc);
    return tl_type_registry_ptr(self, any);
}
tl_monotype *tl_type_registry_ptr_char(tl_type_registry *self) {
    tl_monotype *cchar = tl_type_registry_char(self);
    return tl_type_registry_ptr(self, cchar);
}

// ============================================================================
// Type parsing from AST annotations
// ============================================================================
//
// Converts AST type annotation nodes into tl_monotype values.  Handles symbols
// (nullary types, type arguments), integer literals, named applications (generic
// constructors like Array(T)), arrows, and tuples.  Supports implicit type
// variable sugar, deferred parsing for mutually recursive types, and memoization
// for repeated sub-expressions.

void tl_type_registry_add_type_argument(tl_type_registry *self, str name, tl_monotype *mono,
                                        hashmap **type_arguments) {
    (void)self;
    str_map_set_ptr(type_arguments, name, mono);
}

tl_monotype *tl_type_registry_add_fresh_type_argument(tl_type_registry *self, str name,
                                                      hashmap **type_arguments) {
    // Type arguments will be looked up in a value context, so they should not be wrapped in a literal.
    tl_monotype *mono = tl_monotype_create_fresh_tv(self->subs);
    tl_type_registry_add_type_argument(self, name, mono, type_arguments);
    return mono;
}

static tl_monotype *add_type_argument(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                      str name) {
    // create a fresh type argument with the given name
    tl_monotype *mono = tl_type_registry_add_fresh_type_argument(self, name, &ctx->type_arguments);
    return mono;
}

static int is_type_argument(tl_type_registry_parse_type_ctx const *ctx, str name) {
    return str_map_contains(ctx->type_arguments, name);
}

static tl_monotype *get_type_argument(tl_type_registry_parse_type_ctx const *ctx, str name) {
    return str_map_get_ptr(ctx->type_arguments, name);
}

static tl_monotype *parse_type_specials(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                        ast_node const *node) {

    (void)ctx;
    tl_monotype *mono = null;

    if (ast_node_is_symbol(node)) {
        str name = ast_node_str(node);
        if (str_eq(name, S("any"))) mono = tl_monotype_create_any(self->alloc);
        else if (str_eq(name, S("..."))) mono = tl_monotype_create_ellipsis(self->alloc);
        else if (str_eq(name, S("Type"))) {
            fatal("var: Type is no longer valid syntax.");
        }
    }

    return mono;
}

static tl_monotype *defer_parse(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx, str name,
                                tl_monotype_sized type_args) {
    if (str_hset_contains(ctx->in_progress, name) || str_map_contains(ctx->deferred_parse, name) ||
        (!is_type_argument(ctx, name) && !tl_type_registry_get(self, name))) {

        // target cannot be parsed yet: create a placeholder type for it: Ptr(any)

        tl_monotype *placeholder = str_map_get_ptr(ctx->deferred_parse, name);
        if (!placeholder) {
            placeholder = tl_monotype_create_placeholder(self->alloc, name);
            str_map_set_ptr(&ctx->deferred_parse, name, placeholder);

            tl_monotype_sized *stored = alloc_malloc(self->alloc, sizeof(tl_monotype_sized));
            *stored                   = type_args;
            str_map_set_ptr(&ctx->deferred_type_args, name, stored);

            str_map_set(&ctx->deferred_source_names, name, &ctx->current_utd_name);
            tl_type_variable_sized *sq = alloc_malloc(self->alloc, sizeof(tl_type_variable_sized));
            *sq                        = ctx->current_utd_quantifiers;
            str_map_set_ptr(&ctx->deferred_source_quantifiers, name, sq);
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] defer_parse: created placeholder for '%s' : %p\n",
                    str_cstr(&name), (void *)placeholder);
#endif
        } else {
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] defer_parse: reusing placeholder for '%s' : %p\n",
                    str_cstr(&name), (void *)placeholder);
#endif
        }
        return placeholder;
    }
    return null;
}

// Resolve deferred placeholders for (mutually) recursive types.
//
// During field parsing, references to not-yet-defined types become placeholder nodes.  Once the
// target poly is available, we resolve each placeholder by instantiating the poly with the type
// args captured at the defer site, then mutating the placeholder in-place so all existing
// references see the resolved type.
static void resolve_deferred_placeholders(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                          tl_polytype *poly, str name) {
#if !DEBUG_RECURSIVE_TYPES
    (void)name;
#endif
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type: '%s' resolving %zu deferred placeholders\n",
            str_cstr(&name), (size_t)map_size(ctx->deferred_parse));
#endif
    str_array keys = str_map_keys(self->transient, ctx->deferred_parse);
    forall(i, keys) {
        tl_monotype *placeholder = str_map_get_ptr(ctx->deferred_parse, keys.v[i]);

        if (tl_monotype_is_inst(poly->type)) {
            if (str_eq(poly->type->cons_inst->def->generic_name, keys.v[i])) {
                // mutate placeholder to resolved type: dependent types which retained the
                // placeholder pointer will automatically get resolved type
#if DEBUG_RECURSIVE_TYPES
                {
                    str tmp = tl_monotype_to_string(self->transient, poly->type);
                    fprintf(stderr,
                            "[DEBUG_RECURSIVE_TYPES] parse_type: defer resolve '%s' "
                            "placeholder=%p poly->type=%p: %s\n",
                            str_cstr(&keys.v[i]), (void *)placeholder, (void *)poly->type, str_cstr(&tmp));
                    str_deinit(self->transient, &tmp);
                }
#endif
                tl_monotype_sized *stored_args = str_map_get_ptr(ctx->deferred_type_args, keys.v[i]);
                if (stored_args && stored_args->size > 0) {
                    tl_monotype *instantiated =
                      tl_polytype_instantiate_with(self->alloc, poly, *stored_args, self->subs);
                    *placeholder = *instantiated;
                } else {
                    *placeholder = *poly->type;
                }
#if DEBUG_RECURSIVE_TYPES
                {
                    str tmp = tl_monotype_to_string(self->transient, placeholder);
                    fprintf(stderr,
                            "[DEBUG_RECURSIVE_TYPES] parse_type: AFTER resolve '%s' "
                            "placeholder=%p: %s\n",
                            str_cstr(&keys.v[i]), (void *)placeholder, str_cstr(&tmp));
                    str_deinit(self->transient, &tmp);
                }
#endif
                str_map_erase(ctx->deferred_parse, keys.v[i]);

                // Post-resolution fixup: unify orphaned type variables

                // A. Unify stored_args vars with current poly's quantifiers
                //    (fixes the target type's parse polytype)
                if (stored_args && stored_args->size > 0) {
                    hashmap *useen = hset_create(transient_allocator, 64);
                    for (u32 j = 0; j < stored_args->size && j < poly->quantifiers.size; j++) {
                        if (tl_monotype_is_tv(stored_args->v[j])) {
                            hset_reset(useen);
                            tl_type_subs_unify_tv_tv(self->subs, stored_args->v[j]->var,
                                                     poly->quantifiers.v[j], null, null, &useen,
                                                     TL_UNIFY_SYMMETRIC);
                        }
                    }
                }

                // B. Unify source parse-phase quantifiers with source registry quantifiers
                //    (fixes the source type's registry polytype)
                str                    *source_name_p = str_map_get(ctx->deferred_source_names, keys.v[i]);
                str                     source_name   = source_name_p ? *source_name_p : str_empty();
                tl_type_variable_sized *source_q =
                  str_map_get_ptr(ctx->deferred_source_quantifiers, keys.v[i]);
                tl_polytype *source_poly =
                  !str_is_empty(source_name) ? tl_type_registry_get(self, source_name) : null;
                if (source_poly && source_q) {
                    hashmap *useen = hset_create(transient_allocator, 64);
                    for (u32 j = 0; j < source_q->size && j < source_poly->quantifiers.size; j++) {
                        hset_reset(useen);
                        tl_type_subs_unify_tv_tv(self->subs, source_q->v[j], source_poly->quantifiers.v[j],
                                                 null, null, &useen, TL_UNIFY_SYMMETRIC);
                    }
                }

                // C. Substitute + re-generalize current poly
                tl_monotype_substitute(self->alloc, poly->type, self->subs, null);
                {
                    tl_type_variable_array quant = {.alloc = self->alloc};
                    hashmap               *gseen = hset_create(transient_allocator, 64);
                    generalize(poly->type, &quant, &gseen);
                    poly->quantifiers = (tl_type_variable_sized)array_sized(quant);
                }

                // D. Clone + substitute + re-generalize + re-insert source registry poly
                if (source_poly) {
                    tl_monotype *clone = tl_monotype_clone(self->alloc, source_poly->type);
                    tl_monotype_substitute(self->alloc, clone, self->subs, null);
                    tl_polytype *new_poly = tl_monotype_generalize(self->alloc, clone);
                    tl_type_registry_insert(self, source_name, new_poly);
                }

                // E. Fix sibling types that share the resolved placeholder.
                //
                // The placeholder was shared (by pointer identity) with other types that
                // instantiated the source type (e.g. __MyList__Union_ instantiated
                // MyList__Cons and inherited its Ptr[placeholder]).  After in-place
                // resolution the placeholder content uses stored_args variables (e.g. t43)
                // which are NOT quantified in those sibling types.  We need to unify those
                // stale variables with each sibling's own quantifiers and re-insert.
                if (stored_args && stored_args->size > 0) {
                    str_array all_defs = str_map_keys(self->transient, self->definitions);
                    forall(di, all_defs) {
                        // Skip the target type and the source type (already handled)
                        if (str_eq(all_defs.v[di], keys.v[i])) continue;
                        if (!str_is_empty(source_name) && str_eq(all_defs.v[di], source_name)) continue;

                        tl_polytype *def_poly = str_map_get_ptr(self->definitions, all_defs.v[di]);
                        if (!def_poly) continue;

                        // Only consider types with the same number of quantifiers as stored_args
                        // (they share the same type parameter structure).
                        if (def_poly->quantifiers.size != stored_args->size) continue;

                        // Check if this type contains any stored_args variable as a free variable
                        int      has_stale = 0;
                        hashmap *cseen     = hset_create(transient_allocator, 64);
                        for (u32 j = 0; j < stored_args->size; j++) {
                            if (!tl_monotype_is_tv(stored_args->v[j])) continue;
                            // Check if this var is NOT already one of the quantifiers
                            int is_quantified = 0;
                            for (u32 q = 0; q < def_poly->quantifiers.size; q++) {
                                if (def_poly->quantifiers.v[q] == stored_args->v[j]->var) {
                                    is_quantified = 1;
                                    break;
                                }
                            }
                            if (is_quantified) continue;

                            hset_reset(cseen);
                            if (monotype_contains_tv_(def_poly->type, stored_args->v[j]->var, &cseen)) {
                                has_stale = 1;
                                break;
                            }
                        }
                        if (!has_stale) continue;

                        // Unify stale stored_args variables with this type's quantifiers
                        hashmap *useen = hset_create(transient_allocator, 64);
                        for (u32 j = 0; j < stored_args->size && j < def_poly->quantifiers.size; j++) {
                            if (tl_monotype_is_tv(stored_args->v[j])) {
                                hset_reset(useen);
                                tl_type_subs_unify_tv_tv(self->subs, stored_args->v[j]->var,
                                                         def_poly->quantifiers.v[j], null, null, &useen,
                                                         TL_UNIFY_SYMMETRIC);
                            }
                        }

                        // Clone + substitute + re-generalize + re-insert
                        tl_monotype *dclone = tl_monotype_clone(self->alloc, def_poly->type);
                        tl_monotype_substitute(self->alloc, dclone, self->subs, null);
                        tl_polytype *new_def = tl_monotype_generalize(self->alloc, dclone);
                        tl_type_registry_insert(self, all_defs.v[di], new_def);
#if DEBUG_RECURSIVE_TYPES
                        {
                            str tmp = tl_polytype_to_string(self->transient, new_def);
                            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] step E: re-inserted '%s': %s\n",
                                    str_cstr(&all_defs.v[di]), str_cstr(&tmp));
                        }
#endif
                    }
                }
            }
        } else {
            fatal("logic error");
        }
    }
}

// Parse a named function application as a type: Array(Float), Map(Int, Int), Ptr(T), etc.
static tl_monotype *parse_type_nfa(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                   ast_node const *node) {
    str name = ast_node_str(node->named_application.name);

    // Recursive types: check for indirection through Ptr (or any unary type) and defer it
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type_: nfa '%s' n_type_args=%u\n", str_cstr(&name),
            node->named_application.n_type_arguments);
#endif

    if (tl_type_registry_is_unary_type(self, name)) {

        // Note: returning null is valid, because this function may be called to try to parse things
        // which look like type literals for a while, but are actually type constructors.

        if (1 != node->named_application.n_type_arguments) return null;
        ast_node const *target      = node->named_application.type_arguments[0];

        ast_node const *target_name = null;
        if (ast_node_is_symbol(target)) target_name = target;
        else if (ast_node_is_nfa(target)) target_name = target->named_application.name;
        else return null;

        str          target_name_str = ast_node_str(target_name);

        tl_monotype *result          = null;
        // If the target is a symbol and not a utd in_progress, it must either be a nullary type or it
        // is sugar for a type variable, e.g a function in stdlib.tl which returns a `Ptr(T)`.
        if (ast_node_is_symbol(target)) {
            if (str_hset_contains(ctx->in_progress, target_name_str)) {
                // Self-referential: target is currently being parsed, must defer.
#if DEBUG_RECURSIVE_TYPES
                fprintf(stderr,
                        "[DEBUG_RECURSIVE_TYPES] parse_type_: unary '%s' target '%s' is in_progress, "
                        "deferring\n",
                        str_cstr(&name), str_cstr(&target_name_str));
#endif
                result = defer_parse(self, ctx, target_name_str, (tl_monotype_sized){0});
            } else {
                // Try to parse normally (handles specials like `any`, nullary types, type args).
                // If that fails, the target may be a forward reference in a multi-type recursion
                // cycle (e.g. A->B->C->A when parsing A and B isn't registered yet) — defer it.
                tl_monotype *parsed = tl_type_registry_parse_type_(self, ctx, target);
                if (!parsed && map_size(ctx->in_progress)) {
                    // Inside a UTD parse and target is unknown — forward reference in a
                    // multi-type recursion cycle (e.g. A->B->C->A, parsing A, B not yet registered).
                    result = defer_parse(self, ctx, target_name_str, (tl_monotype_sized){0});
                }
            }
        } else {
            // Maybe defer non-symbol target: parse type args in caller's context before deferring
            tl_monotype_array deferred_args = {.alloc = self->alloc};
            for (u32 j = 0; j < target->named_application.n_type_arguments; j++) {
                tl_monotype *arg =
                  tl_type_registry_parse_type_(self, ctx, target->named_application.type_arguments[j]);
                if (!arg) return null;
                array_push(deferred_args, arg);
            }
            result = defer_parse(self, ctx, target_name_str, (tl_monotype_sized)array_sized(deferred_args));
        }

        if (result) {
            tl_polytype *unary = tl_type_registry_get_unary(self, name);
            assert(tl_monotype_is_inst(unary->type));
            tl_monotype_sized args = {.v = alloc_malloc(self->alloc, sizeof(tl_monotype *)), .size = 1};
            args.v[0]              = result;
            return tl_polytype_instantiate_with(self->alloc, unary, args, self->subs);
        }
    }

    tl_polytype *type_constructor = tl_type_registry_get(self, name);
    if (!type_constructor) return null;
    if (!tl_monotype_is_inst(type_constructor->type)) return null;
    if (tl_polytype_is_nullary(type_constructor)) return null;

    tl_monotype_array args  = {.alloc = self->alloc};
    ast_node_sized    nodes = {.size = node->named_application.n_type_arguments,
                               .v    = node->named_application.type_arguments};

    forall(i, nodes) {
        tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, nodes.v[i]);

        // If the type constructor argument produces nothing, and it's a symbol, it's sugar for a type
        // variable - not a type argument. Otherwise, it's an error
        if (!mono) {
            return null;
        }
        // clang-format off
        { tl_monotype *_t = mono; array_push(args, _t); }
        // clang-format on
    }

    // Note: special case for parsing a Union(a, b, ...)
    if (str_eq(name, S("Union"))) {
        return tl_type_registry_instantiate_union(self, (tl_monotype_sized)array_sized(args));
    } else if (str_eq(name, S("CArray")) && args.size == 2) {
        // CArray(T, N) has 1 quantifier but 2 args (element type + integer count)
        return tl_type_registry_instantiate_carray(self, args.v[0], tl_monotype_integer(args.v[1]));
    } else if (type_constructor->quantifiers.size != args.size) {
        // Arg count doesn't match quantifier count — not a valid type instantiation
        return null;
    } else {
        return tl_polytype_instantiate_with(self->alloc, type_constructor,
                                            (tl_monotype_sized)array_sized(args), self->subs);
    }
}

// Parse a user type definition (struct/enum/tagged union).
static tl_monotype *parse_type_utd(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                   ast_node const *node) {
    str        name             = node->user_type_def.name->symbol.name;
    u32        n_type_arguments = node->user_type_def.n_type_arguments;
    u32        n_fields         = node->user_type_def.n_fields;
    ast_node **type_arguments   = node->user_type_def.type_arguments;
    ast_node **fields           = node->user_type_def.field_names;
    ast_node **annotations      = node->user_type_def.field_annotations;

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type_: utd ENTER '%s' n_type_args=%u n_fields=%u\n",
            str_cstr(&name), n_type_arguments, n_fields);
#endif

    // Add name to in_progress
    str_hset_insert(&ctx->in_progress, name);

    tl_monotype *result = null;

    // Add type arguments to parse context, and save for the type constructor.
    tl_type_variable_array type_argument_tvs = {.alloc = self->alloc};

    for (u32 i = 0, n = n_type_arguments; i < n; ++i) {
        assert(ast_node_is_symbol(type_arguments[i]));
        tl_monotype *mono = mono = add_type_argument(self, ctx, ast_node_str(type_arguments[i]));
        array_push(type_argument_tvs, mono->var);
    }

    ctx->current_utd_name         = name;
    ctx->current_utd_quantifiers  = (tl_type_variable_sized)array_sized(type_argument_tvs);

    str_array         field_names = {.alloc = self->alloc};
    tl_monotype_array field_types = {.alloc = self->alloc};
    array_reserve(field_names, n_fields);
    array_reserve(field_types, n_fields);
    for (u32 i = 0; i < n_fields; ++i) {
        assert(ast_node_is_symbol(fields[i]));
        array_push(field_names, fields[i]->symbol.name);

        // Note: enum types have no annotations
        if (annotations) {
            tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, annotations[i]);
#if DEBUG_RECURSIVE_TYPES
            if (!mono) {
                str tmp = v2_ast_node_to_string(self->transient, annotations[i]);
                fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type: failed to parse field '%s'\n",
                        str_cstr(&tmp));
            }
#endif
            if (!mono) goto done; // TODO: better error

            array_push(field_types, mono);
        }
    }

    {
        tl_polytype *poly = tl_type_constructor_create(
          self, name, (tl_type_variable_sized)array_sized(type_argument_tvs),
          (str_sized)array_sized(field_names), (tl_monotype_sized)array_sized(field_types));

#if DEBUG_RECURSIVE_TYPES
        {
            str tmp = tl_polytype_to_string(self->transient, poly);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type: registered '%s' poly->type=%p: %s\n",
                    str_cstr(&name), (void *)poly->type, str_cstr(&tmp));
            // Print field type pointers for tracing placeholder identity
            if (tl_monotype_is_inst(poly->type)) {
                tl_monotype_sized fargs = poly->type->cons_inst->args;
                for (u32 fi = 0; fi < fargs.size; ++fi) {
                    str fs = tl_monotype_to_string(self->transient, fargs.v[fi]);
                    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   field[%u] = %p: %s\n", fi,
                            (void *)fargs.v[fi], str_cstr(&fs));
                }
            }
            str_deinit(self->transient, &tmp);
        }
#endif

        if (map_size(ctx->deferred_parse)) resolve_deferred_placeholders(self, ctx, poly, name);

        result = tl_polytype_instantiate(self->alloc, poly, self->subs);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type: success '%s'\n", str_cstr(&name));
#endif
    }

done:
#if DEBUG_RECURSIVE_TYPES
    if (!result) fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] parse_type: error '%s'\n", str_cstr(&name));
#endif
    str_hset_remove(ctx->in_progress, name);
    return result;
}

static tl_monotype *tl_type_registry_parse_type_(tl_type_registry                *self,
                                                 tl_type_registry_parse_type_ctx *ctx,
                                                 ast_node const                  *node) {

    tl_monotype *result = map_get_ptr(ctx->memoize, &node, sizeof(ast_node *));
    if (result) return result;

    if (ast_node_is_symbol(node)) {

        // is it a special: any, ..., Type
        result = parse_type_specials(self, ctx, node);
        if (result) goto top_success;

        // or is it a nullary literal: Int, Float, etc
        str          name = ast_node_str(node);
        tl_polytype *poly = tl_type_registry_get_nullary(self, name);
        if (poly) {
            result = poly->type;
            goto top_success;
        }

        // or else is it a type argument previously defined?
        result = get_type_argument(ctx, name);
        if (result) {
            // Note: type arguments will need to be handled in a context-sensitive way by the caller.
            goto top_success;
        }

        // or else is it an annotated symbol? E.g. `count: Int`
        if (node->symbol.annotation) {
            ast_node const *save   = ctx->annotation_target;
            ctx->annotation_target = node;
            result                 = tl_type_registry_parse_type_(self, ctx, node->symbol.annotation);
            ctx->annotation_target = save;
        }

        goto top_success;
    }

    else if (ast_i64 == node->tag) {
        result = tl_monotype_create_integer(self->alloc, (i32)node->i64.val);
        goto top_success;
    }

    else if (ast_node_is_nfa(node)) {
        result = parse_type_nfa(self, ctx, node);
    }

    else if (ast_node_is_arrow(node)) {
        // An arrow annotation: left is always a tuple in our system, even with 0 or 1 params, and right is
        // any type. Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)
        assert(ast_node_is_tuple(node->arrow.left));

        // Register explicit type parameters on the arrow (e.g. [T] in `[T](v: T) -> Option[T]`).
        // These are set by create_variant_constructor when the UTD has type params.
        // Skip any already registered (e.g. loaded via load_toplevel_type_params in Phase 2).
        for (u32 tp = 0; tp < node->arrow.n_type_parameters; tp++) {
            assert(ast_node_is_symbol(node->arrow.type_parameters[tp]));
            str tp_name = ast_node_str(node->arrow.type_parameters[tp]);
            if (!is_type_argument(ctx, tp_name)) add_type_argument(self, ctx, tp_name);
        }

        tl_monotype_array args  = {.alloc = self->alloc};
        ast_node_sized    nodes = ast_node_sized_from_ast_array_const(node->arrow.left);
        forall(i, nodes) {
            tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, nodes.v[i]);
            if (!mono) {
                result = null;
                goto top_success;
            }
            // clang-format off
            { tl_monotype *_t = mono; array_push(args, _t); }
            // clang-format on
        }
        tl_monotype *left_mono =
          tl_monotype_create_tuple(self->alloc, (tl_monotype_sized)array_sized(args));

        tl_monotype *right_mono = tl_type_registry_parse_type_(self, ctx, node->arrow.right);
        if (!right_mono) {
            result = null;
            goto top_success;
        }
        tl_monotype *arrow = tl_type_registry_create_arrow(self, left_mono, right_mono);
        result             = arrow;
    }

    else if (ast_node_is_utd(node)) {
        result = parse_type_utd(self, ctx, node);
    }

top_success:
    map_set_ptr(&ctx->memoize, &node, sizeof(ast_node *), result);
    return result;
}

void tl_type_registry_parse_type_ctx_init(allocator *alloc, tl_type_registry_parse_type_ctx *ctx,
                                          hashmap *type_arguments) {
    *ctx = (tl_type_registry_parse_type_ctx){
      .memoize               = map_new(alloc, ast_node *, tl_monotype *, 64),
      .type_arguments        = type_arguments ? type_arguments : map_new(alloc, str, tl_monotype *, 64),
      .deferred_parse        = map_new(alloc, str, ast_node *, 64),
      .deferred_type_args    = map_new(alloc, str, tl_monotype_sized *, 64),
      .deferred_source_names = map_new(alloc, str, str, 64),
      .deferred_source_quantifiers = map_new(alloc, str, tl_type_variable_sized *, 64),
      .in_progress                 = hset_create(alloc, 64),
    };
}

void tl_type_registry_parse_type_ctx_reinit(tl_type_registry_parse_type_ctx *ctx, hashmap *type_arguments) {
    // Full reset of all fields for reuse.  Contrast with _reset, which preserves memoize and
    // deferred maps for single-pass UTD fixups.
    map_reset(ctx->memoize);
    ctx->type_arguments = type_arguments;
    map_reset(ctx->deferred_parse);
    map_reset(ctx->deferred_type_args);
    map_reset(ctx->deferred_source_names);
    map_reset(ctx->deferred_source_quantifiers);
    hset_reset(ctx->in_progress);
    ctx->current_utd_name        = str_empty();
    ctx->current_utd_quantifiers = (tl_type_variable_sized){0};
    ctx->annotation_target       = null;
}

void tl_type_registry_parse_type_ctx_reset(tl_type_registry_parse_type_ctx *ctx) {
    // Note: does not reset deferred_parse (or its associated maps deferred_type_args,
    // deferred_source_names, deferred_source_quantifiers), which is the whole point: to support
    // single-pass fixups for mutually recursive types.

    // Note: does not reset memoize
    map_reset(ctx->type_arguments);
    hset_reset(ctx->in_progress);
    ctx->annotation_target = null;
}

tl_monotype *tl_type_registry_parse_type(tl_type_registry *self, ast_node const *node) {
    // Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)
    tl_type_registry_parse_type_ctx ctx;
    tl_type_registry_parse_type_ctx_init(self->transient, &ctx, null);

    tl_monotype *result = tl_type_registry_parse_type_(self, &ctx, node);

    return result;
}

tl_monotype *tl_type_registry_parse_type_with_ctx(tl_type_registry *self, ast_node const *node,
                                                  tl_type_registry_parse_type_ctx *ctx) {
    // Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)

    tl_monotype *result = tl_type_registry_parse_type_(self, ctx, node);

#if DEBUG_RECURSIVE_TYPES
    if (!result) {
        str tmp = v2_ast_node_to_string(self->transient, node);
        fprintf(stderr,
                "[DEBUG_RECURSIVE_TYPES] parse_type_with_ctx: failed to parse '%s' "
                "(ctx->type_arguments has %zu keys)\n",
                str_cstr(&tmp), (size_t)map_size(ctx->type_arguments));
    }
#endif

    return result;
}

tl_monotype *tl_type_registry_parse_type_out_ctx(tl_type_registry *self, ast_node const *node,
                                                 allocator *alloc, hashmap *outer_type_arguments,
                                                 tl_type_registry_parse_type_ctx *out_ctx) {
    // alloc is used to allocate the parse context's buffers/maps, and the struct is copied into
    // out_ctx.
    assert(out_ctx);
    tl_type_registry_parse_type_ctx ctx;
    tl_type_registry_parse_type_ctx_init(alloc, &ctx, outer_type_arguments);
    tl_monotype *result = tl_type_registry_parse_type_(self, &ctx, node);
    *out_ctx            = ctx;

    return result;
}

tl_monotype *tl_type_registry_parse_type_except_integer_literal(tl_type_registry *self,
                                                                ast_node const   *node) {
    tl_monotype *res = tl_type_registry_parse_type(self, node);
    if (tl_monotype_is_integer(res)) return null;
    return res;
}

// ============================================================================
// Type environment
// ============================================================================
//
// Maps names (str) to polytypes (tl_polytype*).  Used during inference to track
// the types of all in-scope bindings.  Supports free-variable checking and
// pruning of unknown symbols during tree shaking.

tl_type_env *tl_type_env_create(allocator *alloc) {
    tl_type_env *self = alloc_malloc(alloc, sizeof *self);
    self->alloc       = alloc;
    self->map         = map_create(self->alloc, sizeof(tl_polytype *), 64); // key: str
    self->verbose     = 0;

    return self;
}

void tl_type_env_insert(tl_type_env *self, str name, tl_polytype *type) {
#if DEBUG_ENV
    str type_str = tl_polytype_to_string(transient_allocator, type);
    dbg(self, "insert %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));
#endif
    str_map_set_ptr(&self->map, name, type);
}

void tl_type_env_insert_mono(tl_type_env *self, str name, tl_monotype *type) {
    tl_polytype *poly = tl_polytype_absorb_mono(self->alloc, type);
#if DEBUG_ENV
    str type_str = tl_polytype_to_string(transient_allocator, poly);
    dbg(self, "insert_mono %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));
#endif
    str_map_set_ptr(&self->map, name, poly);
}

tl_polytype *tl_type_env_lookup(tl_type_env *self, str name) {
    return str_map_get_ptr(self->map, name);
}

int tl_type_env_check_missing_fvs(tl_type_env *self, missing_fv_cb cb, void *user) {
    int              error = 0;

    hashmap_iterator iter  = {0};
    while (map_iter(self->map, &iter)) {
        str          name = str_init_n(transient_allocator, iter.key_ptr, iter.key_size);
        tl_polytype *type = *(tl_polytype **)iter.data;

        // FIXME: observed a valid type with a null monotype here
        if (!type || !type->type) continue;

        str_sized fvs = tl_monotype_fvs(type->type);
        forall(i, fvs) {
            if (!str_map_contains(self->map, fvs.v[i])) {
                if (cb) cb(user, name, fvs.v[i]);
                ++error;
            }
        }
    }
    return error;
}

void tl_type_env_remove_unknown_symbols(tl_type_env *self, hashmap *known) {
    str_array        remove = {.alloc = transient_allocator};

    hashmap_iterator iter   = {0};
    while (map_iter(self->map, &iter)) {
        str name = str_init_n(transient_allocator, iter.key_ptr, iter.key_size);
        if (!str_hset_contains(known, name)) array_push(remove, name);
    }

    forall(i, remove) {
        // Don't remove c_ symbols
        if (is_c_symbol(remove.v[i])) continue;

        str_map_erase(self->map, remove.v[i]);
        // dbg(self, "tl_type_env_remove_unknown_symbols: removing '%s'", str_cstr(&remove.v[i]));
    }
}

// ============================================================================
// Polytype operations
// ============================================================================
//
// Polytypes pair a monotype with quantified type variables (forall).  Key
// operations: instantiate (replace quantified vars with fresh type vars for
// let-polymorphism), specialize (replace with concrete types for monomorphization),
// generalize (promote free type vars to quantified), and clone/merge utilities.

tl_polytype *tl_polytype_absorb_mono(allocator *alloc, tl_monotype *mono) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->quantifiers = (tl_type_variable_sized){0};
    self->type        = mono;
    return self;
}

tl_polytype *tl_polytype_create(allocator *alloc, tl_type_variable_sized quantifiers, tl_monotype *mono) {
    tl_polytype *self = tl_polytype_absorb_mono(alloc, mono);
    self->quantifiers = quantifiers;
    return self;
};

tl_polytype *tl_polytype_create_qv(allocator *alloc, tl_type_variable qv) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->type        = tl_monotype_create_tv(alloc, qv);
    self->quantifiers =
      (tl_type_variable_sized){.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    self->quantifiers.v[0] = qv;
    return self;
}

tl_polytype *tl_polytype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *mono = tl_monotype_create_tv(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype *tl_polytype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *mono = tl_monotype_create_weak(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype *tl_polytype_create_fresh_weak(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_weak(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_create_fresh_qv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_qv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_create_fresh_tv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_tv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_clone(allocator *alloc, tl_polytype *orig) {
    tl_polytype *clone = alloc_malloc(alloc, sizeof *clone);
    clone->quantifiers = orig->quantifiers;
    clone->type        = tl_monotype_clone(alloc, orig->type);
    return clone;
}

tl_polytype *tl_polytype_clone_mono(allocator *alloc, tl_monotype *mono) {
    return tl_polytype_absorb_mono(alloc, tl_monotype_clone(alloc, mono));
}

void tl_polytype_list_append(allocator *alloc, tl_polytype *lhs, tl_polytype *rhs) {
    if (rhs->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, lhs->quantifiers.size + rhs->quantifiers.size);
        array_push_many(arr, lhs->quantifiers.v, lhs->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, rhs->quantifiers) array_set_insert(arr, rhs->quantifiers.v[i]);
        alloc_free(alloc, lhs->quantifiers.v);
        lhs->quantifiers.size = arr.size;
        lhs->quantifiers.v    = arr.v;
    }

    tl_monotype *left  = lhs->type;
    tl_monotype *right = rhs->type;
    assert(tl_arrow == left->tag);

    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, left->list.xs.size);
    array_push_many(arr, left->list.xs.v, left->list.xs.size);

    switch (right->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:
    case tl_cons_inst:         array_push(arr, right); break;
    case tl_arrow:
    case tl_tuple:             array_push_many(arr, right->list.xs.v, right->list.xs.size); break;
    }

    array_shrink(arr);
    alloc_free(alloc, left->list.xs.v);
    left->list.xs = (tl_monotype_sized)sized_all(arr);
}

void tl_polytype_merge_quantifiers(allocator *alloc, tl_polytype *self, tl_polytype *in) {
    if (in->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, self->quantifiers.size + in->quantifiers.size);
        array_push_many(arr, self->quantifiers.v, self->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, in->quantifiers) array_set_insert(arr, in->quantifiers.v[i]);
        array_shrink(arr);

        alloc_free(alloc, self->quantifiers.v);
        self->quantifiers.size = arr.size;
        self->quantifiers.v    = arr.v;
    }
}

void tl_polytype_merge_quantifiers_sized(allocator *alloc, tl_polytype *self, tl_polytype_sized arr) {
    tl_type_variable_array tv_array = {.alloc = alloc};
    u32                    sum      = 0;
    forall(i, arr) sum += arr.v[i]->quantifiers.size;
    array_reserve(tv_array, self->quantifiers.size + sum);
    array_push_many(tv_array, self->quantifiers.v, self->quantifiers.size);

    forall(i, arr) {
        tl_polytype *in = arr.v[i];
        forall(i, in->quantifiers) array_set_insert(tv_array, in->quantifiers.v[i]);
    }

    alloc_free(alloc, self->quantifiers.v);
    self->quantifiers.size = tv_array.size;
    self->quantifiers.v    = tv_array.v;
}

static void replace_tv(tl_monotype *self, tl_type_subs *subs, hashmap **map, hashmap **seen) {
    if (!self || seen_set_visit(seen, self)) return;

    // map: tv -> tv

    // Replaces all type variables in self with the matching type variable in map. Used during instantiation
    // to convert quantified type variables to unquantified type variables.

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    break;

    case tl_var:         {
        tl_type_variable *replace = map_get(*map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
    } break;

    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        {
        // Every weak tv gets a fresh tv on instantiation.
        tl_type_variable *replace = map_get(*map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
        else {
            tl_type_variable fresh = tl_type_subs_fresh(subs);
            map_set(map, &self->var, sizeof self->var, &fresh);
            self->var = fresh;
        }

    } break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr = tl_monotype_children(self);
        forall(i, arr) replace_tv(arr.v[i], subs, map, seen);
    } break;
    }
}

static void replace_tv_mono(tl_monotype *self, tl_type_subs *subs, hashmap **map, hashmap **seen) {
    if (!self || seen_set_visit(seen, self)) return;

    // map: tv -> monotype

    // Replaces all type variables in self with the matching monotypes in map. Used during instantiation
    // to convert quantified type variables to arbitrary monotypes.

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:               break;

    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        {
        // Every weak tv gets a fresh tv on instantiation.
        // The map stores tl_monotype* values, so we must work with pointers.
        tl_monotype **replace = map_get(*map, &self->var, sizeof self->var);
        if (replace) self->var = (*replace)->var;
        else {
            tl_type_variable fresh_tv   = tl_type_subs_fresh(subs);
            tl_monotype     *fresh_mono = alloc_malloc(subs->data.alloc, sizeof *fresh_mono);
            *fresh_mono                 = (tl_monotype){.tag = self->tag, .var = fresh_tv};
            map_set(map, &self->var, sizeof self->var, &fresh_mono);
            self->var = fresh_tv;
        }
    } break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr = tl_monotype_children(self);

        forall(i, arr) {
            tl_monotype *mono = arr.v[i];
            if (tl_monotype_is_tv(mono) && map_contains(*map, &mono->var, sizeof(tl_type_variable)))
                arr.v[i] = map_get_ptr(*map, &mono->var, sizeof(tl_type_variable));
            else replace_tv_mono(arr.v[i], subs, map, seen);
        }
    } break;
    }
}

// Internal implementation for polytype instantiation.
// If args.v is NULL, creates fresh type variables for each quantifier.
// If args.v is non-NULL, uses the provided monotypes as substitutions.
// If args.v[i] is NULL, create fresh type variable for THAT quantifier.
static tl_monotype *tl_polytype_instantiate_(allocator *alloc, tl_polytype *self, tl_type_subs *subs,
                                             tl_monotype_sized args) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);
    hashmap     *seen  = hset_create(transient_allocator, 64);

    if (!args.v) {
        // Create fresh type variables for each quantifier
        hashmap *q_to_t = map_create(transient_allocator, sizeof(tl_type_variable), 8);

        forall(i, self->quantifiers) {
            tl_type_variable tv = tl_type_subs_fresh(subs);
            map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &tv);
        }

        replace_tv(fresh, subs, &q_to_t, &seen);
    } else {
        // Use provided monotypes as substitutions
        if (self->quantifiers.size != args.size) fatal("logic error");

        hashmap *q_to_t = map_create(transient_allocator, sizeof(tl_monotype *), args.size);

        forall(i, self->quantifiers) {
            if (!args.v[i]) args.v[i] = tl_monotype_create_fresh_tv(subs);
            map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &args.v[i]);
        }

        if (tl_monotype_is_tv(fresh) && map_contains(q_to_t, &fresh->var, sizeof fresh->var)) {
            fresh = map_get_ptr(q_to_t, &fresh->var, sizeof fresh->var);
            if (!fresh) fatal("unreachable");
        } else {
            replace_tv_mono(fresh, subs, &q_to_t, &seen);
        }
    }

    return fresh;
}

tl_monotype *tl_polytype_instantiate(allocator *alloc, tl_polytype *self, tl_type_subs *subs) {
    return tl_polytype_instantiate_(alloc, self, subs, (tl_monotype_sized){0});
}

tl_monotype *tl_polytype_instantiate_with(allocator *alloc, tl_polytype *self, tl_monotype_sized args,
                                          tl_type_subs *subs) {
    return tl_polytype_instantiate_(alloc, self, subs, args);
}

tl_monotype *tl_polytype_specialize(allocator *alloc, tl_polytype *self, tl_monotype_sized args) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized *inst = &fresh->cons_inst->args;
        if (args.size != inst->size) fatal("logic error");
        forall(i, *inst) inst->v[i] = args.v[i];
    }

    return fresh;
}

tl_monotype *tl_polytype_specialize_cons(allocator *alloc, tl_polytype *self, tl_monotype_sized args,
                                         tl_type_registry *registry, str special_name) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized *inst             = &fresh->cons_inst->args;
        int                is_variable_args = fresh->cons_inst->def->is_variable_args;
        if (!is_variable_args) {
            if (args.size != inst->size) fatal("logic error");
        }

        // specialise cons arguments using registry
        forall(i, args) {
            if (tl_monotype_is_inst(args.v[i])) {
                str               name      = args.v[i]->cons_inst->def->name;
                tl_monotype_sized inst_args = args.v[i]->cons_inst->args;
                tl_monotype      *replace =
                  tl_type_registry_get_cached_specialization(registry, name, inst_args);
                if (replace) args.v[i] = replace;
            }
        }

        if (!is_variable_args) {
            forall(i, *inst) inst->v[i] = args.v[i];
        } else {
            *inst = args;
        }
        fresh->cons_inst->special_name = str_copy(alloc, special_name);
    }

    return fresh;
}

static void generalize(tl_monotype *self, tl_type_variable_array *quant, hashmap **seen) {
    if (!self || seen_set_visit(seen, self)) return;

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    break;

    case tl_var:
        //
        array_set_insert(*quant, self->var);
        break;

    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:             {
        tl_monotype_sized arr = tl_monotype_children(self);
        forall(i, arr) generalize(arr.v[i], quant, seen);

    } break;
    }
}

void tl_polytype_generalize(tl_polytype *self, tl_type_env *env, tl_type_subs *subs) {

    hashmap               *seen  = hset_create(transient_allocator, 64);
    tl_type_variable_array quant = {.alloc = transient_allocator};
    generalize(self->type, &quant, &seen);
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
    // leaks prior array, if any

    // instantiate to get fresh vars, then generalise again using the fresh vars
    self->type = tl_polytype_instantiate(env->alloc, self, subs);
    quant      = (tl_type_variable_array){.alloc = env->alloc};
    hset_reset(seen);
    generalize(self->type, &quant, &seen);
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
}

tl_monotype *tl_polytype_concrete(tl_polytype *self) {
    if (!tl_polytype_is_concrete(self)) fatal("runtime error");
    return self->type;
}

tl_polytype *tl_monotype_generalize(allocator *alloc, tl_monotype *mono) {
    tl_type_variable_array quantifiers = {.alloc = alloc};
    hashmap               *seen        = hset_create(transient_allocator, 64);
    generalize(mono, &quantifiers, &seen);
    return tl_polytype_create(alloc, (tl_type_variable_sized)array_sized(quantifiers), mono);
}

// ============================================================================
// Monotype operations
// ============================================================================
//
// Monotypes are the concrete type representation: type variables (tl_var),
// weak variables (tl_weak), constructor instances (tl_cons_inst), arrows,
// tuples, integers, any, ellipsis, and placeholders.  Provides constructors,
// deep clone, predicate queries (is_concrete, is_arrow, is_ptr, etc.),
// accessors (arrow_args, arrow_result, ptr_target), hashing, and
// pretty-printing (tl_monotype_to_string).

tl_monotype *tl_monotype_create_any(allocator *alloc) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_any};
    return self;
}

tl_monotype *tl_monotype_create_placeholder(allocator *alloc, str name) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_placeholder, .placeholder = name};
    return self;
}

tl_monotype *tl_monotype_create_ellipsis(allocator *alloc) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_ellipsis};
    return self;
}

tl_monotype *tl_monotype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_var, .var = tv};
    return self;
}

tl_monotype *tl_monotype_create_integer(allocator *alloc, i32 integer) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_integer, .integer = integer};
    return self;
}

tl_monotype *tl_monotype_create_fresh_tv(tl_type_subs *subs) {
    tl_type_variable tv = tl_type_subs_fresh(subs);
    return tl_monotype_create_tv(subs->data.alloc, tv);
}

tl_monotype *tl_monotype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak, .var = tv};
    return self;
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak(self->data.alloc, tv);
}

tl_monotype *tl_monotype_create_weak_int_signed(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak_int_signed, .var = tv};
    return self;
}

tl_monotype *tl_monotype_create_weak_int_unsigned(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak_int_unsigned, .var = tv};
    return self;
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak_int_signed(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak_int_signed(self->data.alloc, tv);
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak_int_unsigned(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak_int_unsigned(self->data.alloc, tv);
}

tl_monotype *tl_monotype_create_weak_float(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak_float, .var = tv};
    return self;
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak_float(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak_float(self->data.alloc, tv);
}

tl_monotype *tl_type_registry_create_arrow(tl_type_registry *self, tl_monotype *lhs, tl_monotype *rhs) {
    tl_monotype      *left  = tl_monotype_clone(self->alloc, lhs);
    tl_monotype      *right = tl_monotype_clone(self->alloc, rhs);
    tl_monotype_array arr   = {.alloc = self->alloc};
    assert(left);
    if (right) {
        array_reserve(arr, 2);
        array_push(arr, left);
        array_push(arr, right);
    } else {
        right = tl_type_registry_nil(self);
        array_push(arr, left);
        array_push(arr, right);
    }
    return tl_monotype_create_list(self->alloc, (tl_monotype_sized)array_sized(arr));
}

tl_monotype *tl_monotype_create_list(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_arrow, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_tuple(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_tuple, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_cons(allocator *alloc, tl_type_constructor_inst *cons) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_cons_inst, .cons_inst = cons};
    return self;
}

static tl_monotype *tl_monotype_clone_(allocator *alloc, tl_monotype *orig, hashmap **mapping) {
    if (!orig) return null;

    // Note: never clone a placeholder type, because they need to retain their pointer identity.
    if (tl_placeholder == orig->tag) return orig;

    tl_monotype *found = map_get_ptr(*mapping, &orig, sizeof(void *));
    if (found) return found;

    tl_monotype *clone = alloc_malloc(alloc, sizeof *clone);
    *clone             = (tl_monotype){.tag = orig->tag};
    map_set_ptr(mapping, &orig, sizeof(void *), clone);

    switch (orig->tag) {

    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        clone->var = orig->var; return clone;

    case tl_cons_inst:
        // copy the tl_type_constructor_inst struct

        clone->cons_inst  = alloc_malloc(alloc, sizeof *clone->cons_inst);
        *clone->cons_inst = *orig->cons_inst;

        // clone the args list
        clone->cons_inst->args         = tl_monotype_sized_clone(alloc, orig->cons_inst->args, mapping);
        clone->cons_inst->special_name = str_copy(alloc, orig->cons_inst->special_name);

        break;

    case tl_arrow:
    case tl_tuple:
        clone->list.xs  = tl_monotype_sized_clone(alloc, orig->list.xs, mapping);
        clone->list.fvs = orig->list.fvs; // shallow copy
        break;

    case tl_integer:     clone->integer = orig->integer; return clone;

    case tl_any:
    case tl_ellipsis:    return clone;
    case tl_placeholder: fatal("unreachable");
    }

    return clone;
}

tl_monotype *tl_monotype_clone(allocator *alloc, tl_monotype *orig) {
    hashmap     *mapping = map_create_ptr(transient_allocator, 32);
    tl_monotype *out     = tl_monotype_clone_(alloc, orig, &mapping);
    return out;
}

int tl_monotype_is_concrete_(tl_monotype *self, hashmap **seen) {
    if (!self) return 0;
    if (seen_set_visit(seen, self)) return 1;

    switch (self->tag) {

    case tl_placeholder:
    case tl_var:
    case tl_ellipsis:    return 0;

    case tl_integer:
    case tl_any:
        //
        return 1;

    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:
        // consider weak type variables as concrete, which is *usually* what type inference client wants.
        // There are _no_weak variants, otherwise.
        return 1;

    case tl_cons_inst:
        forall(i, self->cons_inst->args) if (!tl_monotype_is_concrete_(self->cons_inst->args.v[i],
                                                                       seen)) return 0;
        return 1;
    case tl_arrow:
    case tl_tuple: {
        forall(i, self->list.xs) if (!tl_monotype_is_concrete_(self->list.xs.v[i], seen)) return 0;
        return 1;
    }
    }
    fatal("unreachable");
}

int tl_monotype_is_concrete(tl_monotype *self) {
    hashmap *seen = hset_create(transient_allocator, 64);
    int      res  = tl_monotype_is_concrete_(self, &seen);
    return res;
}

int tl_monotype_arrow_is_concrete(tl_monotype *self) {
    // If arrow type is concrete on its arguments, and only polymorphic on its return type, we can consider
    // it concrete.
    if (!tl_monotype_is_arrow(self)) return 0;
    tl_monotype *params = self->list.xs.v[0];
    if (!tl_monotype_is_tuple(params)) fatal("runtime error");
    return tl_monotype_is_concrete(params);
}

int tl_monotype_is_weak_(tl_monotype *self, hashmap **seen) {
    if (!self) return 0;
    if (seen_set_visit(seen, self)) return 0;

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_var:
    case tl_ellipsis:          return 0;

    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        return 1;

    case tl_cons_inst:
        forall(i,
               self->cons_inst->args) if (tl_monotype_is_weak_(self->cons_inst->args.v[i], seen)) return 1;
        return 0;
    case tl_arrow:
    case tl_tuple: {
        forall(i, self->list.xs) if (tl_monotype_is_weak_(self->list.xs.v[i], seen)) return 1;
        return 0;
    }
    }
    fatal("unreachable");
}

int tl_monotype_is_weak_deep(tl_monotype *self) {
    hashmap *seen = hset_create(transient_allocator, 64);
    int      res  = tl_monotype_is_weak_(self, &seen);
    return res;
}

int tl_monotype_sized_is_concrete(tl_monotype_sized arr) {
    forall(i, arr) if (!tl_monotype_is_concrete(arr.v[i])) return 0;
    return 1;
}

int tl_monotype_sized_is_concrete_no_weak(tl_monotype_sized arr) {
    forall(i, arr) if (!tl_monotype_is_concrete_no_weak(arr.v[i])) return 0;
    return 1;
}

int tl_monotype_is_concrete_inst(tl_monotype *self) {
    return self && tl_cons_inst == self->tag && tl_monotype_is_concrete(self);
}

int tl_monotype_is_concrete_no_weak(tl_monotype *self) {
    return tl_monotype_is_concrete(self) && !tl_monotype_is_weak_deep(self);
}

int tl_monotype_is_any(tl_monotype *self) {
    return self && tl_any == self->tag;
}
int tl_monotype_is_integer(tl_monotype *self) {
    return self && tl_integer == self->tag;
}
int tl_monotype_is_weak(tl_monotype *self) {
    return self && tl_weak == self->tag;
}
int tl_monotype_is_weak_int(tl_monotype *self) {
    return self && (tl_weak_int_signed == self->tag || tl_weak_int_unsigned == self->tag);
}
int tl_monotype_is_weak_int_signed(tl_monotype *self) {
    return self && tl_weak_int_signed == self->tag;
}
int tl_monotype_is_weak_int_unsigned(tl_monotype *self) {
    return self && tl_weak_int_unsigned == self->tag;
}
int tl_monotype_is_weak_float(tl_monotype *self) {
    return self && tl_weak_float == self->tag;
}
int tl_monotype_is_any_weak(tl_monotype *self) {
    return tl_monotype_is_weak(self) || tl_monotype_is_weak_int(self) || tl_monotype_is_weak_float(self);
}
int tl_monotype_is_ellipsis(tl_monotype *self) {
    return self && tl_ellipsis == self->tag;
}

int tl_monotype_is_arrow(tl_monotype *self) {
    return self && tl_arrow == self->tag;
}

int tl_monotype_is_inst_of(tl_monotype *self, str name) {
    return self && tl_cons_inst == self->tag && str_eq(self->cons_inst->def->generic_name, name);
}

int tl_monotype_is_void(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Void"));
}

int tl_monotype_is_list(tl_monotype *self) {
    return self && tl_arrow == self->tag;
}

int tl_monotype_is_inst(tl_monotype *self) {
    return self && tl_cons_inst == self->tag;
}

int tl_monotype_is_inst_specialized(tl_monotype *self) {
    return tl_monotype_is_inst(self) && !str_is_empty(self->cons_inst->special_name);
}

int tl_monotype_is_enum(tl_monotype *self) {
    // Enum type: has field names but has no instance args
    return tl_monotype_is_inst(self) && self->cons_inst->def->field_names.size &&
           !self->cons_inst->args.size;
}

int tl_monotype_is_tuple(tl_monotype *self) {
    return self && tl_tuple == self->tag;
}

int tl_monotype_is_tv(tl_monotype *self) {
    return self && tl_var == self->tag;
}

int tl_monotype_is_ptr_to_char(tl_monotype *self) {
    if (!self || !tl_monotype_is_ptr(self)) return 0;
    tl_monotype *target = tl_monotype_ptr_target(self);
    return tl_monotype_is_inst_of(target, S("CChar"));
}

int tl_monotype_is_ptr_to_tv(tl_monotype *self) {
    if (!self || !tl_monotype_is_ptr(self)) return 0;
    tl_monotype *target = tl_monotype_ptr_target(self);
    return tl_monotype_is_tv(target);
}

int tl_monotype_is_ptr(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Ptr"));
}

int tl_monotype_is_const(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Const"));
}

tl_monotype *tl_monotype_const_target(tl_monotype *self) {
    assert(tl_monotype_is_const(self));
    assert(self->cons_inst->args.size == 1);
    return self->cons_inst->args.v[0];
}

int tl_monotype_is_ptr_to_const(tl_monotype *self) {
    if (!tl_monotype_is_ptr(self)) return 0;
    tl_monotype *target = tl_monotype_ptr_target(self);
    return tl_monotype_is_const(target);
}

int tl_monotype_is_ptr_or_null(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("PtrOrNull"));
}
int tl_monotype_is_union(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Union"));
}
int tl_monotype_is_carray(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("CArray"));
}
tl_monotype *tl_monotype_carray_element(tl_monotype *self) {
    assert(tl_monotype_is_carray(self));
    assert(self->cons_inst->args.size == 2);
    return self->cons_inst->args.v[0];
}
i32 tl_monotype_carray_count(tl_monotype *self) {
    assert(tl_monotype_is_carray(self));
    assert(self->cons_inst->args.size == 2);
    return tl_monotype_integer(self->cons_inst->args.v[1]);
}
int tl_monotype_has_ptr(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return 0;
    if (tl_monotype_is_union(self)) {
        forall(i, self->cons_inst->args) {
            if (tl_monotype_has_ptr(self->cons_inst->args.v[i])) return 1;
        }
        return 0;
    } else {
        return tl_monotype_is_ptr(self) || tl_monotype_is_ptr_or_null(self);
    }
}

int tl_monotype_arrow_has_arrow(tl_monotype *self) {
    int has_arrow = 0;
    if (tl_monotype_is_arrow(self)) {
        // A tl_arrow is a list of size 2, left and right. Left is a list of args.
        assert(tl_arrow == self->tag);
        assert(2 == self->list.xs.size);
        tl_monotype_sized args   = self->list.xs.v[0]->list.xs;
        tl_monotype      *result = self->list.xs.v[1];

        has_arrow                = tl_monotype_is_arrow(result);
        if (!has_arrow) {
            forall(i, args) {
                if (tl_monotype_is_arrow(args.v[i])) {
                    has_arrow = 1;
                    break;
                }
            }
        }
    }
    return has_arrow;
}

int tl_monotype_is_signed_integer(tl_monotype *self) {
    return tl_monotype_is_inst(self) && self->cons_inst->def->is_signed_integer;
}
int tl_monotype_is_unsigned_integer(tl_monotype *self) {
    return tl_monotype_is_inst(self) && self->cons_inst->def->is_unsigned_integer;
}
int tl_monotype_is_integer_convertible(tl_monotype *self) {
    return tl_monotype_is_signed_integer(self) || tl_monotype_is_unsigned_integer(self);
}
int tl_monotype_is_float_convertible(tl_monotype *self) {
    return tl_monotype_is_inst(self) && self->cons_inst->def->is_float_convertible;
}

void tl_monotype_set_signed_integer(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) fatal("logic error");
    self->cons_inst->def->is_signed_integer = 1;
}

void tl_monotype_set_unsigned_integer(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) fatal("logic error");
    self->cons_inst->def->is_unsigned_integer = 1;
}

void tl_monotype_set_float_convertible(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) fatal("logic error");
    self->cons_inst->def->is_float_convertible = 1;
}

int tl_monotype_integer_subchain(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return 0;
    return self->cons_inst->def->integer_subchain;
}

int tl_monotype_integer_width_rank(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return -1;
    return self->cons_inst->def->integer_width_rank;
}

int tl_monotype_compare_integer_width(tl_monotype *left, tl_monotype *right) {
    int lsc = tl_monotype_integer_subchain(left);
    int rsc = tl_monotype_integer_subchain(right);
    if (lsc == 0 || rsc == 0) return 2;
    if (lsc != rsc) return 2;
    int lr = tl_monotype_integer_width_rank(left);
    int rr = tl_monotype_integer_width_rank(right);
    if (lr < rr) return -1;
    if (lr > rr) return 1;
    return 0;
}

char const *tl_monotype_integer_c_min(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return NULL;
    return self->cons_inst->def->c_min_macro;
}

char const *tl_monotype_integer_c_max(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return NULL;
    return self->cons_inst->def->c_max_macro;
}

int tl_monotype_is_unsigned_family(tl_monotype *self) {
    return tl_monotype_is_unsigned_integer(self);
}

int tl_monotype_same_integer_subchain(tl_monotype *left, tl_monotype *right) {
    int lsc = tl_monotype_integer_subchain(left);
    int rsc = tl_monotype_integer_subchain(right);
    if (lsc == 0 || rsc == 0) return 0;
    return lsc == rsc;
}

i32 tl_monotype_integer(tl_monotype *self) {
    if (tl_monotype_is_integer(self)) return self->integer;
    fatal("runtime error");
}
tl_type_variable tl_monotype_tv(tl_monotype *self) {
    if (tl_monotype_is_tv(self)) return self->var;
    fatal("runtime error");
}

int tl_polytype_is_scheme(tl_polytype *poly) {
    return poly->quantifiers.size != 0;
}

int tl_polytype_is_concrete(tl_polytype *self) {
    return !tl_polytype_is_scheme(self) && tl_monotype_is_concrete(self->type);
}

int tl_polytype_is_concrete_inst(tl_polytype *self) {
    return !tl_polytype_is_scheme(self) && tl_monotype_is_concrete_inst(self->type);
}

int tl_polytype_is_concrete_no_weak(tl_polytype *self) {
    return !tl_polytype_is_scheme(self) && tl_monotype_is_concrete_no_weak(self->type);
}

int tl_polytype_is_type_constructor(tl_polytype *self) {
    return self && tl_monotype_is_inst(self->type);
}

int tl_polytype_type_constructor_has_field(tl_polytype *self, str name) {
    assert(tl_polytype_is_type_constructor(self));
    tl_type_constructor_def *def = self->type->cons_inst->def;
    return str_array_contains_one(def->field_names, name);
}

i32 tl_monotype_type_constructor_field_index(tl_monotype *self, str name) {
    assert(tl_monotype_is_inst(self));
    tl_type_constructor_def *def   = self->cons_inst->def;
    i32                      found = -1;
    forall(i, def->field_names) {
        if (str_eq(name, def->field_names.v[i])) {
            if (i > INT32_MAX) fatal("overflow");
            found = (i32)i;
            break;
        }
    }
    return found;
}

tl_monotype *tl_monotype_unary_target(tl_monotype *self) {
    if (!self || !tl_monotype_is_inst(self) || 1 != self->cons_inst->args.size) return null;
    return self->cons_inst->args.v[0];
}

tl_monotype *tl_monotype_ptr_target(tl_monotype *self) {
    if (tl_monotype_is_ptr(self)) {
        assert(self->cons_inst->args.size == 1);
        return self->cons_inst->args.v[0];
    } else if (tl_monotype_is_ptr_or_null(self)) {
        assert(self->cons_inst->args.size == 2);
        // first argument is Ptr
        return tl_monotype_ptr_target(self->cons_inst->args.v[0]);
    } else if (tl_monotype_is_union(self)) {
        forall(i, self->cons_inst->args) {
            tl_monotype *arg = self->cons_inst->args.v[i];
            if (tl_monotype_is_ptr(arg)) return tl_monotype_ptr_target(arg);
        }
        fatal("runtime error");
    }

    else
        fatal("unreachable");
}

void tl_monotype_ptr_set_target(tl_monotype *ptr, tl_monotype *target) {
    assert(tl_monotype_is_ptr(ptr));
    assert(target);
    ptr->cons_inst->args.v[0] = target;
    assert(tl_monotype_ptr_target(ptr) == target);
}

tl_monotype *tl_monotype_arrow_args(tl_monotype *self) {
    assert(tl_monotype_is_list(self));
    assert(2 == self->list.xs.size);
    tl_monotype *args = self->list.xs.v[0];
    assert(tl_monotype_is_tuple(args));
    return args;
}

tl_monotype_sized tl_monotype_arrow_get_args(tl_monotype *self) {
    tl_monotype *tup = tl_monotype_arrow_args(self);
    assert(tl_monotype_is_tuple(tup));
    return tup->list.xs;
}

void tl_monotype_sort_fvs(tl_monotype *self) {
    if (tl_arrow != self->tag) return;
    if (!self->list.fvs.size) return;
    qsort(self->list.fvs.v, self->list.fvs.size, sizeof self->list.fvs.v[0], str_cmp_v);
}

str_sized tl_monotype_fvs(tl_monotype *self) {
    if (tl_arrow != self->tag) return (str_sized){0};
    return self->list.fvs;
}

void tl_monotype_absorb_fvs(tl_monotype *self, str_sized fvs) {
    if (tl_arrow != self->tag) fatal("logic error");
    self->list.fvs = fvs;
}

u64 tl_type_constructor_def_hash64(tl_type_constructor_def *self) {
    assert(!str_is_empty(self->generic_name));
    u64 hash = str_hash64(self->generic_name);
    return hash;
}

u64 tl_monotype_sized_hash64_(u64, tl_monotype_sized, u32, hash_cycle_stack *);

u64 tl_monotype_hash64_(tl_monotype *self, u32 gen, hash_cycle_stack *in_progress) {
    if (!self) return 0;

    // Generation-based memoization
    if (self->hash_gen == gen) {
        return self->cached_hash;
    }

    u64 hash = hash64(&self->tag, sizeof self->tag);

    switch (self->tag) {
    case tl_placeholder:       hash = str_hash64_combine(hash, self->placeholder); break;

    case tl_any:
    case tl_ellipsis:          break;
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        hash = hash64_combine(hash, &self->var, sizeof self->var); break;

    case tl_integer:           hash = hash64_combine(hash, &self->integer, sizeof self->integer); break;

    case tl_cons_inst:         {
        tl_type_constructor_def *def      = self->cons_inst->def;
        u64                      def_hash = tl_type_constructor_def_hash64(def);
        hash                              = hash64_combine(hash, &def_hash, sizeof def_hash);

        // Check if self is an ancestor in progress
        u64 *ancestor = null;
        ancestor      = hash_cycle_find(in_progress, def_hash);
        if (ancestor) {
            // back-reference: use the def hash as a stable hash
            hash = hash64_combine(hash, ancestor, sizeof *ancestor);
        } else {

            // Recursive types: mark this in-progress
            int               pushed = hash_cycle_push(in_progress, def_hash);

            tl_monotype_sized args   = self->cons_inst->args;
            forall(i, args) {
                tl_monotype *arg = args.v[i];

                // Look through unary (e.g. Ptr) specializations to the target. If the target is the same
                // generic type constructor as ourselves, we simply tag it as "Self" and go no further.
                if (tl_monotype_is_unary(arg)) {
                    tl_monotype *target = tl_monotype_unary_target(arg);

                    // Check if target is an ancestor in progress
                    u64 *ancestor = null;
                    if (tl_monotype_is_inst(target)) {
                        u64 ancestor_hash = tl_type_constructor_def_hash64(target->cons_inst->def);
                        ancestor          = hash_cycle_find(in_progress, ancestor_hash);
                    }
                    if (ancestor) {
                        // back-reference: use the def hash as a stable hash
                        hash = hash64_combine(hash, ancestor, sizeof *ancestor);
                    } else {
                        hash  = str_hash64_combine(hash, S("Unary"));
                        u64 h = tl_monotype_hash64_(target, gen, in_progress);
                        hash  = hash64_combine(hash, &h, sizeof h);
                    }

                } else {
                    u64 h = tl_monotype_hash64_(arg, gen, in_progress);
                    hash  = hash64_combine(hash, &h, sizeof h);
                }
            }

            // Remove from in-progress
            if (pushed) hash_cycle_pop(in_progress);
        }

        // important: do not include special_name as part of hash, because specialize_user_type uses
        // unspecialised name + hash to de-duplicate

    } break;

    case tl_arrow:
    case tl_tuple: {
        hash = tl_monotype_sized_hash64_(hash, self->list.xs, gen, in_progress);
        if (tl_arrow == self->tag) hash = str_array_hash64(hash, self->list.fvs);
    } break;
    }

    // Cache result
    self->hash_gen    = gen;
    self->cached_hash = hash;
    return hash;
}

u64 tl_monotype_hash64(tl_monotype *self) {
    u32 gen = hash_gen++;
    if (gen == 0) gen = hash_gen++; // skip 0 on wraparound
    hash_cycle_stack in_progress = {.count = 0};
    return tl_monotype_hash64_(self, gen, &in_progress);
}

str tl_monotype_to_string_(allocator *alloc, tl_monotype *self, hashmap **map) {
    if (!self) return S("[null]");
    str *found = map_get(*map, &self, sizeof(void *));
    if (found) return *found;
    str provisional = str_init(alloc, "[recur]");
    map_set(map, &self, sizeof(void *), &provisional);

    str_build b = str_build_init(alloc, 64);

    switch (self->tag) {
    case tl_placeholder:
        //
        str_build_cat(&b, S("PLACEHOLDER"));
        break;

    case tl_any:
        //
        str_build_cat(&b, S("any"));
        break;

    case tl_ellipsis:
        //
        str_build_cat(&b, S("..."));
        break;

    case tl_integer: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "i%i", self->integer);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_var: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "t%u", self->var);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_weak: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "w%u", self->var);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_weak_int_signed: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "ws%u", self->var);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_weak_int_unsigned: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "wu%u", self->var);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_weak_float: {
        char buf[64];
        int  n = snprintf(buf, sizeof buf, "wf%u", self->var);
        str_build_cat_n(&b, buf, (u32)n);
    } break;

    case tl_cons_inst: {
        if (!str_is_empty(self->cons_inst->special_name)) str_build_cat(&b, self->cons_inst->special_name);
        else str_build_cat(&b, self->cons_inst->def->name);
        if (self->cons_inst->args.size) {
            str_build_cat(&b, S("["));
            forall(i, self->cons_inst->args) {
                str_build_cat(&b, tl_monotype_to_string_(alloc, self->cons_inst->args.v[i], map));
                if (i + 1 < self->cons_inst->args.size) str_build_cat(&b, S(", "));
            }
            str_build_cat(&b, S("]"));
        }
    } break;

    case tl_arrow: {
        if (self->list.fvs.size) {
            str_build_cat(&b, S("["));
            forall(i, self->list.fvs) {
                str_build_cat(&b, self->list.fvs.v[i]);
                if (i + 1 < self->list.fvs.size) str_build_cat(&b, S(" "));
            }
            str_build_cat(&b, S("] "));
        }
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string_(alloc, self->list.xs.v[i], map));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(" -> "));
        }
        str_build_cat(&b, S(")"));
    } break;

    case tl_tuple: {
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string_(alloc, self->list.xs.v[i], map));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S(")"));

    } break;
    }

    str out = str_build_finish(&b);
    map_set(map, &self, sizeof(void *), &out);
    return out;
}

str tl_monotype_to_string(allocator *alloc, tl_monotype *self) {
    hashmap *map = map_create(transient_allocator, sizeof(str), 32);
    str      out = tl_monotype_to_string_(alloc, self, &map);
    return out;
}

str tl_polytype_to_string(allocator *alloc, tl_polytype *self) {
    str_build b = str_build_init(alloc, 64);

    if (self->quantifiers.size) {
        str_build_cat(&b, S("forall"));
        forall(i, self->quantifiers) {
            char buf[64];
            int  n = snprintf(buf, sizeof buf, "t%u", self->quantifiers.v[i]);
            str_build_cat(&b, S(" "));
            str_build_cat_n(&b, buf, (u32)n);
        }
        str_build_cat(&b, S(". "));
    }

    str_build_cat(&b, tl_monotype_to_string(alloc, self->type));

    return str_build_finish(&b);
}

tl_monotype *tl_monotype_arrow_result(tl_monotype *self) {
    if (!tl_monotype_is_arrow(self)) fatal("logic error");
    if (2 != self->list.xs.size) fatal("runtime error");
    return self->list.xs.v[1];
}

// ============================================================================
// Substitutions and unification (union-find)
// ============================================================================
//
// Type substitutions use a union-find data structure with path compression and
// rank-based union.  tl_type_subs_unify_mono is the main unification algorithm:
// walks two monotypes structurally, handles special cases (any, ellipsis,
// integer/float-convertible), and dispatches to type-constructor, list, tuple,
// tv-tv, tv-mono, and weak unification.  tl_monotype_substitute applies the
// resolved substitution set to a monotype in-place using a generation counter
// for cycle detection.

tl_type_subs *tl_type_subs_create(allocator *alloc) {
    tl_type_subs *self = new(alloc, tl_type_subs);
    *self              = (tl_type_subs){
                   .data = (tl_type_uf_node_array){.alloc = alloc},
    };
    array_reserve(self->data, 1024);
    return self;
}

void tl_type_subs_destroy(allocator *alloc, tl_type_subs **p) {
    if (!p || !*p) return;
    array_free((*p)->data);
    alloc_free(alloc, *p);
    *p = null;
}

tl_type_variable tl_type_subs_fresh(tl_type_subs *self) {
    tl_type_uf_node x = {.parent = self->data.size};
    array_push(self->data, x);
    return x.parent;
}

static tl_type_variable uf_find(tl_type_subs *self, tl_type_variable tv) {
    assert(tv < self->data.size);
    if (self->data.v[tv].parent != tv) {
        // path compression
        self->data.v[tv].parent = uf_find(self, self->data.v[tv].parent);
    }
    return self->data.v[tv].parent;
}

void tl_type_subs_set_literal_value(tl_type_subs *self, tl_type_variable var, i64 value) {
    tl_type_variable root                = uf_find(self, var);
    self->data.v[root].literal_value     = value;
    self->data.v[root].has_literal_value = 1;
}

static void uf_union(tl_type_subs *self, tl_type_variable tv1, tl_type_variable tv2) {
    tl_type_variable x = uf_find(self, tv1);
    tl_type_variable y = uf_find(self, tv2);
    assert(max(x, y) < self->data.size);

    if (x == y) return;

    // merge by rank
    if (self->data.v[x].rank < self->data.v[y].rank) swap(x, y);

    // make x the new root, with rank >= y rank
    self->data.v[y].parent = x;
    if (self->data.v[x].rank == self->data.v[y].rank) self->data.v[x].rank++;
}

static int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
                      tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen,
                      tl_unify_direction dir);
static int unify_tuple(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right,
                       tl_monotype *lhs, tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen,
                       tl_unify_direction dir);
static int tl_type_subs_unify_tv_tv(tl_type_subs *, tl_type_variable, tl_type_variable, type_error_cb_fun,
                                    void *, hashmap **seen, tl_unify_direction);
static int tl_type_subs_unify_tv_weak(tl_type_subs *, tl_type_variable, tl_monotype *, type_error_cb_fun,
                                      void *, hashmap **seen);
static int tl_type_subs_unify_weak(tl_type_subs *, tl_monotype *weak, tl_monotype *, type_error_cb_fun,
                                   void *, hashmap            **seen);
static int tl_type_subs_unify_tv_weak_int(tl_type_subs *, tl_type_variable, tl_monotype *,
                                          type_error_cb_fun, void *, hashmap **seen);
static int tl_type_subs_unify_weak_int_concrete(tl_type_subs *, tl_monotype *weak_int,
                                                tl_monotype *concrete, type_error_cb_fun, void *,
                                                hashmap    **seen);
static int tl_type_subs_unify_tv_weak_float(tl_type_subs *, tl_type_variable, tl_monotype *,
                                            type_error_cb_fun, void *, hashmap **seen);
static int tl_type_subs_unify_weak_float_concrete(tl_type_subs *, tl_monotype *weak_float,
                                                  tl_monotype *concrete, type_error_cb_fun, void *,
                                                  hashmap    **seen);
static int unify_weak_int_other(tl_type_subs *, tl_monotype *weak_int, tl_monotype *other,
                                type_error_cb_fun, void *, hashmap                **seen);
static int unify_weak_float_other(tl_type_subs *, tl_monotype *weak_float, tl_monotype *other,
                                  type_error_cb_fun, void *, hashmap                  **seen);
int        tl_type_subs_unify_tv_mono(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                                      type_error_cb_fun cb, void *user, hashmap **seen, tl_unify_direction dir,
                                      int tv_is_left);
int tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                            void *user, hashmap **, tl_unify_direction);

static int unify_type_constructor_def(tl_type_constructor_def *lhs, tl_type_constructor_def *rhs) {
    if (lhs == rhs) return 0;
    if (str_eq(lhs->name, rhs->name)) return 0;
    if (str_eq(lhs->generic_name, rhs->generic_name)) return 0;

    return 1;
}

int unify_type_constructor_union(tl_type_subs *subs, tl_monotype *left, tl_monotype *right,
                                 type_error_cb_fun cb, void *user, hashmap **seen, tl_unify_direction dir) {
    (void)dir; // union matching is always symmetric
    assert(tl_monotype_is_inst(left));
    assert(left->cons_inst->def->is_variable_args);

    tl_monotype_sized unions = left->cons_inst->args;

    switch (right->tag) {
    case tl_integer:
    case tl_placeholder: return 1;

    case tl_any:
    case tl_ellipsis:    return 0;

    case tl_var:
        return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen, TL_UNIFY_SYMMETRIC, 0);
    case tl_weak: return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
        return tl_type_subs_unify_weak_int_concrete(subs, right, left, cb, user, seen);
    case tl_weak_float: return tl_type_subs_unify_weak_float_concrete(subs, right, left, cb, user, seen);

    case tl_cons_inst:  {
        if (right->cons_inst->def->is_variable_args) {
            tl_monotype_sized right_unions = right->cons_inst->args;
            forall(i, unions) {
                forall(j, right_unions) {
                    // don't pass cb so that any error is a soft error
                    if (0 == tl_type_subs_unify_mono(subs, unions.v[i], right_unions.v[j], null, null, seen,
                                                     TL_UNIFY_SYMMETRIC))
                        return 0;
                }
            }
        } else {
            forall(i, unions) {
                // don't pass cb so that any error is a soft error
                if (0 ==
                    tl_type_subs_unify_mono(subs, unions.v[i], right, null, null, seen, TL_UNIFY_SYMMETRIC))
                    return 0;
            }
        }
    } break;

    case tl_arrow:
    case tl_tuple:
        // attempt to union with any of the union types
        forall(i, unions) {
            if (0 == tl_type_subs_unify_mono(subs, unions.v[i], right, cb, user, seen, TL_UNIFY_SYMMETRIC))
                return 0;
        }
        break;
    }

    if (cb) cb(user, left, right);
    return 1;
}

int unify_type_constructor(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                           void *user, hashmap **seen, tl_unify_direction dir, int cons_is_left) {
    assert(tl_monotype_is_inst(left));
    if (left->cons_inst->def->is_variable_args)
        return unify_type_constructor_union(subs, left, right, cb, user, seen, dir);

    switch (right->tag) {
    case tl_integer:     return !tl_monotype_is_integer_convertible(left);

    case tl_placeholder: return 1;

    case tl_any:
    case tl_ellipsis:    return 0;

    // TV is on the opposite side from the cons_inst
    case tl_var:
        return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen, dir, !cons_is_left);
    case tl_weak: return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
        return tl_type_subs_unify_weak_int_concrete(subs, right, left, cb, user, seen);
    case tl_weak_float: return tl_type_subs_unify_weak_float_concrete(subs, right, left, cb, user, seen);

    case tl_cons_inst:  {

        if (right->cons_inst->def->is_variable_args)
            return unify_type_constructor_union(subs, right, left, cb, user, seen, dir);

        if (unify_type_constructor_def(left->cons_inst->def, right->cons_inst->def)) {
            if (cb) cb(user, left, right);
            return 1;
        }
        // Type constructor args are invariant — always SYMMETRIC
        return unify_list(subs, left->cons_inst->args, right->cons_inst->args, left, right, cb, user, seen,
                          TL_UNIFY_SYMMETRIC);
    }
    case tl_arrow:
    case tl_tuple:
        if (cb) cb(user, left, right);
        return 1;
    }
    fatal("unreachable");
}

// Unify when one side is a type variable (tl_var).
// tv_is_left: 1 if the TV was on the left (expected) side in the original unification.
static int unify_var_other(tl_type_subs *subs, tl_type_variable tv, tl_monotype *other,
                           type_error_cb_fun cb, void *user, hashmap **seen, tl_unify_direction dir,
                           int tv_is_left) {
    switch (other->tag) {
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    fatal("unreachable");
    case tl_integer:     return 1;
    case tl_var:
        // Preserve left/right ordering for directional unification
        if (tv_is_left) return tl_type_subs_unify_tv_tv(subs, tv, other->var, cb, user, seen, dir);
        else return tl_type_subs_unify_tv_tv(subs, other->var, tv, cb, user, seen, dir);
    case tl_weak:              return tl_type_subs_unify_tv_weak(subs, tv, other, cb, user, seen);
    case tl_weak_int_signed:
    case tl_weak_int_unsigned: return tl_type_subs_unify_tv_weak_int(subs, tv, other, cb, user, seen);
    case tl_weak_float:        return tl_type_subs_unify_tv_weak_float(subs, tv, other, cb, user, seen);
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:             return tl_type_subs_unify_tv_mono(subs, tv, other, cb, user, seen, dir, tv_is_left);
    }
    fatal("unreachable");
}

// Unify when one side is a weak variable (tl_weak).
static int unify_weak_other(tl_type_subs *subs, tl_monotype *weak, tl_monotype *other, type_error_cb_fun cb,
                            void *user, hashmap **seen) {
    switch (other->tag) {
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    fatal("unreachable");
    case tl_integer:     return 1;
    case tl_var:         return tl_type_subs_unify_tv_weak(subs, other->var, weak, cb, user, seen);
    case tl_weak:
        return tl_type_subs_unify_tv_tv(subs, weak->var, other->var, cb, user, seen, TL_UNIFY_SYMMETRIC);
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        return 1; // error: pointer-weak meets integer/float-weak
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:             return tl_type_subs_unify_weak(subs, weak, other, cb, user, seen);
    }
    fatal("unreachable");
}

// Check directional integer width for DIRECTED and EXACT modes.
// left = expected type, right = actual type.
// Returns 0 if the conversion is allowed, 1 if it's a type error.
// Returns -1 if direction check does not apply (cross-sub-chain: fall through to SYMMETRIC).
static int check_integer_direction(tl_monotype *expected, tl_monotype *actual, tl_unify_direction dir,
                                   type_error_cb_fun cb, void *user) {
    if (expected->cons_inst->def == actual->cons_inst->def) return 0; // same type: always OK

    // Cross-sub-chain between multi-member chains (e.g. CInt vs CInt32) requires an explicit cast.
    // Standalone types (CSize, CPtrDiff, CChar) mixing with other chains fall through to SYMMETRIC.
    if (!tl_monotype_same_integer_subchain(expected, actual)) {
        int esc = expected->cons_inst->def->integer_subchain;
        int asc = actual->cons_inst->def->integer_subchain;
        if (esc >= 1 && esc <= 4 && asc >= 1 && asc <= 4) {
            if (cb) cb(user, expected, actual);
            return 1;
        }
        // EXACT with two narrow integers: reject cross-subchain standalone pairs too.
        // This catches e.g. CUnsignedChar (subchain 2) vs CSize (subchain 5) through generics.
        // Non-narrow types (CUnsignedLongLong + CSize in operators) still fall through to SYMMETRIC.
        if (dir == TL_UNIFY_EXACT && expected->cons_inst->def->is_narrow_integer &&
            actual->cons_inst->def->is_narrow_integer) {
            if (cb) cb(user, expected, actual);
            return 1;
        }
        return -1;
    }

    if (dir == TL_UNIFY_EXACT) {
        if (cb) cb(user, expected, actual);
        return 1;
    }
    // TL_UNIFY_DIRECTED: check width ordering
    int cmp = tl_monotype_compare_integer_width(expected, actual);
    if (cmp >= 0) return 0; // expected wider or equal → widening OK
    // cmp == -1 (narrowing) → error
    if (cb) cb(user, expected, actual);
    return 1;
}

int tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                            void *user, hashmap **seen, tl_unify_direction dir) {
    if (!left || !right) return 1;

    tl_monotype_pair pair = {.left = left, .right = right};
    if (hset_contains(*seen, &pair, sizeof(pair))) return 0;
    swap(pair.left, pair.right);
    if (hset_contains(*seen, &pair, sizeof(pair))) return 0;
    hset_insert(seen, &pair, sizeof(pair));

    // Unification ignores Const type wrapper
    if (tl_monotype_is_const(left)) left = tl_monotype_const_target(left);
    if (tl_monotype_is_const(right)) right = tl_monotype_const_target(right);

    // `any` types unify with everything but are not concrete, so they don't resolve type variables
    if (tl_monotype_is_any(left) || tl_monotype_is_any(right)) return 0;

    // `ellipsis` types unify with everything but are not concrete. In addition, when part of a tuple, they
    // act as if the correct number of `any` types are present as required to unify with the target tuple.
    if (tl_monotype_is_ellipsis(left) || tl_monotype_is_ellipsis(right)) return 0;

    // Same-family integer types: behavior depends on direction mode.
    // SYMMETRIC: legacy behavior — same-family unifies freely, non-narrow types canonicalized.
    // DIRECTED:  left=expected, right=actual; widening (actual narrower) OK, narrowing/cross-chain error.
    // EXACT:     same concrete type required (for operators/conditionals).
    if (tl_monotype_is_signed_integer(left) && tl_monotype_is_signed_integer(right)) {
        if (dir != TL_UNIFY_SYMMETRIC) {
            int rc = check_integer_direction(left, right, dir, cb, user);
            if (rc >= 0) return rc; // 0 = OK, 1 = error; -1 = cross-sub-chain, fall through
        }
        // SYMMETRIC or cross-sub-chain fallback
        if (!left->cons_inst->def->is_narrow_integer && !right->cons_inst->def->is_narrow_integer) {
            left->cons_inst  = canonical_signed;
            right->cons_inst = canonical_signed;
        }
        return 0;
    }
    if (tl_monotype_is_unsigned_integer(left) && tl_monotype_is_unsigned_integer(right)) {
        if (dir != TL_UNIFY_SYMMETRIC) {
            int rc = check_integer_direction(left, right, dir, cb, user);
            if (rc >= 0) return rc;
        }
        // SYMMETRIC or cross-sub-chain fallback
        if (!left->cons_inst->def->is_narrow_integer && !right->cons_inst->def->is_narrow_integer) {
            left->cons_inst  = canonical_unsigned;
            right->cons_inst = canonical_unsigned;
        }
        return 0;
    }

    // Float-convertible types: directional width checking (same as integers above).
    // All floats share one sub-chain, so check_integer_direction never returns -1 here.
    if (tl_monotype_is_float_convertible(left) && tl_monotype_is_float_convertible(right)) {
        if (dir != TL_UNIFY_SYMMETRIC) {
            return check_integer_direction(left, right, dir, cb, user);
        }
        return 0;
    }

    if (tl_monotype_is_inst(left)) return unify_type_constructor(subs, left, right, cb, user, seen, dir, 1);
    if (tl_monotype_is_inst(right))
        return unify_type_constructor(subs, right, left, cb, user, seen, dir, 0);

    // Type variables on either side
    if (tl_var == left->tag) return unify_var_other(subs, left->var, right, cb, user, seen, dir, 1);
    if (tl_var == right->tag) return unify_var_other(subs, right->var, left, cb, user, seen, dir, 0);

    // Weak variables on either side
    if (tl_weak == left->tag) return unify_weak_other(subs, left, right, cb, user, seen);
    if (tl_weak == right->tag) return unify_weak_other(subs, right, left, cb, user, seen);

    // Weak integer variables on either side
    if (tl_weak_int_signed == left->tag || tl_weak_int_unsigned == left->tag)
        return unify_weak_int_other(subs, left, right, cb, user, seen);
    if (tl_weak_int_signed == right->tag || tl_weak_int_unsigned == right->tag)
        return unify_weak_int_other(subs, right, left, cb, user, seen);

    // Weak float variables on either side
    if (tl_weak_float == left->tag) return unify_weak_float_other(subs, left, right, cb, user, seen);
    if (tl_weak_float == right->tag) return unify_weak_float_other(subs, right, left, cb, user, seen);

    // Remaining structural cases: both sides must have the same tag
    if (left->tag != right->tag) {
        if (cb) cb(user, left, right);
        return 1;
    }

    switch (left->tag) {
    case tl_integer: return !(left->integer == right->integer);
    case tl_arrow:   return unify_list(subs, left->list.xs, right->list.xs, left, right, cb, user, seen, dir);
    case tl_tuple:
        return unify_tuple(subs, left->list.xs, right->list.xs, left, right, cb, user, seen, dir);

    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:
    case tl_cons_inst:         fatal("unreachable");
    }
    fatal("unreachable");
}

int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
               tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen, tl_unify_direction dir) {
    if (left.size != right.size) {
        if (cb) cb(user, lhs, rhs);
        return 1;
    }

    forall(i, left) {
        if (tl_type_subs_unify_mono(subs, left.v[i], right.v[i], cb, user, seen, dir)) {
            if (cb) cb(user, lhs, rhs);
            return 1;
        }
    }

    return 0;
}

int unify_tuple(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
                tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen,
                tl_unify_direction dir) {
    // Care must be taken when unifying tuples, because an ellipsis type will automatically unify with any
    // number of elements.

    forall(i, left) {
        if (tl_monotype_is_ellipsis(left.v[i])) goto success;
        if (i + 1 < right.size && tl_monotype_is_ellipsis(right.v[i])) goto success;
        if (i >= right.size || tl_type_subs_unify_mono(subs, left.v[i], right.v[i], cb, user, seen, dir)) {
            if (cb) cb(user, lhs, rhs);
            return 1;
        }
    }

    if (left.size != right.size) {
        if (cb) cb(user, lhs, rhs);
        return 1;
    }

    return 0;

success:
    return 0;
}

static int tl_type_subs_monotype_occurs_(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                                         hashmap **seen) {
    if (!mono) return 0;
    if (hset_contains(*seen, &mono, sizeof(void *))) return 0;
    hset_insert(seen, &mono, sizeof(void *));

    switch (mono->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:          return 0;

    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) {
            return 1;
        }
        tl_monotype *resolved = self->data.v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs_(self, tv, resolved, seen);

    } break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr = tl_monotype_children(mono);
        forall(i, arr) {
            // Allow Ptr(tv): this is explicit support for recursive types where a field with a Ptr type is
            // used to refer to itself.
            if (tl_monotype_is_ptr(arr.v[i])) {
                tl_monotype *target = tl_monotype_ptr_target(arr.v[i]);
                if (tl_monotype_is_tv(target) && target->var == tv) continue;
            }
            if (tl_type_subs_monotype_occurs_(self, tv, arr.v[i], seen)) {
                return 1;
            }
        }
    } break;
    }

    return 0;
}

int tl_type_subs_monotype_occurs(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono) {
    hashmap *seen = hset_create(transient_allocator, 64);
    int      res  = tl_type_subs_monotype_occurs_(self, tv, mono, &seen);
    return res;
}

static int tl_type_subs_unify_tv_tv(tl_type_subs *self, tl_type_variable left, tl_type_variable right,
                                    type_error_cb_fun cb, void *user, hashmap **seen,
                                    tl_unify_direction dir) {
    if (left == right) return 0;

    tl_type_variable left_root  = uf_find(self, left);
    tl_type_variable right_root = uf_find(self, right);
    if (left_root == right_root) return 0; // already in same equivalence class

    tl_monotype *left_type  = self->data.v[left_root].type;
    tl_monotype *right_type = self->data.v[right_root].type;
    if (left_type && right_type) {
        // both are resolved: must unify, propagating direction
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user, seen, dir)) {
            return 1;
        }
    }

    // union the two classes
    uf_union(self, left_root, right_root);

    // preserve the resolved type, if any
    tl_type_variable union_root   = uf_find(self, left_root);
    self->data.v[union_root].type = left_type ? left_type : right_type;
    return 0;
}

static int tl_type_subs_unify_tv_weak(tl_type_subs *self, tl_type_variable left, tl_monotype *right,
                                      type_error_cb_fun cb, void *user, hashmap **seen) {
    if (tl_weak != right->tag) fatal("logic error");

    tl_type_variable left_root  = uf_find(self, left);

    tl_monotype     *left_type  = self->data.v[left_root].type;
    tl_monotype     *right_type = right;
    if (left_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user, seen, TL_UNIFY_SYMMETRIC)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->data.v[left_root].type = right;

    return 0;
}

static int tl_type_subs_unify_weak(tl_type_subs *self, tl_monotype *weak, tl_monotype *right,
                                   type_error_cb_fun cb, void *user, hashmap **seen) {
    if (tl_weak != weak->tag) fatal("logic error");

    tl_type_variable weak_root  = uf_find(self, weak->var);

    tl_monotype     *weak_type  = self->data.v[weak_root].type;
    tl_monotype     *right_type = right;
    if (weak_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, weak_type, right_type, cb, user, seen, TL_UNIFY_SYMMETRIC)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->data.v[weak_root].type = right;

    return 0;
}

// Check whether a literal value fits in the given integer type's range.
// Returns 1 if it fits, 0 if it overflows.
int tl_monotype_integer_value_fits(tl_monotype *type, i64 val) {
    if (!tl_monotype_is_inst(type)) return 1;
    return integer_value_fits(type->cons_inst->def, val);
}

static int integer_value_fits(tl_type_constructor_def *def, i64 val) {
    if (!def->has_integer_range) return 1; // full-range or non-integer: always fits
    if (def->integer_min_value < 0)        // signed range (includes CChar: conservative signed treatment)
        return val >= def->integer_min_value && val <= (i64)def->integer_max_value;
    return val >= 0 && (u64)val <= def->integer_max_value;
}

// Unify when one side is a weak integer variable (tl_weak_int_signed or tl_weak_int_unsigned)
// and the other is a concrete type (tl_cons_inst).
static int tl_type_subs_unify_weak_int_concrete(tl_type_subs *subs, tl_monotype *weak_int,
                                                tl_monotype *concrete, type_error_cb_fun cb, void *user,
                                                hashmap **seen) {
    assert(tl_monotype_is_weak_int(weak_int));
    assert(tl_monotype_is_inst(concrete));

    tl_type_constructor_def *def = concrete->cons_inst->def;

    // Check family.
    // weak_int_signed (unsuffixed literal) is family-agnostic: can adopt any integer type.
    // weak_int_unsigned (u-suffixed literal) requires an unsigned integer, non-standalone.
    if (tl_weak_int_unsigned == weak_int->tag) {
        if (!def->is_unsigned_integer) {
            if (cb) cb(user, weak_int, concrete);
            return 1;
        }
        if (def->integer_subchain >= TL_INTEGER_SUBCHAIN_CSIZE) {
            if (cb) cb(user, weak_int, concrete);
            return 1;
        }
    } else {
        if (!def->is_signed_integer && !def->is_unsigned_integer) {
            if (cb) cb(user, weak_int, concrete);
            return 1;
        }
    }

    // Check compile-time literal range: if the weak int originated from a literal, verify it fits.
    {
        tl_type_variable lit_root = uf_find(subs, weak_int->var);
        if (subs->data.v[lit_root].has_literal_value) {
            i64 val = subs->data.v[lit_root].literal_value;
            if (!integer_value_fits(def, val)) {
                if (cb) cb(user, weak_int, concrete);
                return 1;
            }
        }
    }

    // Resolve: store the concrete type at the weak-int's root in union-find.
    tl_type_variable root     = uf_find(subs, weak_int->var);
    tl_monotype     *existing = subs->data.v[root].type;
    if (existing) {
        // recursive unify existing with concrete
        if (tl_type_subs_unify_mono(subs, existing, concrete, cb, user, seen, TL_UNIFY_SYMMETRIC)) return 1;
    }
    subs->data.v[root].type = concrete;
    return 0;
}

// Unify when a regular type variable meets a weak-int.
// Same pattern as tl_type_subs_unify_tv_weak: store the weak-int at the tv's root.
static int tl_type_subs_unify_tv_weak_int(tl_type_subs *self, tl_type_variable left, tl_monotype *right,
                                          type_error_cb_fun cb, void *user, hashmap **seen) {
    assert(tl_monotype_is_weak_int(right));

    tl_type_variable left_root = uf_find(self, left);

    tl_monotype     *left_type = self->data.v[left_root].type;
    if (left_type) {
        // tv is already resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right, cb, user, seen, TL_UNIFY_SYMMETRIC)) {
            return 1;
        }
    }

    // store the weak-int type at the root
    self->data.v[left_root].type = right;

    return 0;
}

// Unify when one side is a weak integer variable (tl_weak_int_signed or tl_weak_int_unsigned).
static int unify_weak_int_other(tl_type_subs *subs, tl_monotype *weak_int, tl_monotype *other,
                                type_error_cb_fun cb, void *user, hashmap **seen) {
    switch (other->tag) {
    case tl_placeholder: fatal("unreachable");
    case tl_any:
    case tl_ellipsis:    return 0;
    case tl_integer:     return 1; // type-level integer, not value integer
    case tl_var:         return tl_type_subs_unify_tv_weak_int(subs, other->var, weak_int, cb, user, seen);
    case tl_weak:        return 1; // error: pointer-weak meets integer-weak
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
        // same family? merge via tv_tv; different family? error
        if (weak_int->tag == other->tag)
            return tl_type_subs_unify_tv_tv(subs, weak_int->var, other->var, cb, user, seen,
                                            TL_UNIFY_SYMMETRIC);
        if (cb) cb(user, weak_int, other);
        return 1;
    case tl_weak_float:
        // weak int meets weak float = error
        if (cb) cb(user, weak_int, other);
        return 1;
    case tl_cons_inst: return tl_type_subs_unify_weak_int_concrete(subs, weak_int, other, cb, user, seen);
    case tl_arrow:
    case tl_tuple:
        if (cb) cb(user, weak_int, other);
        return 1;
    }
    fatal("unreachable");
}

// Unify when one side is a weak float variable and the other is a concrete type (tl_cons_inst).
static int tl_type_subs_unify_weak_float_concrete(tl_type_subs *subs, tl_monotype *weak_float,
                                                  tl_monotype *concrete, type_error_cb_fun cb, void *user,
                                                  hashmap **seen) {
    assert(tl_monotype_is_weak_float(weak_float));
    assert(tl_monotype_is_inst(concrete));

    // Check: concrete must be a float type.
    if (!concrete->cons_inst->def->is_float_convertible) {
        if (cb) cb(user, weak_float, concrete);
        return 1;
    }

    // Resolve: store the concrete type at the weak-float's root in union-find.
    tl_type_variable root     = uf_find(subs, weak_float->var);
    tl_monotype     *existing = subs->data.v[root].type;
    if (existing) {
        if (tl_type_subs_unify_mono(subs, existing, concrete, cb, user, seen, TL_UNIFY_SYMMETRIC)) return 1;
    }
    subs->data.v[root].type = concrete;
    return 0;
}

// Unify when a regular type variable meets a weak-float.
static int tl_type_subs_unify_tv_weak_float(tl_type_subs *self, tl_type_variable left, tl_monotype *right,
                                            type_error_cb_fun cb, void *user, hashmap **seen) {
    assert(tl_monotype_is_weak_float(right));

    tl_type_variable left_root = uf_find(self, left);

    tl_monotype     *left_type = self->data.v[left_root].type;
    if (left_type) {
        if (tl_type_subs_unify_mono(self, left_type, right, cb, user, seen, TL_UNIFY_SYMMETRIC)) {
            return 1;
        }
    }

    // store the weak-float type at the root
    self->data.v[left_root].type = right;

    return 0;
}

// Unify when one side is a weak float variable.
static int unify_weak_float_other(tl_type_subs *subs, tl_monotype *weak_float, tl_monotype *other,
                                  type_error_cb_fun cb, void *user, hashmap **seen) {
    switch (other->tag) {
    case tl_placeholder: fatal("unreachable");
    case tl_any:
    case tl_ellipsis:    return 0;
    case tl_integer:     return 1;
    case tl_var:         return tl_type_subs_unify_tv_weak_float(subs, other->var, weak_float, cb, user, seen);
    case tl_weak:        return 1; // error: pointer-weak meets float-weak
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
        // weak int meets weak float = error
        if (cb) cb(user, weak_float, other);
        return 1;
    case tl_weak_float:
        // merge: both weak floats resolve together
        return tl_type_subs_unify_tv_tv(subs, weak_float->var, other->var, cb, user, seen,
                                        TL_UNIFY_SYMMETRIC);
    case tl_cons_inst:
        return tl_type_subs_unify_weak_float_concrete(subs, weak_float, other, cb, user, seen);
    case tl_arrow:
    case tl_tuple:
        if (cb) cb(user, weak_float, other);
        return 1;
    }
    fatal("unreachable");
}

int tl_type_subs_unify_tv_mono(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                               type_error_cb_fun cb, void *user, hashmap **seen, tl_unify_direction dir,
                               int tv_is_left) {
    if (tl_type_subs_monotype_occurs(self, tv, mono)) return 1;

    tl_type_variable tv_root = uf_find(self, tv);

    switch (mono->tag) {
    case tl_placeholder: return 1;

    case tl_any:
    case tl_ellipsis:
        // unifies with everything, but does not resolve
        return 0;

    case tl_var:
        // case 1: both are tvs
        return tl_type_subs_unify_tv_tv(self, tv, mono->var, cb, user, seen, TL_UNIFY_SYMMETRIC);
    case tl_weak:
        // case 2: one is weak type variable
        return tl_type_subs_unify_tv_weak(self, tv, mono, cb, user, seen);
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
        // case 2b: one is weak integer type variable
        return tl_type_subs_unify_tv_weak_int(self, tv, mono, cb, user, seen);
    case tl_weak_float:
        // case 2c: one is weak float type variable
        return tl_type_subs_unify_tv_weak_float(self, tv, mono, cb, user, seen);

    case tl_integer:
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        // case 3: tv = concrete type or arrow or tuple
        tl_monotype *tv_type = self->data.v[tv_root].type;
        if (tv_type) {
            // If tv_type is a weak int that's already been resolved, follow its resolution chain
            // to get the concrete type. This prevents directional unification from re-resolving
            // the weak to a different type.
            if (tl_monotype_is_weak_int(tv_type)) {
                tl_type_variable weak_root = uf_find(self, tv_type->var);
                tl_monotype     *resolved  = self->data.v[weak_root].type;
                if (resolved) tv_type = resolved;
            }
            if (tl_monotype_is_weak_float(tv_type)) {
                tl_type_variable weak_root = uf_find(self, tv_type->var);
                tl_monotype     *resolved  = self->data.v[weak_root].type;
                if (resolved) tv_type = resolved;
            }
            // When TV is already resolved to a narrow concrete integer, force EXACT to prevent
            // implicit widening/narrowing through generics (e.g. f(x: T, y: T) with CInt and CShort).
            // Non-narrow canonicals (CLongLong/CUnsignedLongLong = Int/UInt) are excluded because
            // TVs frequently resolve to them via weak literal defaulting.
            tl_unify_direction effective_dir = dir;
            if (tl_monotype_is_inst(tv_type) && tv_type->cons_inst->def->is_narrow_integer &&
                tl_monotype_is_inst(mono) && mono->cons_inst->def->is_narrow_integer)
                effective_dir = TL_UNIFY_EXACT;

            // must unify; preserve left/right (expected/actual) ordering
            if (tv_is_left)
                return tl_type_subs_unify_mono(self, tv_type, mono, cb, user, seen, effective_dir);
            else return tl_type_subs_unify_mono(self, mono, tv_type, cb, user, seen, effective_dir);
        }

        // store the type at the root
        self->data.v[tv_root].type = mono;

    } break;
    }

    return 0;
}

static void tl_monotype_substitute_(allocator *alloc, tl_monotype *self, tl_type_subs *subs,
                                    hashmap *exclude, u32 gen) {
    // exclude may be null.
    if (!self) return;
    if (self->visited_gen == gen) return;
    self->visited_gen = gen;

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:          break;

    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        {

        if (exclude && hset_contains(exclude, &self->var, sizeof self->var)) return;
        tl_type_variable root = uf_find((tl_type_subs *)subs, self->var);
        if (exclude && hset_contains(exclude, &root, sizeof root)) return;

        tl_monotype *resolved = subs->data.v[root].type;
        if (resolved) {
            if (!tl_monotype_is_concrete_no_weak(resolved)) {
                tl_monotype_substitute_(alloc, resolved, subs, exclude, gen);
            }

            *self             = *resolved;
            self->visited_gen = gen; // restore after copy
        } else {
            // update to representative tv
            self->var = root;
        }

    } break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr = tl_monotype_children(self);
        forall(i, arr) {
            tl_monotype_substitute_(alloc, arr.v[i], subs, exclude, gen);
        }

    } break;
    }
}

void tl_monotype_substitute(allocator *alloc, tl_monotype *self, tl_type_subs *subs, hashmap *exclude) {
    u32 gen = substitute_gen++;
    if (gen == 0) gen = substitute_gen++; // skip 0 on wraparound (0 means "never visited")
    tl_monotype_substitute_(alloc, self, subs, exclude, gen);
}

static void tl_polytype_substitute_ext(allocator *alloc, tl_polytype *self, tl_type_subs *subs,
                                       hashmap **exclude) {
    if (exclude) map_reset(*exclude);

    if (exclude && self->quantifiers.size) {
        forall(i, self->quantifiers) {
            hset_insert(exclude, &self->quantifiers.v[i], sizeof(tl_type_variable));
        }
    }

    tl_monotype_substitute(alloc, self->type, subs, exclude ? *exclude : null);
}

void tl_polytype_substitute(allocator *alloc, tl_polytype *self, tl_type_subs *subs) {
    hashmap *exclude = null;

    if (self->quantifiers.size) exclude = map_create(alloc, sizeof(tl_type_variable), 8);
    tl_polytype_substitute_ext(alloc, self, subs, exclude ? &exclude : null);
    if (exclude) map_destroy(&exclude);
}

tl_polytype tl_polytype_wrap(tl_monotype *mono) {
    return (tl_polytype){.type = mono};
}

void tl_type_subs_apply(tl_type_subs *subs, tl_type_env *env) {
    hashmap         *exclude = map_create(transient_allocator, sizeof(tl_type_variable), 8);

    hashmap_iterator iter    = {0};
    while (map_iter(env->map, &iter)) {
        tl_polytype *poly = *(tl_polytype **)iter.data;
        tl_polytype_substitute_ext(subs->data.alloc, poly, subs, &exclude);
    }
}

void tl_type_subs_default_weak_ints(tl_type_subs *subs, tl_monotype *int_type, tl_monotype *uint_type,
                                    tl_monotype *float_type) {
    forall(i, subs->data) {
        tl_type_variable root = uf_find(subs, i);
        if (root != (tl_type_variable)i) continue; // skip non-roots
        tl_monotype *type = subs->data.v[root].type;
        if (!type) continue;
        if (tl_weak_int_signed == type->tag) {
            // Also default the weak int's own var root (it may differ from this root)
            tl_type_variable weak_root = uf_find(subs, type->var);
            if (!subs->data.v[weak_root].type) subs->data.v[weak_root].type = int_type;
            subs->data.v[root].type = int_type;
        } else if (tl_weak_int_unsigned == type->tag) {
            tl_type_variable weak_root = uf_find(subs, type->var);
            if (!subs->data.v[weak_root].type) subs->data.v[weak_root].type = uint_type;
            subs->data.v[root].type = uint_type;
        } else if (tl_weak_float == type->tag) {
            tl_type_variable weak_root = uf_find(subs, type->var);
            if (!subs->data.v[weak_root].type) subs->data.v[weak_root].type = float_type;
            subs->data.v[root].type = float_type;
        }
    }
}

// Default weak int/float nodes in-place within a monotype tree.
// Unlike tl_type_subs_default_weak_ints (which walks the subs map), this walks the monotype
// structure directly, replacing any weak_int_signed/unsigned/float nodes with their concrete
// defaults.  Used when weak int TVs are not registered in the subs map.
static void tl_monotype_default_weak_ints_(tl_monotype *self, tl_monotype *int_type,
                                           tl_monotype *uint_type, tl_monotype *float_type, u32 gen) {
    if (!self) return;
    if (self->visited_gen == gen) return;
    self->visited_gen = gen;
    switch (self->tag) {
    case tl_weak_int_signed:   *self = *int_type; self->visited_gen = gen; return;
    case tl_weak_int_unsigned: *self = *uint_type; self->visited_gen = gen; return;
    case tl_weak_float:        *self = *float_type; self->visited_gen = gen; return;
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple: {
        tl_monotype_sized children = tl_monotype_children(self);
        forall(i, children) {
            tl_monotype_default_weak_ints_(children.v[i], int_type, uint_type, float_type, gen);
        }
    } break;
    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_var:
    case tl_weak:
    case tl_placeholder: break;
    }
}

void tl_monotype_default_weak_ints(tl_monotype *self, tl_monotype *int_type, tl_monotype *uint_type,
                                   tl_monotype *float_type) {
    u32 gen = substitute_gen++;
    if (gen == 0) gen = substitute_gen++; // skip 0 on wraparound (0 means "never visited")
    tl_monotype_default_weak_ints_(self, int_type, uint_type, float_type, gen);
}

// ============================================================================
// Debug logging and utilities
// ============================================================================

str tl_type_subs_to_string(allocator *alloc, tl_type_subs *self) {
    return str_copy(alloc, S("not implemented"));
    (void)self;
}

// -- env logging --

void tl_type_env_log(tl_type_env *self) {
    str_array sorted = str_map_sorted_keys(transient_allocator, self->map);
    forall(i, sorted) {
        str          name     = sorted.v[i];
        tl_polytype *type     = str_map_get_ptr(self->map, name);
        str          type_str = tl_polytype_to_string(transient_allocator, type);

        fprintf(stderr, "%.*s : %.*s\n", str_ilen(name), str_buf(&name), str_ilen(type_str),
                str_buf(&type_str));
        str_deinit(transient_allocator, &type_str);
    }
    array_free(sorted);
}

//

void tl_type_subs_log(tl_type_subs *self) {
    hashmap               *seen        = hset_create(transient_allocator, 128);
    tl_type_variable_array equiv_class = {.alloc = transient_allocator};

    forall(i, self->data) {
        tl_type_variable root = uf_find(self, i);
        if (hset_contains(seen, &root, sizeof root)) continue;
        hset_insert(&seen, &root, sizeof root);

        equiv_class.size = 0;
        forall(j, self->data) {
            if (uf_find(self, j) == root) array_push(equiv_class, j);
        }

        fprintf(stderr, "  {");
        forall(j, equiv_class) {
            fprintf(stderr, "t%u", equiv_class.v[j]);
            if (j + 1 < equiv_class.size) fprintf(stderr, ", ");
        }
        fprintf(stderr, "}");

        tl_monotype *type = self->data.v[root].type;
        if (type) {
            fprintf(stderr, " = ");
            str s = tl_monotype_to_string(transient_allocator, type);
            fprintf(stderr, "%.*s", str_ilen(s), str_buf(&s));
        }

        fprintf(stderr, "\n");
    }
}

//

u64 tl_monotype_sized_hash64_(u64 seed, tl_monotype_sized arr, u32 gen, hash_cycle_stack *in_progress) {
    u64 hash = seed;
    forall(i, arr) {
        u64 h = tl_monotype_hash64_(arr.v[i], gen, in_progress);
        hash  = hash64_combine(hash, &h, sizeof h);
    }
    return hash;
}

u64 tl_monotype_sized_hash64(u64 seed, tl_monotype_sized arr) {
    if (!arr.size) return seed;
    u32 gen = hash_gen++;
    if (gen == 0) gen = hash_gen++; // skip 0 on wraparound
    hash_cycle_stack in_progress = {.count = 0};
    return tl_monotype_sized_hash64_(seed, arr, gen, &in_progress);
}

tl_monotype_sized tl_monotype_sized_clone(allocator *alloc, tl_monotype_sized in, hashmap **mapping) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, in.size);
    forall(i, in) {
        tl_monotype *ty = tl_monotype_clone_(alloc, in.v[i], mapping);
        array_push(arr, ty);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

tl_polytype_sized tl_polytype_sized_clone(allocator *alloc, tl_polytype_sized polys) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_polytype *clone = tl_polytype_clone(alloc, polys.v[i]);
        array_push(arr, clone);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype *tl_monotype_sized_last(tl_monotype_sized arr) {
    if (!arr.size) return null;
    return arr.v[arr.size - 1];
}

tl_polytype_sized tl_monotype_sized_clone_poly(allocator *alloc, tl_monotype_sized monos) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, monos.size);
    forall(i, monos) {
        tl_polytype *poly = tl_polytype_absorb_mono(alloc, tl_monotype_clone(alloc, monos.v[i]));
        array_push(arr, poly);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype_sized tl_polytype_sized_concrete(allocator *alloc, tl_polytype_sized polys) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_monotype *mono = tl_polytype_concrete(polys.v[i]);
        array_push(arr, mono);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

static void dbg(tl_type_env *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    char buf[256];

    snprintf(buf, sizeof buf, "tl_type_env: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
