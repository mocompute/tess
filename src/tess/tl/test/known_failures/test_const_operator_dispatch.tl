// Const[UserType] on the LHS of a binary operator should dispatch to the
// user-defined overload in the pointee's module, the same way Ptr[UserType]
// does (see infer_specialize.c:find_overload_func Ptr fallback).
//
// Today `find_overload_func` has no symmetric Const fallback. `Const[Vec3] ==
// Const[Vec3]` falls through to the builtin handler, which emits raw struct
// comparison in C and is rejected at C-compile time.
//
// The UFCS path (a.eq(b)) already works — `ufcs_rewrite_call` uses
// `tl_monotype_effective_target` to peel both Ptr and Const before module
// lookup. Binary operator dispatch should match that behavior.
#module Vec

Vec3: { x: Int, y: Int, z: Int }

eq(a: Vec3, b: Vec3) -> Bool { a.x == b.x && a.y == b.y && a.z == b.z }

#module main

main() {
    a: Const := Vec.Vec3(x = 1, y = 2, z = 3)
    b        := Vec.Vec3(x = 1, y = 2, z = 3)
    c        := Vec.Vec3(x = 9, y = 9, z = 9)

    // Equal Const LHS + value RHS -- must dispatch to Vec.eq.
    if !(a == b) { return 1 }

    // Unequal values -- negated comparison.
    if a == c { return 2 }

    // != is derived from eq.
    if a != b { return 3 }
    if !(a != c) { return 4 }

    0
}
