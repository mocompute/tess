// Combined alias tests from:
//   test_alias_basic.tl
//   test_alias_deep_dotted.tl
//   test_alias_dotted.tl
//   test_alias_file_boundary.tl
//   test_alias_funcptr.tl
//   test_alias_nested_type.tl
//   test_alias_unalias.tl

// --- Modules from test_alias_basic ---
#module AL_Foo1
value()     { 42    }
add  (a, b) { a + b }

// --- Modules from test_alias_deep_dotted ---
#module AL_A
// intermediate

#module AL_A.AL_B
// intermediate

#module AL_A.AL_B.AL_C
value() { 77 }

// --- Modules from test_alias_dotted ---
#module AL_Outer
// intermediate

#module AL_Outer.AL_Inner
value() { 99 }

// --- Modules from test_alias_file_boundary ---
#module AL_Foo2
value() { 55 }

#module AL_Bar2
other() { 33 }

#module AL_FileOne
#alias F AL_Foo2
use_alias() { F.value() }

#unity_file test_alias_comprehensive_part2.tl

#module AL_FileTwo
#alias F AL_Bar2
use_bar() { F.other() }

// --- Modules from test_alias_unalias ---
#module AL_Foo3
value() { 10 }

// --- Modules from test_alias_funcptr ---
#module AL_Lib
double(x: Int) -> Int { x * 2 }

// --- Modules from test_alias_nested_type ---
#module AL_Shapes
Shape: | Circle { radius: Int } | Square { length: Int }

// --- main ---
#module main

// test_alias_basic
#alias F1 AL_Foo1

test_basic() {
    x := F1.value()
    y := F1.add(1, 2)
    if x == 42 && y == 3 { 0 } else { 1 }
}

#unalias F1

// test_alias_deep_dotted
#alias X AL_A.AL_B.AL_C

test_deep_dotted() {
    if X.value() == 77 { 0 } else { 1 }
}

#unalias X

// test_alias_dotted
#alias OI AL_Outer.AL_Inner

test_dotted() {
    if OI.value() == 99 { 0 } else { 1 }
}

#unalias OI

// test_alias_file_boundary
test_file_boundary() {
    x := AL_FileOne.use_alias()
    y := AL_FileTwo.use_bar()
    if x == 55 && y == 33 { 0 } else { 1 }
}

// test_alias_funcptr
#alias L AL_Lib

test_funcptr() {
    fp           := L.double/1
    result: CInt := fp(21) - 42
    result
}

#unalias L

// test_alias_nested_type
#alias S AL_Shapes

test_nested_type() {
    c            := S.Circle(1)
    result: CInt := when c {
        ci: Circle { ci.radius - 1 }
        sq: Square { 1 }
    }
    result
}

#unalias S

// test_alias_unalias
#alias F3 AL_Foo3
test_unalias_aliased() { F3.value() }
#unalias F3
test_unalias_original() { AL_Foo3.value() }

test_unalias() {
    x := test_unalias_aliased()
    y := test_unalias_original()
    if x == 10 && y == 10 { 0 } else { 1 }
}

main() {
    r := test_basic()
    r = r + test_deep_dotted()
    r = r + test_dotted()
    r = r + test_file_boundary()
    r = r + test_funcptr()
    r = r + test_nested_type()
    r = r + test_unalias()
    r
}
