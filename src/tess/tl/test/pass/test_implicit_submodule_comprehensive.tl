// Test bare submodule name as type (auto-collapse).
// In #module IS_Parent_bare, bare c"Child" as a type resolves to IS_Parent_bare.Child.Child.
#module IS_Parent_bare

#module IS_Parent_bare.Child
Child:                   { x: CInt      }
create(v: CInt) -> Child { Child(x = v) }

#module IS_Parent_bare

// Bare c"Child" in type position resolves to IS_Parent_bare.Child.Child
wrap (v: CInt)  -> Child { Child.create(v) }
get_x(c: Child) -> CInt  { c.x             }

// Test bare submodule type constructor in expression position.
// In #module IS_Parent_ctor, bare c"Child(...)" resolves to IS_Parent_ctor.Child.Child(...).
#module IS_Parent_ctor

#module IS_Parent_ctor.Child
Child: { x: CInt, y: CInt }

#module IS_Parent_ctor
make() -> Child { Child(x = 10, y = 20) }

// Test three-level deep submodule nesting with implicit resolution.
#module IS_A

#module IS_A.B
BData:                     { x: CInt      }
create_b(v: CInt) -> BData { BData(x = v) }

#module IS_A.B.C
CData:                     { y: CInt      }
create_c(v: CInt) -> CData { CData(y = v) }

#module IS_A.B
// In IS_A.B: bare C.func() resolves to IS_A.B.C
use_c(v: CInt) -> C.CData { C.create_c(v) }

#module IS_A
// In IS_A: B.func() resolves to IS_A.B (depth 1)
use_b(v: CInt) -> B.BData { B.create_b(v) }

// In IS_A: B.C.func() resolves to IS_A.B.C (depth 2)
use_bc(v: CInt) -> B.C.CData { B.C.create_c(v) }

// Test generic types in implicit submodules: specialization cache, UFCS, bare Child.Wrapper[T].
#module IS_Parent_gen

#module IS_Parent_gen.Child

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] { Wrapper(v = x) }

unwrap[T](w: Wrapper[T]) -> T { w.v }

#module IS_Parent_gen

// Type position: bare Child.Wrapper[CInt]
make_int(x: CInt) -> Child.Wrapper[CInt] { Child.wrap(x) }

// Expression position: Child.Wrapper(v = ...)
make_bool(b: Bool) -> Child.Wrapper[Bool] { Child.Wrapper(v = b) }

// UFCS on generic submodule type
get_int(w: Child.Wrapper[CInt]) -> CInt { w.unwrap() }

// Test interaction: implicit submodule resolution across module reopenings.
#module IS_Parent_reopen

#module IS_Parent_reopen.Child
Data:                   { x: CInt     }
create(v: CInt) -> Data { Data(x = v) }

#module IS_Parent_reopen
// First use of implicit Child
first(v: CInt) -> Child.Data { Child.create(v) }

#module IS_Parent_reopen.Child
// Reopen submodule itself: add more functions
double(d: Data) -> Data { Data(x = d.x * 2) }

#module IS_Parent_reopen
// Reopened parent: implicit Child still works, including new functions
second(v: CInt) -> CInt {
    d  := Child.create(v)
    d2 := Child.double(d)
    d2.x
}

// Test sibling submodules: both accessible via bare name, no cross-collision.
#module IS_Parent_sib

#module IS_Parent_sib.A
Data:                   { x: CInt     }
create(x: CInt) -> Data { Data(x = x) }

#module IS_Parent_sib.B
Data:                   { y: CInt     }
create(y: CInt) -> Data { Data(y = y) }

#module IS_Parent_sib

// Both A.Data and B.Data accessible without conflict
combine(x: CInt, y: CInt) -> CInt {
    a := A.create(x)
    b := B.create(y)
    a.x + b.y
}

// Synopsis with sibling types
transform(a: A.Data) -> B.Data
transform(a) { B.create(a.x * 2) }

// Test implicit submodule with tagged unions, UFCS, and synopsis.
// Mirrors CommandLine.tl patterns: parent module uses Child.Variant, Child.func.
#module IS_Parent_tu

#module IS_Parent_tu.Child
Value: | IntVal { v: CInt }
       | Empty

create(v: CInt) -> Value { IntVal(v = v) }

get(val: Value) -> CInt {
    when val {
        i: IntVal { i.v }
        e: Empty { 0 }
    }
}

#module IS_Parent_tu

// Synopsis using implicit submodule dotted type
wrap(v: CInt) -> Child.Value
wrap(v) { Child.create(v) }

// Use implicit submodule variant constructors
make_empty() -> Child.Value { Child.Empty }

// UFCS on submodule type
extract(val: Child.Value) -> CInt { val.get() }

// Top-level module should take precedence over implicit submodule resolution.
// Bare c"IS_Foo_tp" in #module IS_Parent_tp should resolve to the top-level IS_Foo_tp.IS_Foo_tp,
// not IS_Parent_tp.IS_Foo_tp.IS_Foo_tp.

#module IS_Foo_tp
IS_Foo_tp: { x: CInt }

#module IS_Parent_tp

#module IS_Parent_tp.IS_Foo_tp
IS_Foo_tp: { y: CInt }

#module IS_Parent_tp

// Type position: IS_Foo_tp should be top-level IS_Foo_tp.IS_Foo_tp (has field x)
wrap_top(v: CInt) -> IS_Foo_tp { IS_Foo_tp.IS_Foo_tp(x = v) }

// Expression position: IS_Foo_tp(...) should be top-level IS_Foo_tp.IS_Foo_tp
make_top(v: CInt) -> IS_Foo_tp { IS_Foo_tp(x = v) }

// Explicit IS_Parent_tp.IS_Foo_tp.IS_Foo_tp for the submodule version
make_sub(v: CInt) -> IS_Parent_tp.IS_Foo_tp.IS_Foo_tp { IS_Parent_tp.IS_Foo_tp.IS_Foo_tp(y = v) }

#module main

test_bare_type() {
    error := IS_Parent_bare.get_x(IS_Parent_bare.wrap(42)) - 42
    error
}

test_constructor() {
    c := IS_Parent_ctor.make()
    c.x + c.y - 30
}

test_deep() {
    error: CInt := 0

    // Depth 1: IS_A -> B
    bd := IS_A.use_b(10)
    if bd.x != 10 { error += 1 }

    // Depth 2: IS_A -> B.C
    cd := IS_A.use_bc(20)
    if cd.y != 20 { error += 1 }

    // B -> C (depth 1 from B's perspective)
    cd2 := IS_A.B.use_c(30)
    if cd2.y != 30 { error += 1 }

    error
}

test_generic() {
    error: CInt := 0

    // Multiple specializations via implicit submodule
    wi := IS_Parent_gen.make_int(42)
    wb := IS_Parent_gen.make_bool(true)

    if wi.v != 42 { error += 1 }
    if !wb.v { error += 1 }

    // UFCS through parent module
    if IS_Parent_gen.get_int(wi) != 42 { error += 1 }

    // Direct UFCS on Child.Wrapper value
    if wi.unwrap() != 42 { error += 1 }

    error
}

test_reopen() {
    error: CInt := 0

    d := IS_Parent_reopen.first(10)
    if d.x != 10 { error += 1 }

    if IS_Parent_reopen.second(21) != 42 { error += 1 }

    error
}

test_siblings() {
    error: CInt := 0

    if IS_Parent_sib.combine(10, 20) != 30 { error += 1 }

    a := IS_Parent_sib.A.create(5)
    b := IS_Parent_sib.transform(a)
    if b.y != 10 { error += 1 }

    error
}

test_tagged_union() {
    v := IS_Parent_tu.wrap(42)
    e := IS_Parent_tu.make_empty()
    IS_Parent_tu.extract(v) + IS_Parent_tu.extract(e) - 42
}

test_toplevel_precedence() {
    t := IS_Parent_tp.make_top(10)
    s := IS_Parent_tp.make_sub(20)
    t.x + s.y - 30
}

main() {
    error := 0
    error += test_bare_type()
    error += test_constructor()
    error += test_deep()
    error += test_generic()
    error += test_reopen()
    error += test_siblings()
    error += test_tagged_union()
    error += test_toplevel_precedence()
    error
}
