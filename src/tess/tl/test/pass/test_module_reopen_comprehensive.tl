// File 1: test_module_reopen.tl
#module RF_Foo
FooData:                        { x: CInt        }
create_data(x: CInt) -> FooData { FooData(x = x) }

#module RF_Foo.Bar
BarData:                     { y: CInt        }
make_bar(y: CInt) -> BarData { BarData(y = y) }

#module RF_Foo

create_pair(x: CInt, y: CInt) -> FooData {
    bar := RF_Foo.Bar.make_bar(y)
    create_data(x + bar.y)
}

// File 2: test_module_reopen_cross_kind.tl
// Function and type with the same user-visible name in a module.
// Functions are arity-mangled, so they occupy a separate namespace from types.
#module RF_M_cross

f: { value: CInt }

#module RF_M_cross

f(x: CInt) -> CInt { s := f(value = x); s.value }

// File 3: test_module_reopen_generics.tl
// Test generic types and functions across module reopenings.
#module RF_M_gen

Box[T]: { v: T }

make_box[T](x: T) -> Box[T] { Box(v = x) }

#module RF_M_gen.Helper
helper() -> CInt { 1 }

#module RF_M_gen

// Reopened: add generic function that uses type from first opening
wrap[T](x: T) -> Box[T] { make_box(x) }

// Reopened: access generic type from original section
get_box_int(b: Box[CInt]) -> CInt { b.v }

// Cross-opening call
wrap_with_helper(x: CInt) -> Box[CInt] { wrap(x + RF_M_gen.Helper.helper()) }

// File 4: test_module_reopen_multiple.tl
// Test module opened 3 times with cross-opening calls and overloaded functions.
#module RF_M_multi

f(x: CInt) -> CInt { x + 1 }

#module RF_M_multi.Sub
helper() -> CInt { 100 }

#module RF_M_multi
// Second opening: add g, call f from first
g(x: CInt) -> CInt { f(x) + 10 }

// Overloaded: 2-arg version of f
f(x: CInt, y: CInt) -> CInt { x + y }

#module RF_M_multi
// Third opening: call functions from both prior openings
h(x: CInt) -> CInt { f(x) + g(x) + f(x, x) }

// File 5: test_module_reopen_submodule.tl
// Test reopening a submodule (not the parent).
#module RF_P

#module RF_P.S
SData:                   { v: CInt      }
create(v: CInt) -> SData { SData(v = v) }

#module RF_P
use_s(v: CInt) -> RF_P.S.SData { RF_P.S.create(v) }

#module RF_P.S
// Reopen submodule: add new function
double(d: SData) -> SData { SData(v = d.v * 2) }

// File 6: test_module_reopen_submodule_types.tl
// Test that dotted type names in synopsis declarations within reopened modules
// resolve correctly (left-to-right, not right-recursive).
#module RF_Outer

#module RF_Outer.Inner
InnerData:                 { value: CInt          }
make(v: CInt) -> InnerData { InnerData(value = v) }

#module RF_Outer
// Synopsis uses a 3-level dotted type name: RF_Outer.Inner.InnerData
wrap(d: RF_Outer.Inner.InnerData) -> CInt
wrap(d) { d.value }

get_inner() -> RF_Outer.Inner.InnerData
get_inner() { RF_Outer.Inner.make(42) }

// File 7: test_module_reopen_symbols.tl
#module RF_M_sym
f(x: CInt)          -> CInt { x     }
f(x: CInt, y: CInt) -> CInt { x + y }

#module RF_M_sym.Sub
g() -> CInt { 1 }

#module RF_M_sym
h(x: CInt) -> CInt { f(x) + f(x, 1) }

#module main

test_reopen() {
    error  := 0
    result := RF_Foo.create_pair(10, 32)
    if result.x - 42 != 0 { error += 1 }
    error
}

test_reopen_cross_kind() {
    error := 0
    x     := RF_M_cross.f(value = 10)
    if RF_M_cross.f(10) - x.value != 0 { error += 1 }
    error
}

test_reopen_generics() {
    error: CInt := 0

    // Specialize with Int
    bi := RF_M_gen.wrap(42)
    if RF_M_gen.get_box_int(bi) != 42 { error += 1 }

    // Specialize with Bool
    bb := RF_M_gen.wrap(true)
    if !bb.v { error += 1 }

    // Cross-opening function call
    bh := RF_M_gen.wrap_with_helper(10)
    if bh.v != 11 { error += 1 }

    error
}

test_reopen_multiple() {
    error := 0
    // h(5) = f(5) + g(5) + f(5,5) = 6 + 16 + 10 = 32
    if RF_M_multi.h(5) - 32 != 0 { error += 1 }
    error
}

test_reopen_submodule() {
    error: CInt := 0

    // Function from first submodule opening
    d := RF_P.S.create(10)
    if d.v != 10 { error += 1 }

    // Function from second submodule opening
    d2 := RF_P.S.double(d)
    if d2.v != 20 { error += 1 }

    // Parent function using submodule
    d3 := RF_P.use_s(5)
    if d3.v != 5 { error += 1 }

    error
}

test_reopen_submodule_types() {
    error := 0
    d     := RF_Outer.Inner.make(10)
    r     := RF_Outer.wrap(d)
    d2    := RF_Outer.get_inner()
    if r + d2.value - 52 != 0 { error += 1 }
    error
}

test_reopen_symbols() {
    error := 0
    if RF_M_sym.h(10) - 21 != 0 { error += 1 }
    error
}

main() {
    error := 0
    error += test_reopen()
    error += test_reopen_cross_kind()
    error += test_reopen_generics()
    error += test_reopen_multiple()
    error += test_reopen_submodule()
    error += test_reopen_submodule_types()
    error += test_reopen_symbols()
    error
}
