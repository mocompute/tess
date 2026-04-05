#module Lookup

#import <HashMap.tl>

Lookup[K, V]: { map: Ptr[HashMap[K, V]] }

count[K, V](self: Ptr[Lookup[K, V]]) -> CSize {
    self->map.size()
}

create[K, V]() -> Lookup[K, V] {
    Lookup[K, V](map = HashMap.create[K, V]())
}

destroy[K, V](self: Ptr[Lookup[K, V]]) -> Void {
    self->map.destroy()
}

#module main

#import <Alloc.tl>
#import <String.tl>

// test_generic_apply
id   (x)    { x    }
apply(f, x) { f(x) }

test_apply() {
    apply(id/1, 0)
}

// test_generic_apply_through_pointer
id2   (x)    { x    }
apply2(f, x) { f(x) }

test_apply_through_pointer() {
    f1 := id2/1
    f1 := id2/1                         // shadow, the prior value is unspecialised and must not be emitted
    apply2(f1, 0)
}

// test_generic_closure_multi_type
// Partially-annotated generic lambda closure called at different argument
// types. The return type is fixed (Int) but the param type varies per
// call site.
test_closure_multi_type() {
    error := 0

    special  := 42
    constant := [[alloc, capture(special)]] (x) -> Int { special }

    if constant(1.0) != 42 { error += 1 }
    if constant("hello") != 42 { error += 1 }
    if constant(0) != 42 { error += 1 }

    error
}

// test_generic_fn_ref_explicit_type_args
// Taking a reference to a generic function with explicit type arguments
// should work: _default_hash[Int]/1
_default_hash[K](key: Ptr[K]) -> UInt { 42u }

test_fn_ref_explicit_type_args() {
    fn          := _default_hash[Int]/1
    p: Ptr[Int] := null
    r: CInt     := fn(p)
    r - 42
}

// test_generic_lambda
test_lambda() {
    f   := (a, b) { a + b }
    x1  := f(1, 2)
    x2  := f(1.0, 0.5)
    res := f(-1, 1)
    res
}

// test_generic_nested_sizeof
// Bug: sizeof[T] unifies T across separate call sites when used with
// nested generic structs.
Inner[K, V]: { key: K, value: V }

Outer[K, V]: {
    items: Ptr[Inner[K, V]],
    size:  CSize,
}

_alloc_inner[K, V](alloc: Ptr[Alloc.Allocator], n: CSize) -> Ptr[Inner[K, V]] {
    p: Ptr[Inner[K, V]] := alloc->calloc(alloc, n, sizeof[Inner[K, V]]())
    p
}

make_outer[K, V]() -> Ptr[Outer[K, V]] {
    alloc               := Alloc.context.default
    m: Ptr[Outer[K, V]] := alloc->malloc(alloc, sizeof[Outer[K, V]]())
    m->items = _alloc_inner[K, V](alloc, 16zu)
    m->size  = 0zu
    m
}

test_nested_sizeof() {
    m := make_outer[Int, Int]()
    if m->size != 0zu { return 1 }
    0
}

// test_generic_null_field
// Bug: generic struct constructor with null for Ptr[V] field
// leaves type variable V unresolved.
Simple[V]: {
    data: Ptr[V],
    size: UInt,
}

make_simple[V]() -> Simple[V] {
    Simple[V](data = null, size = 0u)
}

test_null_field() {
    s := make_simple[Int]()
    if s.size != 0u { return 1 }
    0
}

// test_generic_struct_fn_ptr_field
// A generic struct with a function pointer field whose parameter type
// uses a generic type wrapped in Ptr[K] should parse successfully.
FooFnPtr[K]: {
    f: (Ptr[K]) -> UInt,
}

test_struct_fn_ptr_field() {
    0
}

// test_generic_struct_null_fn_ptr_field
// Constructing a generic struct with null for a function pointer field
// that references a generic type parameter should resolve the type.
FooNullFnPtr[K]: {
    f: (Ptr[K]) -> UInt,
}

test_struct_null_fn_ptr_field() {
    opts := FooNullFnPtr[Int](f = null)
    if opts.f == null { return 0 }
    1
}

// test_generic_struct_wrapping_hashmap
// Pre-existing bug: wrapping Ptr[HashMap[K, V]] inside another generic struct
// causes expected_pointer when the outer struct is used with Ptr[T] receiver.
test_struct_wrapping_hashmap() {
    lk    := Lookup.create[Int, Int]()
    error := if lk.count() == 0 {0} else {1}
    lk.destroy()
    error
}

main() {
    error := 0
    error += test_apply()
    error += test_apply_through_pointer()
    error += test_closure_multi_type()
    error += test_fn_ref_explicit_type_args()
    error += test_lambda()
    error += test_nested_sizeof()
    error += test_null_field()
    error += test_struct_fn_ptr_field()
    error += test_struct_null_fn_ptr_field()
    error += test_struct_wrapping_hashmap()
    error
}
