#module main
#import <cstdio.tl>
#import <cstdlib.tl>
#import <Array.tl>

// --- Helpers from test_const_value_advanced ---

// Const parameter: can't reassign inside, transparent to caller
add_one(x: Const[Int]) {
    x + 1
}

// Generic with const
identity(x: Const) {
    x
}

// --- Helpers from test_const_value_struct ---

Point: { x: Int, y: Int }

read_point(p) {
    p.x + p.y
}

// Read-only method taking Ptr[Const[Point]]
point_sum(p: Ptr[Const[Point]]) {
    p->x + p->y
}

// --- Test functions ---

test_basic() {
    // Basic const value
    x: Const[Int] := 42
    c_printf(c"x = %lld\n", x)

    // Const with inference
    y: Const := 100
    c_printf(c"y = %lld\n", y)

    // Passing const to non-const parameter
    c_printf(c"sum = %lld\n", x + y)

    // Shadowing a const binding
    z: Const := 5
    z        := 10
    c_printf(c"z = %lld\n", z)

    0
}

test_advanced() {
    // Const parameter called with plain Int
    c_printf(c"add_one = %lld\n", add_one(5))

    // Generic with Const
    c_printf(c"identity = %lld\n", identity(42))

    // Closure capturing const
    val: Const := 10
    f          := () { val }
    c_printf(c"closure = %lld\n", f())

    0
}

test_edge_cases() {
    // Tagged union destructuring: when-bindings are mutable
    x: Const[Option[Int]] := Some(42)
    result                := when x {
        s: Some {
            val := s.value
            val = val + 1
            val
        }
        else { 0 }
    }
    c_printf(c"result = %lld\n", result)

    0
}

test_for_loop() {
    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)
    arr.push(30)

    // Value iterator: loop var is Const[Int]
    sum := 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum = %lld\n", sum)

    // Pointer iterator: loop var is Const[Ptr[Int]], can mutate pointee
    for p.& in arr {
        p.* = p.* + 1
    }

    sum = 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum after increment = %lld\n", sum)

    arr.free()
    0
}

test_loop_reassign() {
    arr := Array.empty[Int]()
    arr.push(1)
    arr.push(2)

    for x in arr {
        x = 99
    }

    arr.free()
    0
}

test_struct() {
    // Const struct: can read fields
    p: Const[Point] := Point(x = 10, y = 20)
    c_printf(c"sum = %lld\n", read_point(p))

    // Const pointer binding: can mutate pointee, can't reassign pointer
    raw: Ptr[Int] := c_malloc(8zu)
    raw.* = 42
    cp: Const[Ptr[Int]] := raw
    cp.* = 99
    c_printf(c"val = %d\n", cp.*)

    // Combined: Const[Ptr[Const[Int]]] — can't reassign, can't mutate pointee, can read
    cpc: Const[Ptr[Const[Int]]] := raw
    c_printf(c"cpc = %d\n", cpc.*)

    c_free(raw)

    // UFCS read-only method on const binding
    arr: Const[Array[Int]] := Array.empty[Int]()
    c_printf(c"size = %zu\n", arr.size)

    0
}

test_when_exhaustive() {
    // Const[Result] with exhaustive when (no else).
    x: Const[Result[Int, Int]] := Ok(42)
    result                     := when x {
        o: Ok { o.value }
        e: Err { e.error }
    }
    c_printf(c"result = %lld\n", result)
    0
}

main() {
    errors := 0
    errors = errors + test_basic()
    errors = errors + test_advanced()
    errors = errors + test_edge_cases()
    errors = errors + test_for_loop()
    errors = errors + test_loop_reassign()
    errors = errors + test_struct()
    errors = errors + test_when_exhaustive()
    errors
}
