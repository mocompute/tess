#module main

// Helper: narrows Int to Byte range, returning Option[Byte]
narrowing(x: Int) -> Option[Byte] {
    if x < -128 || x > 255 { return None() }
    b: Byte := x
    Some(b)
}

// Helper: returns Some(a - b) if a >= b, else None
checked_sub(a: Int, b: Int) -> Option[Int] {
    if a < b { return None() }
    Some(a - b)
}

// Helper: finds first positive among three values
find_first_positive(a: Int, b: Int, c: Int) -> Option[Int] {
    if a > 0 { return Some(a) }
    if b > 0 { return Some(b) }
    if c > 0 { return Some(c) }
    None()
}

// --- 1. Some construction with type inference ---

test_some_construction() {
    error := 0

    // Some(42) infers Option[Int]
    opt_int := Some(42)
    error += when opt_int {
        s: Some { if s.v == 42 {0} else {1} }
        n: None { 1 }
    }

    // Some(3.14) infers Option[Float]
    opt_float := Some(3.14)
    error += when opt_float {
        s: Some { if s.v == 3.14 {0} else {1} }
        n: None { 1 }
    }

    // Some(true) infers Option[Bool]
    opt_bool := Some(1 == 1)
    error += when opt_bool {
        s: Some { if s.v {0} else {1} }
        n: None { 1 }
    }

    error
}

// --- 2. None construction with type context ---

test_none_construction() {
    error := 0

    // None() inferred from annotated variable
    n: Option[Int] := None()
    error += when n {
        s: Some { 1 }
        n: None { 0 }
    }

    // None() inferred from void-initialized variable then assigned
    nopt: Option[Float] := void
    nopt = None()
    error += when nopt {
        s: Some { 1 }
        n: None { 0 }
    }

    // None() inferred from function return type (narrowing returns Option[Byte])
    r := narrowing(1000)
    error += when r {
        s: Some { 1 }
        n: None { 0 }
    }

    error
}

// --- 3. When matching ---

test_when_matching() {
    error := 0

    // Match Some, extract .v
    opt := Some(100)
    val := when opt {
        s: Some { s.v }
        n: None { 0 }
    }
    error += if val == 100 {0} else {1}

    // Match None, produce default
    empty: Option[Int] := None()
    val := when empty {
        s: Some { s.v }
        n: None { -1 }
    }
    error += if val == -1 {0} else {1}

    // When result fed into conditional
    opt := Some(50)
    doubled := when opt {
        s: Some { s.v * 2 }
        n: None { 0 }
    }
    error += if doubled == 100 {0} else {1}

    // When on function result directly
    val := when narrowing(42) {
        s: Some { s.v }
        n: None { 0 }
    }
    error += if val == 42 {0} else {1}

    error
}

// --- 4. When with else arm ---

test_when_else() {
    error := 0

    // else instead of explicit None arm — Some path
    opt := Some(77)
    val := when opt {
        s: Some { s.v }
        else { 0 }
    }
    error += if val == 77 {0} else {1}

    // else instead of explicit None arm — None path
    empty: Option[Int] := None()
    val := when empty {
        s: Some { s.v }
        else { -1 }
    }
    error += if val == -1 {0} else {1}

    error
}

// --- 5. Unwrap ---

test_unwrap() {
    error := 0

    // unwrap Some returns inner value
    error += if unwrap(Some(42), 0) == 42 {0} else {1}

    // unwrap None returns default
    none_val: Option[Int] := None()
    error += if unwrap(none_val, 99) == 99 {0} else {1}

    // unwrap with Float
    error += if unwrap(Some(2.718), 0.0) == 2.718 {0} else {1}

    // unwrap None Float
    none_f: Option[Float] := None()
    error += if unwrap(none_f, 1.0) == 1.0 {0} else {1}

    error
}

// --- 6. Try operator for Option propagation ---

// Composes checked_sub with try: computes (a - b) + (c - d), propagates None
add_subs(a: Int, b: Int, c: Int, d: Int) -> Option[Int] {
    x := try checked_sub(a, b)
    y := try checked_sub(c, d)
    Some(x + y)
}

test_try() {
    error := 0

    // Both subtractions succeed (a >= b and c >= d)
    r := add_subs(10, 3, 9, 4)
    error += when r {
        s: Some { if s.v == 12 {0} else {1} }
        n: None { 1 }
    }

    // First subtraction fails (a < b)
    r := add_subs(2, 10, 9, 4)
    error += when r {
        s: Some { 1 }
        n: None { 0 }
    }

    // Second subtraction fails (c < d)
    r := add_subs(10, 3, 4, 9)
    error += when r {
        s: Some { 1 }
        n: None { 0 }
    }

    error
}

// --- 7. Let-else (bail) pattern ---

test_let_else() {
    error := 0

    // Basic bail: extract Some or return error
    opt := Some(42)
    s: Some := opt else { return 1 }
    error += if s.v == 42 {0} else {1}

    // Bail takes the else path
    empty: Option[Int] := None()
    result := 0
    while 1 {
        s: Some := empty else { break }
        result = 1  // should not reach
        break
    }
    error += result

    // Multiple sequential bails
    a := Some(10)
    b := Some(20)
    x: Some := a else { return 1 }
    y: Some := b else { return 1 }
    error += if x.v + y.v == 30 {0} else {1}

    // Bail inside a loop with break
    opt := narrowing(99)
    found := 0
    while 1 {
        s: Some := opt else { break }
        found = s.v
        break
    }
    error += if found == 99 {0} else {1}

    error
}

// --- 8. Functions returning Option ---

test_option_returning_functions() {
    error := 0

    // narrowing: in-range returns Some
    r := narrowing(127)
    error += when r {
        s: Some { if s.v == 127 {0} else {1} }
        n: None { 1 }
    }

    // narrowing: out-of-range returns None
    r := narrowing(256)
    error += when r {
        s: Some { 1 }
        n: None { 0 }
    }

    // find_first_positive: first is positive
    r := find_first_positive(5, -1, -2)
    error += if unwrap(r, 0) == 5 {0} else {1}

    // find_first_positive: second is positive
    r := find_first_positive(-1, 7, -2)
    error += if unwrap(r, 0) == 7 {0} else {1}

    // find_first_positive: third is positive
    r := find_first_positive(-1, -2, 3)
    error += if unwrap(r, 0) == 3 {0} else {1}

    // find_first_positive: none positive
    r := find_first_positive(-1, -2, -3)
    error += when r {
        s: Some { 1 }
        n: None { 0 }
    }

    error
}

// --- 9. Generic function taking Option ---

get_or[T](opt: Option[T], default: T) -> T {
    when opt {
        s: Some { s.v }
        n: None { default }
    }
}

test_generic_option_function() {
    error := 0

    // Specialize with Int
    error += if get_or(Some(42), 0) == 42 {0} else {1}
    none_int: Option[Int] := None()
    error += if get_or(none_int, 99) == 99 {0} else {1}

    // Specialize with Float
    error += if get_or(Some(3.14), 0.0) == 3.14 {0} else {1}
    none_float: Option[Float] := None()
    error += if get_or(none_float, 2.718) == 2.718 {0} else {1}

    error
}

// --- 10. Nested Option[Option[T]] ---

test_nested_option() {
    error := 0

    // Some(Some(42)) — fully present
    nested := Some(Some(42))
    error += when nested {
        outer: Some {
            when outer.v {
                inner: Some { if inner.v == 42 {0} else {1} }
                n: None { 1 }
            }
        }
        n: None { 1 }
    }

    // Some(None()) — outer present, inner absent
    inner_none: Option[Int] := None()
    nested := Some(inner_none)
    error += when nested {
        outer: Some {
            when outer.v {
                inner: Some { 1 }
                n: None { 0 }
            }
        }
        n: None { 1 }
    }

    // None() — fully absent (needs type context)
    flat_none: Option[Option[Int]] := None()
    error += when flat_none {
        outer: Some { 1 }
        n: None { 0 }
    }

    error
}

// --- 11. Option in conditional expressions ---

test_conditional_option() {
    error := 0

    // if/else producing Option — type inference across branches
    x := 10
    opt := if x > 5 { Some(x) } else { None() }
    error += if unwrap(opt, 0) == 10 {0} else {1}

    // None branch taken
    x := 3
    opt := if x > 5 { Some(x) } else { None() }
    error += when opt {
        s: Some { 1 }
        n: None { 0 }
    }

    // Conditional inside a function returning Option
    r := checked_sub(10, 3)
    val := if unwrap(r, -1) > 0 { Some(unwrap(r, 0) * 2) } else { None() }
    error += if unwrap(val, 0) == 14 {0} else {1}

    error
}

// --- 12. Option as struct field ---

Point: { x: Int, y: Int }

Config: { value: Int, limit: Option[Int] }

test_option_in_struct() {
    error := 0

    // Struct with Some field
    c := Config(value = 10, limit = Some(100))
    error += when c.limit {
        s: Some { if s.v == 100 {0} else {1} }
        n: None { 1 }
    }

    // Struct with None field
    c := Config(value = 20, limit = None())
    error += when c.limit {
        s: Some { 1 }
        n: None { 0 }
    }

    // Access value field alongside Option field
    c := Config(value = 42, limit = Some(99))
    total := c.value + unwrap(c.limit, 0)
    error += if total == 141 {0} else {1}

    error
}

// --- 13. Map pattern (generic + lambda) ---

map_option[T, U](opt: Option[T], f) -> Option[U] {
    when opt {
        s: Some { Some(f(s.v)) }
        n: None { None() }
    }
}

test_map_option() {
    error := 0

    // Map Some: transform inner value
    opt := Some(3)
    mapped := map_option(opt, (x) { x * 2 })
    error += if unwrap(mapped, 0) == 6 {0} else {1}

    // Map None: stays None
    empty: Option[Int] := None()
    mapped := map_option(empty, (x) { x * 2 })
    error += when mapped {
        s: Some { 1 }
        n: None { 0 }
    }

    // Map with Float type
    fopt := Some(2.5)
    mapped_f := map_option(fopt, (x) { x * 2.0 })
    error += when mapped_f {
        s: Some { if s.v == 5.0 {0} else {1} }
        n: None { 1 }
    }

    error
}

// --- 14. Fallback / or pattern ---

first_some[T](a: Option[T], b: Option[T]) -> Option[T] {
    when a {
        s: Some { a }
        n: None { b }
    }
}

test_first_some() {
    error := 0

    // First is Some — returns first
    a := Some(3)
    b := Some(5)
    error += if unwrap(first_some(a, b), 0) == 3 {0} else {1}

    // First is None — returns second
    none_val: Option[Int] := None()
    b := Some(7)
    error += if unwrap(first_some(none_val, b), 0) == 7 {0} else {1}

    // Both None
    none1: Option[Int] := None()
    none2: Option[Int] := None()
    error += when first_some(none1, none2) {
        s: Some { 1 }
        n: None { 0 }
    }

    // Chain: first_some(None, first_some(None, Some(9)))
    error += if unwrap(first_some(none1, first_some(none2, Some(9))), 0) == 9 {0} else {1}

    error
}

// --- 15. Option wrapping compound type ---

test_option_with_struct() {
    error := 0

    // Some wrapping a struct
    opt := Some(Point(x = 10, y = 20))
    error += when opt {
        s: Some { if s.v.x == 10 && s.v.y == 20 {0} else {1} }
        n: None { 1 }
    }

    // Extract struct via let-else, then use fields
    opt := Some(Point(x = 3, y = 4))
    s: Some := opt else { return 1 }
    error += if s.v.x + s.v.y == 7 {0} else {1}

    // None of struct type
    empty: Option[Point] := None()
    error += when empty {
        s: Some { 1 }
        n: None { 0 }
    }

    error
}

// --- 16. Option as function parameter with inference ---

// Caller doesn't need to annotate — type inferred from the Option argument
add_if_present(base: Int, extra: Option[Int]) -> Int {
    when extra {
        s: Some { base + s.v }
        n: None { base }
    }
}

test_option_as_parameter() {
    error := 0

    // Pass Some directly — type inferred at call site
    error += if add_if_present(10, Some(5)) == 15 {0} else {1}

    // Pass None — needs type context from function signature
    error += if add_if_present(10, None()) == 10 {0} else {1}

    // Pass result of another function
    error += if add_if_present(100, checked_sub(10, 3)) == 107 {0} else {1}
    error += if add_if_present(100, checked_sub(3, 10)) == 100 {0} else {1}

    error
}

// --- Main ---

main() {
    error := 0

    error += test_some_construction()
    error += test_none_construction()
    error += test_when_matching()
    error += test_when_else()
    error += test_unwrap()
    error += test_try()
    error += test_let_else()
    error += test_option_returning_functions()
    error += test_generic_option_function()
    error += test_nested_option()
    error += test_conditional_option()
    error += test_option_in_struct()
    error += test_map_option()
    error += test_first_some()
    error += test_option_with_struct()
    error += test_option_as_parameter()

    error
}
