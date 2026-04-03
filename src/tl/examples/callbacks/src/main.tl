#module main
#import <Alloc.tl>
#import <Print.tl>
#import <String.tl>

#include <callbacks/callbacks.h>


// -- Tess bindings --

c_sort_ints(arr: Ptr[CLongLong], len: CInt,
            compare: (Ptr[Const[any]], Ptr[Const[any]]) -> CInt) -> Void

c_apply_int_fn       (x: CLongLong, f: (CLongLong) -> CLongLong) -> CLongLong
c_apply_float_fn     (x: CDouble, f: (CDouble) -> CDouble)       -> CDouble
c_apply_op           (a: CLongLong, b: CLongLong, op: Ptr[any])  -> CLongLong
c_store_callback     (fn: Ptr[any])                              -> Void
c_run_stored_callback()                                          -> CLongLong


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
// 2. Generic function pointers with explicit type arguments
// ============================================================

identity[T](x: T) -> T { x }

demo_generic_fn_ptr() {
    Print.println("--- 2. Generic function pointer (explicit type args) ---")
    ri := c_apply_int_fn(42, identity[CLongLong]/1)
    Print.println(f"apply_int_fn(42, identity[CLongLong]/1) = {ri}")
    rf := c_apply_float_fn(3.14, identity[CDouble]/1)
    Print.println(f"apply_float_fn(3.14, identity[CDouble]/1) = {rf}")
}


// ============================================================
// 3. Stack-based capturing closure
// ============================================================

demo_stack_capture() {
    Print.println("--- 3. Stack-based capturing closure ---")
    offset: CLongLong := 100
    op                := (a: CLongLong, b: CLongLong) -> CLongLong { a + b + offset }
    result            := c_apply_op(3, 4, op.&)
    Print.println(f"apply_op(3, 4, (a,b) a+b+100) = {result}")
}


// ============================================================
// 4. Heap-allocated capturing closure
// ============================================================

store_a_callback() {
    secret := 42
    f      := [[alloc, capture(secret)]] () -> CLongLong { secret }
    c_store_callback(f.&)
}

demo_alloc_capture() {
    Print.println("--- 4. Heap-allocated capturing closure ---")
    store_a_callback()
    result := c_run_stored_callback()
    Print.println(f"run_stored_callback() = {result}")
}


// ============================================================
// Main
// ============================================================

main() {
    demo_named_function()
    demo_inline_lambda()
    demo_lambda_binding()
    demo_generic_fn_ptr()
    demo_stack_capture()
    demo_alloc_capture()
    0
}
