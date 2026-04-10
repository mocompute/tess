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

    error
}
