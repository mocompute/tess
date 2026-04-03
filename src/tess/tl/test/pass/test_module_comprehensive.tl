// Test: auto-collapse works when a module's primary type is named T.
// A module type named T should be usable via the bare module name as a
// constructor and in type positions, just like same-name types (e.g. Array.Array).

#module Vec2

T: { x: Int, y: Int }

add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y) }

#module Vec3

T: { x: Int, y: Int, z: Int }

add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z) }

#module ModuleOne

foo := 123

f(a, b) { a + b }


#module ModuleTwo

f(a, b) { a - b }


#module TestInit

// Global variable initialized to a sentinel value
initialized_value := 0

// Module init function - should run before main()
__init() {
    initialized_value = 42
}

get_value() { initialized_value }

#module FooNested
f1() { 1 }

#module FooNested.Bar
// declares intermediate module to allow another level of submodule

#module FooNested.Bar.Baz
f2() { 2                    }
f3() { FooNested.f1() + 10 }

// Bug: module-prefixed nullary tagged union variant not auto-invoked.
// Opt.Empty should produce a value, not a function reference.

#module Opt_basic
T: | Has { v: Int } | Empty

// Ensure module-prefixed nullary variant works with non-T type names
// (no auto-collapse interaction).

#module Opt_named
MyOption: | Has { v: Int } | Empty

// Bug: module-prefixed nullary tagged union variant (generic) not auto-invoked.

#module Opt_generic
T[a]: | Has { v: a } | Empty

#import <Hash.tl>

#module main
#import <Array.tl>
#import <HashMap.tl>

// Test: bare Array[T] in function return type
make_array() -> Array[Int] {
    a := Array.empty[Int]()
    Array.push(a.&, 1)
    Array.push(a.&, 2)
    a
}

test_return_type() -> CInt {
    error: CInt := 0
    a           := make_array()
    if a.size != 2zu { error += 1 }
    if a.data.[0] != 1 { error += 1 }
    if a.data.[1] != 2 { error += 1 }
    Array.free(a.&)
    error
}

// Test: bare Array[T] in function parameter type
sum_array(a: Array[Int]) -> Int {
    total    := 0
    i: CSize := 0zu
    while i < a.size, i += 1zu { total += a.data.[i] }
    total
}

test_param_type() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 10)
    Array.push(a.&, 20)
    Array.push(a.&, 30)
    if sum_array(a) != 60 { error += 1 }
    Array.free(a.&)
    error
}

// Test: explicit Array.Array[T] still works alongside the alias
identity_array(a: Array.Array[Int]) -> Array.Array[Int] { a }

test_explicit_qualified() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 99)
    b := identity_array(a)
    if b.data.[0] != 99 { error += 1 }
    Array.free(b.&)
    error
}

// Test: bare and explicit are interchangeable
convert(a: Array[Int]) -> Array.Array[Int] { a }

test_interchangeable() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 7)
    b := convert(a)
    if b.data.[0] != 7 { error += 1 }
    Array.free(b.&)
    error
}

test_auto_collapse() {
    error := 0
    error += test_return_type()
    error += test_param_type()
    error += test_explicit_qualified()
    error += test_interchangeable()
    error
}

// Test: bare HashMap[K, V] in function return type
make_map() -> Ptr[HashMap[Int, Int]] {
    m := HashMap.create[Int, Int]()
    m.set(1, 100)
    m
}

// Test: bare HashMap[K, V] in function parameter type
map_size(m: Ptr[HashMap[Int, Int]]) -> CSize {
    m.size()
}

// Test: explicit HashMap.HashMap[K, V] still works
map_size_qualified(m: Ptr[HashMap.HashMap[Int, Int]]) -> CSize {
    m.size()
}

// Test: bare and qualified are interchangeable
convert_map(m: Ptr[HashMap[Int, Int]]) -> Ptr[HashMap.HashMap[Int, Int]] { m }

test_auto_collapse_hashmap() {
    error := 0
    m     := make_map()
    if map_size(m) != 1zu { error += 1 }
    if map_size_qualified(m) != 1zu { error += 1 }
    if map_size(convert_map(m)) != 1zu { error += 1 }
    p := m.get(1).or_else(null)
    if p == null { error += 1 }
    else if p.* != 100 { error += 1 }
    m.destroy()
    error
}

// Test: bare Vec2 as constructor (the key auto-collapse use case)
test_constructor() -> CInt {
    error: CInt := 0
    v           := Vec2(x = 10, y = 20)
    if v.x != 10 { error += 1 }
    if v.y != 20 { error += 1 }
    error
}

// Test: bare Vec3 as constructor
test_constructor_vec3() -> CInt {
    error: CInt := 0
    v           := Vec3(x = 1, y = 2, z = 3)
    if v.x != 1 { error += 1 }
    if v.y != 2 { error += 1 }
    if v.z != 3 { error += 1 }
    error
}

// Test: explicit Vec2.T still works alongside the alias
test_explicit() -> CInt {
    error: CInt := 0
    v           := Vec2.T(x = 5, y = 6)
    if v.x != 5 { error += 1 }
    error
}

// Test: bare and explicit are interchangeable
convert_vec(v: Vec2.T) -> Vec2.T { v }

test_interchangeable_vec() -> CInt {
    error: CInt := 0
    v           := Vec2(x = 7, y = 8)
    v2          := convert_vec(v)
    if v2.x != 7 { error += 1 }
    if v2.y != 8 { error += 1 }
    error
}

test_auto_collapse_T() {
    error := 0
    error += test_constructor()
    error += test_constructor_vec3()
    error += test_explicit()
    error += test_interchangeable_vec()
    error
}

test_basic() {
    error := 0
    error += ModuleOne.f(-1, 1)
    error
}

test_init() {
    error := 0
    error += if TestInit.get_value() == 42 {0} else {1}
    error
}

test_nested() {
    x1 := FooNested.f1()
    x2 := FooNested.Bar.Baz.f2()
    x3 := FooNested.Bar.Baz.f3()
    x1 + x2 + x3 - 14
}

test_nullary_variant() {
    a     := Opt_basic.Has(42)
    b     := Opt_basic.Empty
    error := 0
    error += when a { s: Has { 0 } n: Empty { 1 } }
    error += when b { s: Has { 1 } n: Empty { 0 } }
    error
}

test_nullary_variant_named() {
    a     := Opt_named.Has(42)
    b     := Opt_named.Empty
    error := 0
    error += when a { s: Has { 0 } n: Empty { 1 } }
    error += when b { s: Has { 1 } n: Empty { 0 } }
    error
}

test_nullary_variant_generic() {
    a                  := Opt_generic.Has(42)
    b: Opt_generic[Int] := Opt_generic.Empty
    error               := 0
    error += when a { s: Has { 0 } n: Empty { 1 } }
    error += when b { s: Has { 1 } n: Empty { 0 } }
    error
}

main() {
    error := 0
    error += test_auto_collapse()
    error += test_auto_collapse_hashmap()
    error += test_auto_collapse_T()
    error += test_basic()
    error += test_init()
    error += test_nested()
    error += test_nullary_variant()
    error += test_nullary_variant_named()
    error += test_nullary_variant_generic()
    error
}
