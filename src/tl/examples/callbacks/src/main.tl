#module main
#import <Alloc.tl>
#import <Print.tl>
#import <String.tl>

// -- C forward declarations --
//
// We cannot #include "callbacks.h" because it defines tl_closure, which
// the Tess runtime also defines.  Instead, forward-declare the C functions
// so the generated C compiles without implicit-declaration errors.

#ifc
struct tl_closure;
void sort_ints(long long *arr, int len,
               int (*compare)(const void *, const void *));
long long apply_op(long long a, long long b, struct tl_closure *op);
void store_callback(struct tl_closure *fn);
long long run_stored_callback(void);
#endc


// -- Tess bindings --

c_sort_ints(arr: Ptr[CLongLong], len: CInt,
            compare: (Ptr[Const[any]], Ptr[Const[any]]) -> CInt) -> Void

c_apply_op           (a: CLongLong, b: CLongLong, op: Ptr[any]) -> CLongLong
c_store_callback     (fn: Ptr[any])                             -> Void
c_run_stored_callback()                                         -> CLongLong


// -- Helpers --

print_array(arr: Ptr[CLongLong], len: CInt) {
    i := 0
    while i < len {
        if i > 0 { Print.print(", ") }
        Print.print(f"{arr.[i]}")
        i += 1
    }
    Print.println("")
}

init_array(arr: Ptr[CLongLong]) {
    arr.[0] = 5
    arr.[1] = 3
    arr.[2] = 1
    arr.[3] = 4
    arr.[4] = 2
}


// ============================================================
// 1. Non-capturing callbacks: plain C function pointer (qsort)
// ============================================================

// A regular named function used as a callback.
compare_asc(a: Ptr[Const[any]], b: Ptr[Const[any]]) -> CInt {
    pa: Ptr[Const[CLongLong]] := a
    pb: Ptr[Const[CLongLong]] := b
    va                        := pa.*
    vb                        := pb.*
    if va < vb { 0 - 1 } else { if va > vb { 1 } else { 0 } }
}

demo_named_function() {
    Print.println("--- 1a. Named function as callback ---")
    arr: CArray[CLongLong, 5] := void
    init_array(arr.[0].&)
    c_sort_ints(arr.[0].&, 5, compare_asc/2)
    Print.print("sorted: ")
    print_array(arr.[0].&, 5)
}

demo_inline_lambda() {
    Print.println("--- 1b. Inline lambda as callback ---")
    // NOTE: inline lambdas passed directly as args don't work yet
    // (transpiler wraps in tl_closure instead of using trampoline).
    // Workaround: bind the lambda to a variable first.
    cmp := (a: Ptr[Const[any]], b: Ptr[Const[any]]) -> CInt {
        pa: Ptr[Const[CLongLong]] := a
        pb: Ptr[Const[CLongLong]] := b
        va                        := pa.*
        vb                        := pb.*
        if va > vb { 0 - 1 } else { if va < vb { 1 } else { 0 } }
    }
    arr: CArray[CLongLong, 5] := void
    init_array(arr.[0].&)
    c_sort_ints(arr.[0].&, 5, cmp)
    Print.print("sorted descending: ")
    print_array(arr.[0].&, 5)
}

demo_lambda_binding() {
    Print.println("--- 1c. Lambda as local binding ---")
    cmp := (a: Ptr[Const[any]], b: Ptr[Const[any]]) -> CInt {
        pa: Ptr[Const[CLongLong]] := a
        pb: Ptr[Const[CLongLong]] := b
        va                        := pa.*
        vb                        := pb.*
        if va < vb { 0 - 1 } else { if va > vb { 1 } else { 0 } }
    }
    arr: CArray[CLongLong, 5] := void
    init_array(arr.[0].&)
    c_sort_ints(arr.[0].&, 5, cmp)
    Print.print("sorted: ")
    print_array(arr.[0].&, 5)
}


// ============================================================
// 2. Stack-based capturing lambda
// ============================================================
//
// TODO: Transpiler bug — lambda.& generates a function pointer
// instead of a tl_closure{fn, ctx} struct.  The C side expects
// tl_closure* so it can call fn(ctx, args...).
//
// demo_stack_capture() {
//     Print.println("--- 2. Stack-based capturing lambda ---")
//     offset := 100
//     op     := (a: CLongLong, b: CLongLong) -> CLongLong { a + b + offset }
//     result := c_apply_op(3, 4, op.&)
//     Print.println(f"apply_op(3, 4, |a,b| a+b+100) = {result}")
// }


// ============================================================
// 3. Heap-allocated capturing lambda
// ============================================================
//
// TODO: Same transpiler bug as above, plus [[alloc, capture(...)]]
// needs to emit heap-allocated tl_closure struct.
//
// store_a_callback() {
//     secret := 42
//     f      := [[alloc, capture(secret)]] () -> CLongLong { secret }
//     c_store_callback(f.&)
// }
//
// demo_alloc_capture() {
//     Print.println("--- 3. Heap-allocated capturing lambda ---")
//     store_a_callback()
//     result := c_run_stored_callback()
//     Print.println(f"run_stored_callback() = {result}")
// }


// ============================================================
// Main
// ============================================================

main() {
    demo_named_function()
    demo_inline_lambda()
    demo_lambda_binding()
    // demo_stack_capture()
    // demo_alloc_capture()
    0
}
