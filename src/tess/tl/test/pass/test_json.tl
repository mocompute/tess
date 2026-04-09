#module main

#import <Alloc.tl>
#import <Json.tl>
#import <Print.tl>
#import <String.tl>
#import <ToInt.tl>

// Use a module alias, not a type alias
#alias Arena Alloc.BumpAllocator
#alias J Json.Value

test_jtrue() {
    val := J.JTrue

    if _: JTrue := val {0} else {1}
}

test_jfalse() {
    val := J.JFalse

    if _: JFalse := val {0} else {1}
}

test_jnull() {
    val := J.JNull

    if _: JNull := val {0} else {1}
}

test_jint() {
    val := J.JInt64(42)

    _: CInt := if x: JInt64 := val {(x.value != 42).to_int()} else {1}
}

test_juint() {
    val := J.JUInt64(42)

    _: CInt := if x: JUInt64 := val {(x.value != 42).to_int()} else {1}
}

test_jdouble() {
    val     := J.JDouble(3.14)
    _: CInt := if x: JDouble := val {(x.value != 3.14).to_int()} else {1}
}


main() {

    error := 0

    arena := Arena.create(Alloc.context.managed, 1024)

    // unfortunate cannot use UFCS due to type erasure: arena is a Ptr[Allocator]
    defer Arena.destroy(arena)

    json := Json.Context.create(arena)
    defer json.destroy()

    error += test_jtrue()
    error += test_jfalse()
    error += test_jnull()
    error += test_jint()
    error += test_juint()
    error += test_jdouble()

    error
}
