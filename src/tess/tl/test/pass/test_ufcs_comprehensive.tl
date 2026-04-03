// Combined UFCS tests — covers basic UFCS, chaining, implicit address-of,
// cross-module resolution, generics, submodules, functional chains, HashMap,
// String, non-struct receivers, Ptr[Const] dispatch, and user-defined modules.

// ── Non-main modules ──────────────────────────────────────────────────────

// From test_ufcs_module.tl
#module veclib_mod

VecMod2: { x: Int, y: Int }

length_sq(v: VecMod2) -> Int {
    v.x * v.x + v.y * v.y
}

scale(v: VecMod2, s: Int) -> VecMod2 {
    VecMod2(x = v.x * s, y = v.y * s)
}

// From test_ufcs_non_struct_receiver.tl
#module mymath

square(n: Int) -> Int { n * n }
double(n: Int) -> Int { n + n }

// From test_ufcs_implicit_submodule_generic.tl
#module SubGenParent

#module SubGenParent.Child
Data:                      { x: CInt           }
create(v: CInt) -> Data    { Data(x = v)       }
transform(d: Data) -> Data { Data(x = d.x * 2) }
get_value(d: Data) -> CInt { d.x               }

Wrapper[T]:                   { v: T           }
wrap[T](x: T) -> Wrapper[T]   { Wrapper(v = x) }
unwrap[T](w: Wrapper[T]) -> T { w.v            }

#module SubGenParent

// UFCS chain through implicit submodule
chain_test(v: CInt) -> CInt {
    Child.create(v).transform().get_value()
}

// UFCS on generic submodule type
unwrap_int(w: Child.Wrapper[CInt]) -> CInt { w.unwrap() }

// From test_ufcs_submodule.tl
#module SubParent

#module SubParent.Child
MyType:                                 { x: CInt       }
create(v: CInt) -> MyType               { MyType(x = v) }
get_x(self: Ptr[Const[MyType]]) -> CInt { self->x       }

// From test_ufcs_user_module.tl
#module Geom

Vec3: { x: Int, y: Int, z: Int }

length_sq(v: Vec3) -> Int {
    v.x * v.x + v.y * v.y + v.z * v.z
}

scale(v: Vec3, s: Int) -> Vec3 {
    Vec3(x = v.x * s, y = v.y * s, z = v.z * s)
}

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

set_x(v: Ptr[Vec3], x: Int) -> Void {
    v->x = x
}

reset(v: Ptr[Vec3]) -> Void {
    v->x = 0
    v->y = 0
    v->z = 0
}

// ── Main module ───────────────────────────────────────────────────────────

#module main
#import <Array.tl>
#import <String.tl>
#import <Hash.tl>
#import <HashMap.tl>

// ── Types and helpers for test_ufcs ───────────────────────────────────────

BasicVec2: { x: Int, y: Int }

basic_vec2_length_sq(v: BasicVec2) -> Int {
    v.x * v.x + v.y * v.y
}

basic_vec2_add(a: BasicVec2, b: BasicVec2) -> BasicVec2 {
    BasicVec2(x = a.x + b.x, y = a.y + b.y)
}

basic_vec2_scale(v: BasicVec2, s: Int) -> BasicVec2 {
    BasicVec2(x = v.x * s, y = v.y * s)
}

BasicPair[A, B]: { a: A, b: B }

basic_pair_sum(p: BasicPair[Int, Int]) -> Int { p.a + p.b }

basic_identity[T](x: T) -> T { x }

basic_apply_length_sq[T](x: T) -> Int {
    x.basic_vec2_length_sq()
}

basic_apply_identity[T](x: T) -> T {
    x.basic_identity()
}

basic_vec2_length_sq_ptr(v: Ptr[BasicVec2]) -> Int {
    v->x * v->x + v->y * v->y
}

basic_vec2_scale_ptr(v: Ptr[BasicVec2], s: Int) -> BasicVec2 {
    BasicVec2(x = v->x * s, y = v->y * s)
}

// ── Types and helpers for test_ufcs_implicit_addr ─────────────────────────

AddrVec2: { x: Int, y: Int }

addr_vec2_set_x(self: Ptr[AddrVec2], new_x: Int) -> Void {
    self->x = new_x
}

addr_vec2_set_y(self: Ptr[AddrVec2], new_y: Int) -> Void {
    self->y = new_y
}

addr_vec2_reset(self: Ptr[AddrVec2]) -> Void {
    self->x = 0
    self->y = 0
}

addr_vec2_length_sq(v: AddrVec2) -> Int {
    v.x * v.x + v.y * v.y
}

addr_swap[T](a: Ptr[T], b: Ptr[T]) -> Void {
    tmp := a.*
    a.* = b.*
    b.* = tmp
}

// ── Helpers for test_ufcs_generic_implicit_module ─────────────────────────

gi_count[T](arr: Array[T]) -> CSize {
    arr.size
}

gi_is_short[T](arr: Array[T]) -> Bool {
    arr.is_empty()
}

gi_first[T](arr: Array[T]) -> T {
    arr.front()
}

// ── Helper for test_ufcs_dot_call_unannotated_param ──────────────────────

_count_dotcall(content) {
    i:  CSize := 0
    ch: Some  := content.char_at(i) else { return 1 }
    0
}

// ── Helper for test_ufcs_ptr_const ───────────────────────────────────────

_takes_ptr_const(s: Ptr[Const[String]]) -> CInt {
    n := s.len()
    c := s.copy(0, n)
    if c.len() != n { return 1 }
    0
}

// ── Test functions ────────────────────────────────────────────────────────

test_ufcs() {
    error := 0

    v := BasicVec2(x = 3, y = 4)

    // Test 1: UFCS basic call
    len_sq := v.basic_vec2_length_sq()
    error += if len_sq == 25 {0} else {1}

    // Test 2: UFCS with extra arguments
    v2 := v.basic_vec2_scale(2)
    error += if v2.x == 6 {0} else {1}
    error += if v2.y == 8 {0} else {1}

    // Test 3: UFCS with two struct arguments
    v3 := v.basic_vec2_add(v2)
    error += if v3.x == 9 {0} else {1}
    error += if v3.y == 12 {0} else {1}

    // Test 4: Struct field access still works
    error += if v.x == 3 {0} else {1}
    error += if v.y == 4 {0} else {1}

    // Test 5: UFCS chaining
    chained := v.basic_vec2_scale(3).basic_vec2_length_sq()
    error += if chained == 225 {0} else {1}

    // Test 6: UFCS on generic struct
    p := BasicPair(a = 3, b = 7)
    error += if p.basic_pair_sum() == 10 {0} else {1}

    // Test 9: UFCS with generic function
    v5 := v.basic_identity()
    error += if v5.x == 3 {0} else {1}
    error += if v5.y == 4 {0} else {1}

    // Test 10: UFCS inside generic function
    error += if basic_apply_length_sq(v) == 25 {0} else {1}

    // Test 11: UFCS with generic function inside generic function
    v6 := v.basic_apply_identity()
    error += if v6.x == 3 {0} else {1}
    error += if v6.y == 4 {0} else {1}

    error
}

test_ufcs_chained_module_resolve() {
    error := 0

    // 1. Same-module chain: String → String → String
    s := String.from_cstr(c" HELLO ").trim().to_lower()
    error += if s.len() == 5zu {0} else {1}

    // 2. Cross-module: String.split → Array[String]
    parts := String.from_cstr(c"a,b,c").split(String.from_cstr(c","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).len() == 1zu {0} else {1}

    // 3. Full round-trip chain: String → Array → String
    first_len := String.from_cstr(c"hello,world").split(String.from_cstr(c",")).get(0zu).len()
    error += if first_len == 5zu {0} else {1}

    // 4. Array methods after cross-module result
    words := String.from_cstr(c"one,two,three").split(String.from_cstr(c","))
    words.push(String.from_cstr(c"four"))
    error += if words.size == 4zu {0} else {1}

    words.free()
    parts.free()

    error
}

test_ufcs_dot_call_unannotated_param() {
    _count_dotcall("hello")
}

test_ufcs_functional() {
    error := 0

    arr := Array.empty[Int]()
    arr.push(1)
    arr.push(2)
    arr.push(3)
    arr.push(4)
    arr.push(5)

    // map — double each element
    doubled := arr.map((x) { x * 2 })
    error += if doubled.size == 5zu {0} else {1}
    error += if doubled.get(0zu) == 2 {0} else {1}
    error += if doubled.get(4zu) == 10 {0} else {1}

    // filter — keep elements > 3
    big := arr.filter((x) { x > 3 })
    error += if big.size == 2zu {0} else {1}
    error += if big.get(0zu) == 4 {0} else {1}

    // reduce — sum
    sum := arr.reduce(0, (a, b) { a + b })
    error += if sum == 15 {0} else {1}

    // any_of / all
    error += if arr.any_of((x) { x > 4 }) == true {0} else {1}
    error += if arr.any_of((x) { x > 10 }) == false {0} else {1}
    error += if arr.all((x) { x > 0 }) == true {0} else {1}
    error += if arr.all((x) { x > 3 }) == false {0} else {1}

    // foreach — side effect
    count := 0
    arr.foreach((x) { count = count + 1 })
    error += if count == 5 {0} else {1}

    // Chained: map then filter then reduce
    result := arr.map((x) { x * 2 }).filter((x) { x > 4 }).reduce(0, (a, b) { a + b })
    error += if result == 24 {0} else {1}

    // sorted + chained access
    arr2 := Array.empty[Int]()
    arr2.push(5)
    arr2.push(1)
    arr2.push(3)
    sorted := arr2.sorted((a, b) { a - b })
    error += if sorted.get(0zu) == 1 {0} else {1}
    error += if sorted.get(2zu) == 5 {0} else {1}

    // cleanup
    doubled.free()
    big.free()
    sorted.free()
    arr2.free()
    arr.free()

    error
}

test_ufcs_generic_implicit_module() {
    error := 0

    // Instantiate with Int
    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)

    error += if arr.gi_count() == 2 {0} else {1}
    error += if arr.gi_is_short() == false {0} else {1}
    error += if arr.gi_first() == 10 {0} else {1}

    // Instantiate with Bool
    barr := Array.empty[Bool]()
    barr.push(true)

    error += if barr.gi_count() == 1 {0} else {1}
    error += if barr.gi_is_short() == false {0} else {1}
    error += if barr.gi_first() == true {0} else {1}

    // Empty array
    empty := Array.empty[Int]()
    error += if empty.gi_is_short() == true {0} else {1}

    arr.free()
    barr.free()

    error
}

test_ufcs_hashmap() {
    error := 0

    m := HashMap.create[Int, Int]()

    // Nullary on Ptr — size, is_empty
    error += if m.size() == 0zu {0} else {1}
    error += if m.is_empty() == true {0} else {1}

    // set, contains, get
    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)
    error += if m.size() == 3zu {0} else {1}
    error += if m.is_empty() == false {0} else {1}

    error += if m.contains(1) == true {0} else {1}
    error += if m.contains(99) == false {0} else {1}

    // get returns Option[Ptr[V]]
    p := when m.get(2) {
        s: Some { s.value }
        n: None { null }
    }
    error += if p != null && p.* == 200 {0} else {1}

    // get_copy returns Option[V]
    opt := m.get_copy(3)
    val := when opt {
        s: Some { s.value }
        n: None { -1 }
    }
    error += if val == 300 {0} else {1}

    none_opt := m.get_copy(99)
    none_val := when none_opt {
        s: Some { s.value }
        n: None { -1 }
    }
    error += if none_val == -1 {0} else {1}

    // remove
    error += if m.remove(2) == true {0} else {1}
    error += if m.size() == 2zu {0} else {1}
    error += if m.contains(2) == false {0} else {1}

    // keys — returns Array[K], then chain cross-module
    ks := m.keys()
    error += if ks.size == 2zu {0} else {1}
    ks.free()

    // clear
    m.clear()
    error += if m.size() == 0zu {0} else {1}

    m.destroy()

    error
}

test_ufcs_implicit_addr() {
    error := 0

    // Test 1: Basic implicit address-of
    v := AddrVec2(x = 3, y = 4)
    v.addr_vec2_set_x(10)
    error += if v.x == 10 {0} else {1}

    // Test 2: Multiple mutating calls in sequence
    v.addr_vec2_set_x(100)
    v.addr_vec2_set_y(200)
    error += if v.x == 100 {0} else {1}
    error += if v.y == 200 {0} else {1}

    // Test 3: Nullary mutating call
    v.addr_vec2_reset()
    error += if v.x == 0 {0} else {1}
    error += if v.y == 0 {0} else {1}

    // Test 4: Non-Ptr first param unchanged
    v2 := AddrVec2(x = 3, y = 4)
    len := v2.addr_vec2_length_sq()
    error += if len == 25 {0} else {1}

    // Test 5: Mixed explicit and implicit
    v3 := AddrVec2(x = 1, y = 2)
    addr_vec2_set_x(v3.&, 50)
    v3.addr_vec2_set_y(60)
    error += if v3.x == 50 {0} else {1}
    error += if v3.y == 60 {0} else {1}

    // Test 6: Ptr receiver with . — should NOT double-wrap
    v4 := AddrVec2(x = 5, y = 6)
    p  := v4.&
    p.addr_vec2_set_x(99)
    error += if v4.x == 99 {0} else {1}

    error
}

test_ufcs_implicit_addr_cross_module() {
    error := 0

    // Test 1: Basic push via implicit module UFCS
    arr := Array.empty[Int]()
    arr.push(10)
    error += if arr.size == 1zu {0} else {1}

    // Test 2: Multiple pushes
    arr.push(20)
    arr.push(30)
    error += if arr.size == 3zu {0} else {1}

    // Test 3: Verify values were actually pushed
    error += if arr.get(0zu) == 10 {0} else {1}
    error += if arr.get(1zu) == 20 {0} else {1}
    error += if arr.get(2zu) == 30 {0} else {1}

    // Test 4: Mixed explicit and implicit
    Array.push(arr.&, 40)
    arr.push(50)
    arr.Array.push(60)
    error += if arr.size == 6zu {0} else {1}
    error += if arr.get(3zu) == 40 {0} else {1}
    error += if arr.get(4zu) == 50 {0} else {1}
    error += if arr.get(5zu) == 60 {0} else {1}

    // Test 5: pop via implicit module UFCS
    top := arr.pop()
    error += if top == 60 {0} else {1}
    error += if arr.size == 5zu {0} else {1}

    // Test 6: Non-Ptr first param still works
    val := arr.get(0zu)
    error += if val == 10 {0} else {1}

    arr.free()

    error
}

test_ufcs_implicit_addr_cross_module_nullary() {
    error := 0

    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)

    // Implicit module + implicit & on nullary mutating function
    arr.clear()
    error += if arr.size == 0zu {0} else {1}

    arr.push(30)

    // Explicit .Array. still works as alternative
    arr.Array.push(40)
    error += if arr.size == 2zu {0} else {1}

    arr.free()

    error
}

test_ufcs_implicit_submodule_generic() {
    error: CInt := 0

    // UFCS chain
    if SubGenParent.chain_test(5) != 10 { error += 1 }

    // UFCS on generic type from implicit submodule
    w := SubGenParent.Child.wrap(99)
    if SubGenParent.unwrap_int(w) != 99 { error += 1 }

    // Direct UFCS on Child.Data
    d := SubGenParent.Child.create(7)
    if d.transform().get_value() != 14 { error += 1 }

    error
}

test_ufcs_module() {
    error := 0

    v := veclib_mod.VecMod2(x = 3, y = 4)

    // Test 1: cross-module UFCS
    len_sq := v.veclib_mod.length_sq()
    error += if len_sq == 25 {0} else {1}

    // Test 2: cross-module UFCS with extra argument
    v2 := v.veclib_mod.scale(2)
    error += if v2.x == 6 {0} else {1}
    error += if v2.y == 8 {0} else {1}

    // Test 3: chaining cross-module UFCS
    chained := v.veclib_mod.scale(3).veclib_mod.length_sq()
    error += if chained == 225 {0} else {1}

    error
}

test_ufcs_non_struct_receiver() {
    error := 0
    n     := 5

    error = error + if n.mymath.square() == 25 { 0 } else { 1 }
    error = error + if n.mymath.double() == 10 { 0 } else { 1 }
    m := 3
    error = error + if m.mymath.square() == 9 { 0 } else { 1 }

    error
}

test_ufcs_ptr_const() {
    greeting := "hello"
    _takes_ptr_const(greeting.&)
}

test_ufcs_string() {
    error := 0

    s := String.from_cstr(c"Hello, World!")

    // Queries — value receiver
    error += if s.len() == 13zu {0} else {1}
    error += if s.is_empty() == false {0} else {1}

    empty := String.empty()
    error += if empty.is_empty() == true {0} else {1}

    // Comparison — value receiver
    s2 := String.from_cstr(c"Hello, World!")
    error += if s.eq(s2) == true {0} else {1}

    // Predicates — value receiver
    error += if s.starts_with(String.from_cstr(c"Hello")) == true {0} else {1}
    error += if s.ends_with(String.from_cstr(c"World!")) == true {0} else {1}
    error += if s.contains(String.from_cstr(c"lo, W")) == true {0} else {1}
    error += if s.contains(String.from_cstr(c"xyz")) == false {0} else {1}

    // Transformations — value receiver, returns new String
    upper := String.from_cstr(c"hello").to_upper()
    error += if upper.eq(String.from_cstr(c"HELLO")) {0} else {1}

    lower := String.from_cstr(c"HELLO").to_lower()
    error += if lower.eq(String.from_cstr(c"hello")) {0} else {1}

    trimmed := String.from_cstr(c"  hi  ").trim()
    error += if trimmed.eq(String.from_cstr(c"hi")) {0} else {1}

    // Cross-module return: split returns Array[String]
    parts := String.from_cstr(c"a,b,c").split(String.from_cstr(c","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).eq(String.from_cstr(c"a")) {0} else {1}
    error += if parts.get(2zu).eq(String.from_cstr(c"c")) {0} else {1}
    parts.free()

    // Mutation: push — Ptr receiver, implicit & + implicit module
    greeting := String.from_cstr(c"Hello")
    greeting.push(String.from_cstr(c", World!"))
    error += if greeting.eq(String.from_cstr(c"Hello, World!")) {0} else {1}

    error
}

test_ufcs_submodule() {
    v := SubParent.Child.create(42)
    v.get_x() - 42
}

test_ufcs_type_module_resolve() {
    error := 0

    arr := Array.empty[Int]()

    // Push via implicit module (Ptr receiver, implicit &)
    arr.push(10)
    arr.push(20)
    arr.push(30)
    arr.push(40)
    error += if arr.size == 4zu {0} else {1}

    // Mixed: explicit and implicit
    arr.Array.push(50)
    error += if arr.size == 5zu {0} else {1}

    // Value receiver — element access
    error += if arr.get(0zu) == 10 {0} else {1}
    error += if arr.get(4zu) == 50 {0} else {1}
    error += if arr.front() == 10 {0} else {1}
    error += if arr.back() == 50 {0} else {1}

    // Value receiver — queries
    error += if arr.is_empty() == false {0} else {1}
    error += if arr.contains(30) == true {0} else {1}
    error += if arr.contains(99) == false {0} else {1}

    // Ptr receiver — insert (multiple args)
    arr.insert(0zu, 5)
    error += if arr.get(0zu) == 5 {0} else {1}
    error += if arr.size == 6zu {0} else {1}

    // Ptr receiver — remove
    removed := arr.remove(0zu)
    error += if removed == 5 {0} else {1}
    error += if arr.size == 5zu {0} else {1}

    // Ptr receiver — reverse (nullary mutating)
    arr.reverse()
    error += if arr.front() == 50 {0} else {1}
    error += if arr.back() == 10 {0} else {1}

    // Ptr receiver — pop
    top := arr.pop()
    error += if top == 10 {0} else {1}
    error += if arr.size == 4zu {0} else {1}

    // Ptr receiver — clear
    arr.clear()
    error += if arr.size == 0zu {0} else {1}
    error += if arr.is_empty() == true {0} else {1}

    // Cleanup via implicit module
    arr.free()
    error
}

test_ufcs_user_module() {
    error := 0

    v := Geom.Vec3(x = 1, y = 2, z = 3)
    w := Geom.Vec3(x = 10, y = 20, z = 30)

    // Implicit module resolve — value receiver
    error += if v.length_sq() == 14 {0} else {1}
    b := v.scale(2)
    error += if b.x == 2 && b.y == 4 && b.z == 6 {0} else {1}

    c := v.add(w)
    error += if c.x == 11 && c.y == 22 && c.z == 33 {0} else {1}

    // Implicit module + implicit & — Ptr receiver
    v.set_x(10)
    error += if v.x == 10 {0} else {1}

    // Nullary Ptr receiver
    v.reset()
    error += if v.x == 0 && v.y == 0 && v.z == 0 {0} else {1}

    // Chaining: value→value→scalar
    r := Geom.Vec3(x = 1, y = 0, z = 0).scale(3).add(Geom.Vec3(x = 0, y = 4, z = 0)).length_sq()
    error += if r == 25 {0} else {1}

    // Explicit .Geom. still works
    d := v.Geom.scale(5)
    error += if d.x == 0 && d.y == 0 && d.z == 0 {0} else {1}

    error
}

// ── Main ──────────────────────────────────────────────────────────────────

main() {
    error := 0
    error = error + test_ufcs()
    error = error + test_ufcs_chained_module_resolve()
    error = error + test_ufcs_dot_call_unannotated_param()
    error = error + test_ufcs_functional()
    error = error + test_ufcs_generic_implicit_module()
    error = error + test_ufcs_hashmap()
    error = error + test_ufcs_implicit_addr()
    error = error + test_ufcs_implicit_addr_cross_module()
    error = error + test_ufcs_implicit_addr_cross_module_nullary()
    error = error + test_ufcs_implicit_submodule_generic()
    error = error + test_ufcs_module()
    error = error + test_ufcs_non_struct_receiver()
    error = error + test_ufcs_ptr_const()
    error = error + test_ufcs_string()
    error = error + test_ufcs_submodule()
    error = error + test_ufcs_type_module_resolve()
    error = error + test_ufcs_user_module()
    error
}
