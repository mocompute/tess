// Comprehensive type alias tests — combined from individual test files.

// --- File 1: test_type_alias_fully_generic ---
// Fully generic type alias: Foo = Option (no type params bound).
#module TA1_App
Foo = Option

// --- File 2: test_type_alias_fully_generic_none ---
// Fully generic type alias: assigning None() to aliased Option.
#module TA2_App
Foo = Option

// --- File 3: test_type_alias_fully_generic_result ---
// Fully generic type alias for a multi-param type: Bar = Result
#module TA3_App
Bar = Result

// --- File 4: test_type_alias_generic ---
#module TA4_Foo
Point[T]: {x: T, y: T}
Pt = Point[Int]

// --- File 5: test_type_alias_local ---
#module TA5_Foo
Point: {x: Int, y: Int}
Pt = Point

// --- File 6: test_type_alias_module_chained ---
#module TA6_A
Base: {x: Int}

#module TA6_B
Alias1 = TA6_A.Base

#module TA6_C
Alias2 = TA6_B.Alias1

// --- File 7: test_type_alias_module_enum ---
#module TA7_Foo
Status: {Ok, Error, Pending}

#module TA7_Bar
Outcome = TA7_Foo.Status

// --- File 8: test_type_alias_module_multi_arg ---
#module TA8_Collections
Map[K, V]: { keys: Ptr[K], values: Ptr[V], size: Int }

#module TA8_App
StringIntMap = TA8_Collections.Map[CString, Int]

// --- File 9: test_type_alias_module_multi_arg_direct_compatible ---
#module TA9_Collections
Map[K, V]: { keys: Ptr[K], values: Ptr[V], size: Int }

#module TA9_App
StringIntMap = TA9_Collections.Map[CString, Int]

// --- File 10: test_type_alias_module_simple ---
#module TA10_Foo
Span[T]: { v: Ptr[T], sz: CSize }

#module TA10_Bar
Chars = TA10_Foo.Span[CChar]

// --- main module ---
#module main

test_fully_generic() {
    x: TA1_App.Foo[CInt] := Some(42)
    when x { s: Some { s.value - 42 } else { 0 } }
}

test_fully_generic_none() {
    n: TA2_App.Foo[Int] := None()
    when n { s: Some { 1 } else { 0 } }
}

test_fully_generic_result() {
    error := 0

    ok: TA3_App.Bar[Int, Int] := Ok(v = 42)
    r                         := when ok { o: Ok { o.value } e: Err { e.error } }
    error += if r == 42 { 0 } else { 1 }

    fail: TA3_App.Bar[Int, Int] := Err(v = -1)
    r2                          := when fail { o: Ok { o.value } e: Err { e.error } }
    error += if r2 == -1 { 0 } else { 1 }

    error
}

test_generic() {
    p   := TA4_Foo.Pt(x = 10, y = 20)
    res := p.x + p.y
    if (res == 30) { 0 } else { 1 }
}

test_local() {
    error := 0

    // Via type annotation: construct using original type, annotate with alias
    p1: TA5_Foo.Pt := TA5_Foo.Point(x = 10, y = 20)
    error += if p1.x + p1.y == 30 {0} else {1}

    // Direct usage: construct using local alias
    p2 := TA5_Foo.Pt(x = 10, y = 20)
    error += if p2.x + p2.y == 30 {0} else {1}

    error
}

test_module_chained() {
    error := 0

    // Via type annotation: construct using original type, annotate with chained alias
    v1: TA6_C.Alias2 := TA6_A.Base(x = 42)
    error += if v1.x == 42 {0} else {1}

    // Direct usage: construct using chained alias
    v2 := TA6_C.Alias2(x = 42)
    error += if v2.x == 42 {0} else {1}

    error
}

test_module_enum() {
    // Via type annotation: access enum through original type, annotate with alias
    r1: TA7_Bar.Outcome := TA7_Foo.Status.Ok

    // Direct usage: access enum values through the alias
    r2 := TA7_Bar.Outcome.Ok

    0
}

test_module_multi_arg() {
    // Via type annotation: construct using original type, annotate with alias
    m1: TA8_App.StringIntMap := TA8_Collections.Map(keys = null, values = null, size = 0)

    // Direct usage: construct using multi-argument generic alias
    m2 := TA8_App.StringIntMap(keys = null, values = null, size = 0)

    result: CInt := m1.size + m2.size
    result
}

test_module_multi_arg_direct_compatible() {
    // Direct usage: construct using multi-argument generic alias
    m  := TA9_App.StringIntMap(keys = null, values = null, size = 0)
    m2 := TA9_Collections.Map(keys: Ptr[CString] = null, values: Ptr[Int] = null, size = 0)
    m = m2
    result: CInt := m.size
    result
}

test_module_simple() {
    // Via type annotation: construct using original type, annotate with alias
    s1: TA10_Bar.Chars := TA10_Foo.Span(v = null, sz = 0zu)

    // Direct usage: construct using the alias as if it were the type constructor
    s2 := TA10_Bar.Chars(v = null, sz = 0zu)

    // Verify both sz fields are accessible and unsigned
    result: CSize := s1.sz + s2.sz
    if result != 0zu {1} else {0}
}

main() {
    error := 0
    error += test_fully_generic()
    error += test_fully_generic_none()
    error += test_fully_generic_result()
    error += test_generic()
    error += test_local()
    error += test_module_chained()
    error += test_module_enum()
    error += test_module_multi_arg()
    error += test_module_multi_arg_direct_compatible()
    error += test_module_simple()
    error
}
