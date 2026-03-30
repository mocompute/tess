#module main

// Combined generic tagged union tests
// (from test_tagged_union_generic_{basic,case,func,param,nested,return,multi,scoped,function_pointer})

// --- Shared type definitions ---

// Option-like type (renamed to avoid builtin Some/None conflict)
MyOpt[a]: | MySome { value: a }
          | MyNone

// Box-like type for nested/return tests
Box[a]: | Has { v: a }
        | Empty

// Two-parameter type for multi test
Either[a, b]: | Left  { v: a }
              | Right { v: b }

// Function pointer types for function_pointer test
IntHandler: { fn: (Int) -> Int }

Outcome[T]: | Success { value: T }
            | Failure { code: Int, handler: IntHandler }

// --- Test: basic construction ---

test_basic() -> CInt {
    good: MyOpt[Int] := MyNone()
    0
}

// --- Test: pattern matching (case) ---

test_case() -> CInt {
    good := MySome(123)

    res := when good {
        s: MySome { s.value }
        n: MyNone { 0 }
    }
    res - 123
}

// --- Test: generic function (func) ---

my_unwrap[a](opt: MyOpt[a], default: a) -> a {
    when opt {
        s: MySome { s.value }
        n: MyNone { default }
    }
}

test_func() -> CInt {
    val := MySome(42)

    // Testing polymorphism of MyOpt and unwrap
    val_float := MySome(0.5)
    if my_unwrap(val_float, 0.0) != 0.5 { return 1 }

    my_unwrap(val, 0) - 42
}

// --- Test: concrete-typed function parameter (param) ---

my_unwrap_int(opt: MyOpt[Int]) -> Int {
    when opt {
        s: MySome { s.value }
        n: MyNone { 0 }
    }
}

test_param() -> CInt {
    val          := MySome(42)
    result: CInt := my_unwrap_int(val) - 42
    result
}

// --- Test: nested generic construction ---

test_nested() -> CInt {
    nested := Has(Has(42))
    0
}

// --- Test: return type inference ---

make_has(x) {
    Has(x)
}

test_return() -> CInt {
    val := make_has(42)
    0
}

// --- Test: multiple type parameters ---

test_multi() -> CInt {
    x: Either[Int, Bool] := Left(42)
    0
}

// --- Test: scoped variant access ---

test_scoped() -> CInt {
    o1 := MyOpt.MySome(42)
    r  := when o1 { s: MySome { s.value } n: MyNone { 0 } }
    if r == 42 {0} else {1}
}

// --- Test: function pointers in variants ---

double_int(x: Int) -> Int { x * 2 }
negate    (x: Int) -> Int { 0 - x }

apply_int_handler(h: IntHandler, x: Int) -> Int {
    h.fn(x)
}

test_function_pointer() -> CInt {
    error := 0

    // Success variant with Int value
    ok_int  := Success(42)
    result1 := when ok_int {
        o: Success { o.value }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result1 == 42 {0} else {1}

    // Success variant with Float value
    ok_float := Success(3.14)
    result2  := when ok_float {
        o: Success { o.value }
        e: Failure { 0.0 }
    }
    error += if result2 == 3.14 {0} else {1}

    // Failure variant with double handler
    h1                 := IntHandler(fn = double_int/1)
    err1: Outcome[Int] := Failure(5, h1)

    result3 := when err1 {
        o: Success { 0 }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result3 == 10 {0} else {1}

    // Failure variant with negate handler
    h2                 := IntHandler(fn = negate/1)
    err2: Outcome[Int] := Failure(7, h2)

    result4 := when err2 {
        o: Success { 0 }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result4 == -7 {0} else {1}

    error
}

// --- Main: run all tests ---

main() {
    error := 0
    error += test_basic()
    error += test_case()
    error += test_func()
    error += test_param()
    error += test_nested()
    error += test_return()
    error += test_multi()
    error += test_scoped()
    error += test_function_pointer()
    error
}
