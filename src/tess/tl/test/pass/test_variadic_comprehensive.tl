// Comprehensive variadic function tests.
// Combined from: test_variadic_basic, test_variadic_autocollapse,
// test_variadic_cross_module, test_variadic_longdouble,
// test_variadic_mixed_param_alias, test_variadic_multiple_fixed,
// test_variadic_print, test_variadic_tostring, test_variadic_tostring_typed,
// test_variadic_trait_no_collapse, test_variadic_values, test_variadic_zero_args.

#import <Hash.tl>
#import <ToString.tl>
#import <String.tl>
#import <Print.tl>
#import <cstdio.tl>

// --- Modules for cross_module test ---

#module Foo

format_all(args: ...Hash) -> CSize {
    result: CSize := 100
    i             := 0
    while i < args.size, i = i + 1 {
        result = result + args.data.[i]
    }
    result
}

#module Bar

format_all(args: ...Hash) -> CSize {
    result: CSize := 200
    i             := 0
    while i < args.size, i = i + 1 {
        result = result + args.data.[i]
    }
    result
}

// --- Modules for trait_no_collapse test ---

#module Helpers

Describable[T]: { describe(a: T) -> String }

#module Int

describe[T](n: T) -> String {
    s      := to_string(n)
    result := "["
    result.push(s)
    result.push("]")
    result
}

#module Bool

describe(b: Bool) -> String {
    s      := to_string(b)
    result := "["
    result.push(s)
    result.push("]")
    result
}

// --- Main module ---

#module main

// Shared helper: concat all ToString arguments into a single String.
concat_all(args: ...ToString) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.push(args.data.[i])
    }
    result
}

// Helper for trait_no_collapse test.
join_all(args: ...Helpers.Describable) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.push(args.data.[i])
    }
    result
}

// Aliases for print test.
print   = Print.print
println = Print.println

// Aliases for mixed_param_alias test.
fprint   = Print.fprint
fprintln = Print.fprintln

// --- test_basic ---

hash_all(args: ...Hash) -> CSize {
    args.size
}

test_basic() {
    h := hash_all(42, 100)
    if h != 2 { return 1 }
    0
}

// --- test_autocollapse ---

autocollapse_sum_hashes(args: ...Hash) -> CSize {
    h: CSize := 0
    i: CSize := 0
    while i < args.size, i += 1 {
        h = h + args.data.[i]
    }
    h
}

test_autocollapse() {
    // Bare ...ToString works
    s1 := concat_all(42, true)
    if !s1.eq_cstr(c"42true") { return 1 }

    s2 := concat_all()
    if !s2.is_empty() { return 2 }

    hello := "hello"
    s3    := concat_all(hello, 99)
    if !s3.eq_cstr(c"hello99") { return 3 }

    // Bare ...Hash works
    h := autocollapse_sum_hashes(1, 2, 3)
    if h == 0 { return 4 }

    0
}

// --- test_cross_module ---

test_cross_module() {
    a := Foo.format_all(1, 2, 3)
    b := Bar.format_all(1, 2, 3)

    if b - a != 100 { return 1 }
    0
}

// --- test_longdouble ---

test_longdouble() {
    x: CLongDouble := 3.14
    s              := concat_all(x)

    if !s.eq_cstr(c"3.14") { return 1 }
    0
}

// --- test_mixed_param_alias ---

test_mixed_param_alias() {
    fprint(c_stdout, "hello ")
    fprintln(c_stdout, "world")
    fprintln(c_stderr, 42, " is ", true)
    0
}

// --- test_multiple_fixed ---

count_with_base(base: CSize, args: ...Hash) -> CSize {
    base + args.size
}

test_multiple_fixed() {
    r := count_with_base(10, 1, 2, 3)
    if r != 13 { return 1 }
    0
}

// --- test_print ---

test_print() {
    // Print.print outputs without newline
    print(42, c" hello ", true)

    // Print.println appends a newline
    println(c" world")

    // Zero args
    print()
    println()

    0
}

// --- test_tostring ---

test_tostring() {
    // Basic: int + bool
    s1 := concat_all(42, true)
    if !s1.eq_cstr(c"42true") { return 1 }

    // Multiple types: int + bool + float
    s2 := concat_all(100, false, 3.14)
    if !s2.eq_cstr(c"100false3.14") { return 2 }

    // String argument
    hello := String.from_cstr(c"hello")
    s3    := concat_all(hello)
    if !s3.eq_cstr(c"hello") { return 3 }

    // Mixed: String + int
    s4 := concat_all(hello, 42)
    if !s4.eq_cstr(c"hello42") { return 4 }

    // Zero variadic args
    s5 := concat_all()
    if !s5.is_empty() { return 5 }

    0
}

// --- test_tostring_typed ---

test_tostring_typed() {
    x: CLongDouble  := 3.14
    y: CFloat       := 2.0
    z: CInt         := 42
    w: CUnsignedInt := 7

    s := concat_all(x, y, z, w)

    if !s.eq_cstr(c"3.142427") { return 1 }
    0
}

// --- test_trait_no_collapse ---

test_trait_no_collapse() {
    s1 := join_all(42, true)
    if !s1.eq_cstr(c"[42][true]") { return 1 }

    s2 := join_all()
    if !s2.is_empty() { return 2 }

    0
}

// --- test_values ---

values_sum_hashes(args: ...Hash) -> CSize {
    total: CSize := 0
    i:     CSize := 0
    while i < args.size, i = i + 1 {
        total = total + args.data.[i]
    }
    total
}

test_values() {
    // hash of a single value: verify we can access the actual values
    h1 := values_sum_hashes(42)
    if h1 == 0 { return 1 }

    // two values: sum should be different from each individual
    h2 := values_sum_hashes(42, 100)
    if h2 == h1 { return 2 }

    0
}

// --- test_zero_args ---

hash_count(args: ...Hash) -> CSize {
    args.size
}

test_zero_args() {
    h := hash_count()
    if h != 0 { return 1 }
    0
}

// --- main ---

main() {
    result := 0
    result = result + test_basic()
    result = result + test_autocollapse()
    result = result + test_cross_module()
    result = result + test_longdouble()
    result = result + test_mixed_param_alias()
    result = result + test_multiple_fixed()
    result = result + test_print()
    result = result + test_tostring()
    result = result + test_tostring_typed()
    result = result + test_trait_no_collapse()
    result = result + test_values()
    result = result + test_zero_args()
    result
}
