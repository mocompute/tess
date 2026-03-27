#include "alloc.h"
#include "str.h"
#include "type.h"
#include "type_registry.h"

#include <stdio.h>
#include <string.h>

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

// Helper: create a minimal type registry for testing.
static tl_type_registry *test_registry(void) {
    allocator    *arena = arena_create(default_allocator(), 16 * 1024);
    tl_type_subs *subs  = tl_type_subs_create(arena);
    return tl_type_registry_create(arena, arena, subs);
}

// Helper: look up a nullary type by name and return its monotype.
static tl_monotype *lookup(tl_type_registry *reg, char *name) {
    str          s = (str){.big = {.buf = name, .len = (u32)strlen(name)}};
    tl_polytype *p = tl_type_registry_get(reg, s);
    if (!p) return null;
    return p->type;
}

// ---------------------------------------------------------------------------
// Test: sub-chain IDs for all integer types
// ---------------------------------------------------------------------------
static int test_subchain_ids(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    // C-named signed (sub-chain 1)
    error += tl_monotype_integer_subchain(lookup(reg, "CSignedChar")) != TL_INTEGER_SUBCHAIN_C_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CShort")) != TL_INTEGER_SUBCHAIN_C_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CInt")) != TL_INTEGER_SUBCHAIN_C_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CLong")) != TL_INTEGER_SUBCHAIN_C_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CLongLong")) != TL_INTEGER_SUBCHAIN_C_SIGNED;

    // C-named unsigned (sub-chain 2)
    error += tl_monotype_integer_subchain(lookup(reg, "CUnsignedChar")) != TL_INTEGER_SUBCHAIN_C_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUnsignedShort")) != TL_INTEGER_SUBCHAIN_C_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUnsignedInt")) != TL_INTEGER_SUBCHAIN_C_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUnsignedLong")) != TL_INTEGER_SUBCHAIN_C_UNSIGNED;
    error +=
      tl_monotype_integer_subchain(lookup(reg, "CUnsignedLongLong")) != TL_INTEGER_SUBCHAIN_C_UNSIGNED;

    // Fixed-width signed (sub-chain 3)
    error += tl_monotype_integer_subchain(lookup(reg, "CInt8")) != TL_INTEGER_SUBCHAIN_FIXED_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CInt16")) != TL_INTEGER_SUBCHAIN_FIXED_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CInt32")) != TL_INTEGER_SUBCHAIN_FIXED_SIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CInt64")) != TL_INTEGER_SUBCHAIN_FIXED_SIGNED;

    // Fixed-width unsigned (sub-chain 4)
    error += tl_monotype_integer_subchain(lookup(reg, "CUInt8")) != TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUInt16")) != TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUInt32")) != TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED;
    error += tl_monotype_integer_subchain(lookup(reg, "CUInt64")) != TL_INTEGER_SUBCHAIN_FIXED_UNSIGNED;

    // Standalone sub-chains
    error += tl_monotype_integer_subchain(lookup(reg, "CSize")) != TL_INTEGER_SUBCHAIN_CSIZE;
    error += tl_monotype_integer_subchain(lookup(reg, "CPtrDiff")) != TL_INTEGER_SUBCHAIN_CPTRDIFF;
    error += tl_monotype_integer_subchain(lookup(reg, "CChar")) != TL_INTEGER_SUBCHAIN_CCHAR;

    // Float sub-chain (sub-chain 8)
    error += tl_monotype_integer_subchain(lookup(reg, "CFloat")) != TL_INTEGER_SUBCHAIN_FLOAT;
    error += tl_monotype_integer_subchain(lookup(reg, "CDouble")) != TL_INTEGER_SUBCHAIN_FLOAT;
    error += tl_monotype_integer_subchain(lookup(reg, "CLongDouble")) != TL_INTEGER_SUBCHAIN_FLOAT;

    // Non-integer, non-float types
    error += tl_monotype_integer_subchain(lookup(reg, "Void")) != TL_INTEGER_SUBCHAIN_NONE;
    error += tl_monotype_integer_subchain(lookup(reg, "Bool")) != TL_INTEGER_SUBCHAIN_NONE;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: width ranks within each sub-chain
// ---------------------------------------------------------------------------
static int test_width_ranks(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    // C-named signed: 0,1,2,3,4
    error += tl_monotype_integer_width_rank(lookup(reg, "CSignedChar")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CShort")) != 1;
    error += tl_monotype_integer_width_rank(lookup(reg, "CInt")) != 2;
    error += tl_monotype_integer_width_rank(lookup(reg, "CLong")) != 3;
    error += tl_monotype_integer_width_rank(lookup(reg, "CLongLong")) != 4;

    // C-named unsigned: 0,1,2,3,4
    error += tl_monotype_integer_width_rank(lookup(reg, "CUnsignedChar")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUnsignedShort")) != 1;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUnsignedInt")) != 2;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUnsignedLong")) != 3;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUnsignedLongLong")) != 4;

    // Fixed-width signed: 0,1,2,3
    error += tl_monotype_integer_width_rank(lookup(reg, "CInt8")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CInt16")) != 1;
    error += tl_monotype_integer_width_rank(lookup(reg, "CInt32")) != 2;
    error += tl_monotype_integer_width_rank(lookup(reg, "CInt64")) != 3;

    // Fixed-width unsigned: 0,1,2,3
    error += tl_monotype_integer_width_rank(lookup(reg, "CUInt8")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUInt16")) != 1;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUInt32")) != 2;
    error += tl_monotype_integer_width_rank(lookup(reg, "CUInt64")) != 3;

    // Standalone types: rank 0
    error += tl_monotype_integer_width_rank(lookup(reg, "CSize")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CPtrDiff")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CChar")) != 0;

    // Float sub-chain: 0, 1, 2
    error += tl_monotype_integer_width_rank(lookup(reg, "CFloat")) != 0;
    error += tl_monotype_integer_width_rank(lookup(reg, "CDouble")) != 1;
    error += tl_monotype_integer_width_rank(lookup(reg, "CLongDouble")) != 2;

    // Non-integer, non-float types: -1
    error += tl_monotype_integer_width_rank(lookup(reg, "Void")) != -1;
    error += tl_monotype_integer_width_rank(lookup(reg, "Bool")) != -1;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: compare_integer_width within same sub-chain
// ---------------------------------------------------------------------------
static int test_compare_width_same_chain(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    // Within C-named signed
    error += tl_monotype_compare_integer_width(lookup(reg, "CSignedChar"), lookup(reg, "CLongLong")) != -1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CLongLong"), lookup(reg, "CSignedChar")) != 1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt"), lookup(reg, "CInt")) != 0;
    error += tl_monotype_compare_integer_width(lookup(reg, "CShort"), lookup(reg, "CLong")) != -1;

    // Within C-named unsigned
    error += tl_monotype_compare_integer_width(lookup(reg, "CUnsignedChar"),
                                               lookup(reg, "CUnsignedLongLong")) != -1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CUnsignedLongLong"),
                                               lookup(reg, "CUnsignedChar")) != 1;

    // Within fixed-width signed
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt8"), lookup(reg, "CInt64")) != -1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt64"), lookup(reg, "CInt8")) != 1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt32"), lookup(reg, "CInt32")) != 0;

    // Within fixed-width unsigned
    error += tl_monotype_compare_integer_width(lookup(reg, "CUInt8"), lookup(reg, "CUInt64")) != -1;
    error += tl_monotype_compare_integer_width(lookup(reg, "CUInt64"), lookup(reg, "CUInt8")) != 1;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: compare_integer_width across different sub-chains returns 2
// ---------------------------------------------------------------------------
static int test_compare_width_cross_chain(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    // C-named signed vs C-named unsigned
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt"), lookup(reg, "CUnsignedInt")) != 2;

    // C-named signed vs fixed-width signed
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt"), lookup(reg, "CInt32")) != 2;

    // C-named unsigned vs fixed-width unsigned
    error += tl_monotype_compare_integer_width(lookup(reg, "CUnsignedInt"), lookup(reg, "CUInt32")) != 2;

    // Any integer vs non-integer
    error += tl_monotype_compare_integer_width(lookup(reg, "CInt"), lookup(reg, "Void")) != 2;
    error += tl_monotype_compare_integer_width(lookup(reg, "Void"), lookup(reg, "CInt")) != 2;
    error += tl_monotype_compare_integer_width(lookup(reg, "Void"), lookup(reg, "Bool")) != 2;

    // Standalone vs multi-member chain
    error += tl_monotype_compare_integer_width(lookup(reg, "CSize"), lookup(reg, "CUnsignedInt")) != 2;
    error += tl_monotype_compare_integer_width(lookup(reg, "CPtrDiff"), lookup(reg, "CInt")) != 2;
    error += tl_monotype_compare_integer_width(lookup(reg, "CChar"), lookup(reg, "CUnsignedChar")) != 2;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: same_integer_subchain
// ---------------------------------------------------------------------------
static int test_same_subchain(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    // Same chain
    error += tl_monotype_same_integer_subchain(lookup(reg, "CInt"), lookup(reg, "CLong")) != 1;
    error +=
      tl_monotype_same_integer_subchain(lookup(reg, "CUnsignedChar"), lookup(reg, "CUnsignedLong")) != 1;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CInt8"), lookup(reg, "CInt64")) != 1;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CUInt16"), lookup(reg, "CUInt32")) != 1;

    // Standalone same (self)
    error += tl_monotype_same_integer_subchain(lookup(reg, "CSize"), lookup(reg, "CSize")) != 1;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CPtrDiff"), lookup(reg, "CPtrDiff")) != 1;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CChar"), lookup(reg, "CChar")) != 1;

    // Different chains
    error += tl_monotype_same_integer_subchain(lookup(reg, "CInt"), lookup(reg, "CUnsignedInt")) != 0;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CInt8"), lookup(reg, "CInt")) != 0;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CSize"), lookup(reg, "CUnsignedInt")) != 0;

    // Non-integer
    error += tl_monotype_same_integer_subchain(lookup(reg, "Void"), lookup(reg, "CInt")) != 0;
    error += tl_monotype_same_integer_subchain(lookup(reg, "CInt"), lookup(reg, "Void")) != 0;
    error += tl_monotype_same_integer_subchain(lookup(reg, "Void"), lookup(reg, "Bool")) != 0;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: weak integer literal construction and predicates
// ---------------------------------------------------------------------------
static int test_weak_int_construction(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();
    tl_type_subs     *subs  = tl_type_subs_create(reg->alloc);

    // Fresh weak-int signed
    tl_monotype *ws = tl_monotype_create_fresh_weak_int_signed(subs);
    error += !ws;
    error += ws->tag != tl_weak_int_signed;

    // Fresh weak-int unsigned
    tl_monotype *wu = tl_monotype_create_fresh_weak_int_unsigned(subs);
    error += !wu;
    error += wu->tag != tl_weak_int_unsigned;

    // Different type variables
    error += ws->var == wu->var;

    // Direct constructors
    tl_monotype *ws2 = tl_monotype_create_weak_int_signed(reg->alloc, 42);
    error += ws2->tag != tl_weak_int_signed;
    error += ws2->var != 42;

    tl_monotype *wu2 = tl_monotype_create_weak_int_unsigned(reg->alloc, 99);
    error += wu2->tag != tl_weak_int_unsigned;
    error += wu2->var != 99;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: weak integer predicates
// ---------------------------------------------------------------------------
static int test_weak_int_predicates(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();
    tl_type_subs     *subs  = tl_type_subs_create(reg->alloc);

    tl_monotype      *ws    = tl_monotype_create_fresh_weak_int_signed(subs);
    tl_monotype      *wu    = tl_monotype_create_fresh_weak_int_unsigned(subs);
    tl_monotype      *w     = tl_monotype_create_fresh_weak(subs);
    tl_monotype      *tv    = tl_monotype_create_fresh_tv(subs);

    // is_weak_int
    error += !tl_monotype_is_weak_int(ws);
    error += !tl_monotype_is_weak_int(wu);
    error += tl_monotype_is_weak_int(w);
    error += tl_monotype_is_weak_int(tv);

    // is_weak_int_signed
    error += !tl_monotype_is_weak_int_signed(ws);
    error += tl_monotype_is_weak_int_signed(wu);
    error += tl_monotype_is_weak_int_signed(w);

    // is_weak_int_unsigned
    error += tl_monotype_is_weak_int_unsigned(ws);
    error += !tl_monotype_is_weak_int_unsigned(wu);
    error += tl_monotype_is_weak_int_unsigned(w);

    // is_any_weak
    error += !tl_monotype_is_any_weak(ws);
    error += !tl_monotype_is_any_weak(wu);
    error += !tl_monotype_is_any_weak(w);
    error += tl_monotype_is_any_weak(tv);

    // is_weak (pointer-weak only — unchanged)
    error += tl_monotype_is_weak(ws);
    error += tl_monotype_is_weak(wu);
    error += !tl_monotype_is_weak(w);

    // is_concrete: weak-int counts as concrete
    error += !tl_monotype_is_concrete(ws);
    error += !tl_monotype_is_concrete(wu);

    // is_weak_deep: weak-int counts as weak
    error += !tl_monotype_is_weak_deep(ws);
    error += !tl_monotype_is_weak_deep(wu);

    // is_concrete_no_weak: weak-int is NOT concrete-no-weak
    error += tl_monotype_is_concrete_no_weak(ws);
    error += tl_monotype_is_concrete_no_weak(wu);

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: type registry csize/cptrdiff helpers
// ---------------------------------------------------------------------------
static int test_registry_csize_cptrdiff(void) {
    int               error    = 0;
    tl_type_registry *reg      = test_registry();

    tl_monotype      *csize    = tl_type_registry_csize(reg);
    tl_monotype      *cptrdiff = tl_type_registry_cptrdiff(reg);

    error += !csize;
    error += !cptrdiff;
    error += !tl_monotype_is_inst(csize);
    error += !tl_monotype_is_inst(cptrdiff);

    // Check they are the right types
    error += !tl_monotype_is_unsigned_integer(csize);
    error += !tl_monotype_is_signed_integer(cptrdiff);

    // Check subchain IDs
    error += tl_monotype_integer_subchain(csize) != TL_INTEGER_SUBCHAIN_CSIZE;
    error += tl_monotype_integer_subchain(cptrdiff) != TL_INTEGER_SUBCHAIN_CPTRDIFF;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

// ---------------------------------------------------------------------------
// Test: CChar is neither signed nor unsigned but is integer-convertible
// ---------------------------------------------------------------------------
static int test_cchar_signedness(void) {
    int               error = 0;
    tl_type_registry *reg   = test_registry();

    tl_monotype      *cchar = lookup(reg, "CChar");
    error += !cchar;
    error += !tl_monotype_is_inst(cchar);

    // CChar is neither signed nor unsigned
    error += tl_monotype_is_signed_integer(cchar) != 0;
    error += tl_monotype_is_unsigned_integer(cchar) != 0;
    error += tl_monotype_is_unsigned_family(cchar) != 0;

    // But it IS integer-convertible (has a valid subchain)
    error += tl_monotype_is_integer_convertible(cchar) != 1;

    // Subchain is correct
    error += tl_monotype_integer_subchain(cchar) != TL_INTEGER_SUBCHAIN_CCHAR;

    allocator *arena = reg->alloc;
    tl_type_transient_destroy();
    arena_destroy(&arena);
    return error;
}

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_subchain_ids);
    T(test_width_ranks);
    T(test_compare_width_same_chain);
    T(test_compare_width_cross_chain);
    T(test_same_subchain);
    T(test_weak_int_construction);
    T(test_weak_int_predicates);
    T(test_registry_csize_cptrdiff);
    T(test_cchar_signedness);

    return error;
}
