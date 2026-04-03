#module main
#import <cstdlib.tl>

// === Types from test_struct_concrete ===
PointConcrete: { x: Int, y: Int }

// === Types from test_struct_construction ===
PointConstruction: { x: Int, y: Int }
Color: { r: Int, g: Int, b: Int }
Pair[a, b]: { first: a, second: b }

// === Types from test_struct_empty ===
EmptyStruct: { }

// === Types from test_struct_field_ptr_cast ===
FooPtrCast[T]: {data: Ptr[T]}

// === Types from test_struct_field_ptr_cast_inline ===
ArrInline[a]: {
    data:     Ptr[a],
    size:     CSize,
    capacity: CSize,
}

map_inline[a, b](f: (a) -> b, arr: ArrInline[a]) -> ArrInline[b]

map_inline[a, b](f, arr) {
    out := ArrInline(data: Ptr[b] = c_malloc(sizeof[b]() * arr.size), size = arr.size, capacity = arr.size)

    i := 0zu
    while i < arr.size {
        out.data.[i] = f(arr.data.[i])
        i            = i + 1zu
    }
    out
}

add1(x) { x + 1 }

// === Types from test_struct_field_ptr_cast_multi ===
ArrMulti[a]: {data: Ptr[a], size: UInt}

// === Types from test_struct_generic ===
PointGeneric[a]: { x: a, y: a }

// === Types from test_struct_generic_function_signature ===
double (x: Int) -> Int { x * 2 }
triple (x: Int) -> Int { x * 3 }
add_one(x: Int) -> Int { x + 1 }

Wrapper[T]: { callback: T, id: Int }

// === Types from test_struct_nested ===
FooNested[T1, T2]: {
    Bar: {
        barf: T1
    }

    Baz: {
        bazf: T2
    }

    a_bar: Bar,
    a_baz: Baz,
    a_t2:  T2,
}

// === Types from test_struct_nested_access ===
PointNested[a]: { x: a, y: a                               }
Rect[a]:        { topleft: PointNested[a], botright: PointNested[a] }

// === Test functions ===

test_concrete() {
    p            := PointConcrete(1, 0)
    result: CInt := p.y
    result
}

test_construction() {
    error := 0

    // Test named construction
    p2 := PointConstruction(x = 30, y = 40)
    error += if p2.x == 30 {0} else {1}
    error += if p2.y == 40 {0} else {1}

    // Test named construction with different order
    p3 := PointConstruction(y = 60, x = 50)
    error += if p3.x == 50 {0} else {1}
    error += if p3.y == 60 {0} else {1}

    // Test three-field struct with named
    c2 := Color(r = 0, g = 255, b = 128)
    error += if c2.r == 0 {0} else {1}
    error += if c2.g == 255 {0} else {1}
    error += if c2.b == 128 {0} else {1}

    // Test generic struct with named
    pair2 := Pair(first = 3, second = 4)
    error += if pair2.first == 3 {0} else {1}
    error += if pair2.second == 4 {0} else {1}

    error
}

test_empty() {
    ts := EmptyStruct()
    0
}

test_field_ptr_cast() {
    foo := FooPtrCast(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu))
    foo.data.[0] = 1
    c_free(foo.data)
    0
}

test_field_ptr_cast_inline() {
    vals := ArrInline(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 0zu, capacity = 4zu)
    vals.data.[0] = 0
    vals.data.[1] = 1
    vals.data.[2] = 2
    vals.data.[3] = 3
    vals.size     = 4zu

    res := map_inline(add1/1, vals)

    sum := 0
    i   := 0zu
    while i < res.size, i = i + 1zu {
        sum = sum + res.data.[i]
    }
    result: CInt := sum - 10
    result
}

test_field_ptr_cast_multi() {
    ints := ArrMulti(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 4u)
    ints.data.[0] = 1

    floats := ArrMulti(data: Ptr[Float] = c_malloc(sizeof[Float]() * 4zu), size = 4u)
    floats.data.[0] = 3.14

    c_free(ints.data)
    c_free(floats.data)
    0
}

test_generic() {
    p := PointGeneric(x = 1, y = 0)
    p.y
}

test_generic_function_signature() {
    error := 0

    // Test 1: Wrapper infers function pointer type from double/1
    w1      := Wrapper(callback = double/1, id = 1)
    result1 := w1.callback(5)
    error += if result1 == 10 {0} else {1}

    // Test 2: Wrapper infers function pointer type from triple/1
    w2      := Wrapper(callback = triple/1, id = 2)
    result2 := w2.callback(5)
    error += if result2 == 15 {0} else {1}

    // Test 3: Direct construction with add_one
    w3      := Wrapper(callback = add_one/1, id = 3)
    result3 := w3.callback(10)
    error += if result3 == 11 {0} else {1}

    // Test 4: Check id field is preserved
    error += if w1.id == 1 {0} else {1}
    error += if w2.id == 2 {0} else {1}
    error += if w3.id == 3 {0} else {1}

    // Test 5: Reassign callback and invoke again
    cb      := w1.callback
    result5 := cb(7)
    error += if result5 == 14 {0} else {1}

    error
}

test_nested() {
    error := 0
    bar   := FooNested.Bar(barf = 42)
    baz   := FooNested.Baz(bazf = 100)
    foo   := FooNested(a_bar = bar, a_baz = baz, a_t2 = 7)
    error += if foo.a_bar.barf == 42 {0} else {1}
    error += if foo.a_baz.bazf == 100 {0} else {1}
    error += if foo.a_t2 == 7 {0} else {1}
    error
}

test_nested_access() {
    r := Rect ( topleft = PointNested ( x = 0, y = 0 ), botright = PointNested ( x = 10, y = 10 ) )
    r.topleft.x
}

main() {
    error := 0
    error += test_concrete()
    error += test_construction()
    error += test_empty()
    error += test_field_ptr_cast()
    error += test_field_ptr_cast_inline()
    error += test_field_ptr_cast_multi()
    error += test_generic()
    error += test_generic_function_signature()
    error += test_nested()
    error += test_nested_access()
    error
}
