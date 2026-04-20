#module main

#import <Alloc.tl>
#import <Json.tl>
#import <Print.tl>
#import <String.tl>
#import <ToInt.tl>

// Use a module alias, not a type alias
#alias Arena Alloc.BumpAllocator
#alias J Json.Value

test_jtrue(json) {
    v1 := json.create_true()
    v2 := json.create_true()
    if v1 == v2 {0} else {1}
}

test_jfalse(json) {
    v1 := json.create_false()
    v2 := json.create_false()
    if v1 == v2 {0} else {1}
}

test_jnull(json) {
    v1 := json.create_null()
    v2 := json.create_null()
    if v1 == v2 {0} else {1}
}

test_jint(json) {
    val       := json.create_int(42)
    x: JInt64 := val else { return 1 }
    if x.value == 42 {0} else {1}

}

test_juint(json) {
    val        := json.create_uint(42)
    x: JUInt64 := val else { return 1 }
    if x.value == 42 {0} else {1}
}

test_jdouble(json) {
    val        := json.create_double(3.14)
    x: JDouble := val else { return 1 }
    if x.value == 3.14 {0} else {1}
}

test_string(json) {
    val        := json.create_string("hello")
    s: JString := val else { return 1 }
    if s.value == "hello" {0} else {1}
}

test_array(json) {
    error := 0

    arr := json.create_array()
    json.push(arr, json.create_string("abc"))
    json.push(arr, json.create_string("def"))

    data: JArray  := arr else { return 1 }
    s0:   JString := data.value.get(0) else { return 1 }
    s1:   JString := data.value.get(1) else { return 1 }
    error += if s0.value == "abc" {0} else {1}
    error += if s1.value == "def" {0} else {1}

    error
}

test_object(json) {
    error := 0

    obj := json.create_object()
    obj.set("abc", json.create_int(123))
    obj.set("def", json.create_int(456))

    error += if obj.contains("abc") {0} else {1}
    error += if obj.contains("def") {0} else {1}

    s: Some := obj.get("abc") else { return error + 1 }

    // Deref pointers here to do structural equality rather than pointer identity
    error += if s.value.* == json.create_int(123).* {0} else {1}

    error
}

test_eq_simple(json) {
    error := 0

    // Same values should be equal (dereference for structural equality)
    error += if json.create_true().*  == json.create_true().*  {0} else {1}
    error += if json.create_false().* == json.create_false().* {0} else {1}
    error += if json.create_null().*  == json.create_null().*  {0} else {1}

    // Different values should not be equal
    error += if json.create_true().* != json.create_false().* {0} else {1}
    error += if json.create_true().* != json.create_null().*  {0} else {1}

    error
}

test_eq_int(json) {
    error := 0

    error += if json.create_int(42).* == json.create_int(42).* {0} else {1}
    error += if json.create_int(42).* != json.create_int(99).* {0} else {1}

    error
}

test_eq_string(json) {
    error := 0

    error += if json.create_string("hello").* == json.create_string("hello").* {0} else {1}
    error += if json.create_string("hello").* != json.create_string("world").* {0} else {1}

    error
}

test_ord_int(json) {
    error := 0

    // Less than
    error += if json.create_int(10) < json.create_int(20) {0} else {1}
    error += if json.create_int(20) < json.create_int(10) {1} else {0}

    // Less than or equal
    error += if json.create_int(10) <= json.create_int(20) {0} else {1}
    error += if json.create_int(10) <= json.create_int(10) {0} else {1}

    // Greater than
    error += if json.create_int(20) > json.create_int(10) {0} else {1}
    error += if json.create_int(10) > json.create_int(20) {1} else {0}

    // Greater than or equal
    error += if json.create_int(20) >= json.create_int(10) {0} else {1}
    error += if json.create_int(20) >= json.create_int(20) {0} else {1}

    error
}

test_ord_string(json) {
    error := 0

    error += if json.create_string("aaa") < json.create_string("bbb") {0} else {1}
    error += if json.create_string("bbb") > json.create_string("aaa") {0} else {1}
    error += if json.create_string("aaa") <= json.create_string("aaa") {0} else {1}
    error += if json.create_string("aaa") >= json.create_string("aaa") {0} else {1}

    error
}

test_eq_array(json) {
    error := 0

    arr1 := json.create_array()
    json.push(arr1, json.create_int(1))
    json.push(arr1, json.create_int(2))

    arr2 := json.create_array()
    json.push(arr2, json.create_int(1))
    json.push(arr2, json.create_int(2))

    arr3 := json.create_array()
    json.push(arr3, json.create_int(1))
    json.push(arr3, json.create_int(3))

    error += if arr1.* == arr2.* {0} else {1}
    error += if arr1.* != arr3.* {0} else {1}

    error
}

test_eq_object(json) {
    error := 0

    obj1 := json.create_object()
    obj1.set("a", json.create_int(1))
    obj1.set("b", json.create_int(2))

    obj2 := json.create_object()
    obj2.set("a", json.create_int(1))
    obj2.set("b", json.create_int(2))

    obj3 := json.create_object()
    obj3.set("a", json.create_int(1))
    obj3.set("b", json.create_int(99))

    error += if obj1.* == obj2.* {0} else {1}
    error += if obj1.* != obj3.* {0} else {1}

    error
}

test_cross_type_ord(json) {
    error := 0

    // Different variant tags should compare by tag order
    // Tag order: JNull < JTrue < JFalse < JInt64 < ... < JString < JArray < JObject
    error += if json.create_int(0) < json.create_string("x") {0} else {1}
    error += if json.create_string("x") > json.create_int(0) {0} else {1}

    error
}

main() {

    error := 0

    arena := Arena.create(Alloc.context.managed, 1024)

    // unfortunate cannot use UFCS due to type erasure: arena is a Ptr[Allocator]
    defer Arena.destroy(arena)

    json := Json.Context.create(arena)
    defer json.destroy()

    error += test_jtrue(json)
    error += test_jfalse(json)
    error += test_jnull(json)
    error += test_jint(json)
    error += test_juint(json)
    error += test_jdouble(json)
    error += test_string(json)
    error += test_array(json)
    error += test_object(json)

    // Eq and Ord trait tests
    error += test_eq_simple(json)
    error += test_eq_int(json)
    error += test_eq_string(json)
    error += test_ord_int(json)
    error += test_ord_string(json)
    error += test_eq_array(json)
    error += test_eq_object(json)
    error += test_cross_type_ord(json)

    error
}
