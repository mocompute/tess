#module main

// Adversarial stress test: generic function pointers in structs

// --- Setup: generic structs ---

Box[T]: { value: T }

Processor[T]: { run: (T) -> T }

// --- Test 1: fn ptr field returns a generic struct (nested generics) ---

Maker[T]: { make: (T) -> Box[T] }

wrap_int(x: Int) -> Box[Int] { Box(value = x) }
wrap_bool(x: Bool) -> Box[Bool] { Box(value = x) }

// --- Test 2: Generic struct with fn ptr inside tagged union ---

MaybeProcessor[T]: | Active { proc: Processor[T] }
                   | Inactive

double_int(x: Int) -> Int { x * 2 }
triple_int(x: Int) -> Int { x * 3 }
negate_int(x: Int) -> Int { 0 - x }
not_bool(x: Bool) -> Bool { !x }
id_bool(x: Bool) -> Bool { x }

try_process[T](mp: MaybeProcessor[T], val: T, fallback: T) -> T {
    when mp {
        a: Active { a.proc.run(val) }
        i: Inactive { fallback }
    }
}

// --- Test 3: Chain - fn ptr returns struct whose fn ptr is then called ---

Chain[T]: { first: (T) -> Processor[T] }

make_doubler_proc(x: Int) -> Processor[Int] {
    // Use a regular function instead of closure to avoid compiler limitation
    Processor[Int](run = double_int/1)
}

make_tripler_proc(x: Int) -> Processor[Int] {
    Processor[Int](run = triple_int/1)
}

chain_apply[T](c: Chain[T], seed: T) -> T {
    intermediate := c.first(seed)
    intermediate.run(seed)
}

// --- Test 4: Generic struct specialized at a function pointer type ---

Holder[T]: { item: T }

// --- Test 5: Two-level nesting: struct with fn ptr that returns struct with fn ptr ---

Outer[T]: { get_inner: (T) -> Processor[T] }

make_processor_from_int(x: Int) -> Processor[Int] {
    Processor[Int](run = triple_int/1)
}

outer_chain[T](o: Outer[T], val: T) -> T {
    proc := o.get_inner(val)
    proc.run(val)
}

main() {
    error := 0

    // Test 1a: Maker[Int] - fn ptr returns Box[Int]
    m1 := Maker(make = wrap_int/1)
    b1 := m1.make(42)
    error += if b1.value == 42 { 0 } else { 1 }

    // Test 1b: Maker[Bool] - fn ptr returns Box[Bool]
    m2 := Maker(make = wrap_bool/1)
    b2 := m2.make(true)
    error += if b2.value == true { 0 } else { 1 }

    // Test 1c: Multiple Maker[Int] instances with same fn ptr
    m3 := Maker(make = wrap_int/1)
    b3 := m3.make(99)
    error += if b3.value == 99 { 0 } else { 1 }

    // Test 2a: Tagged union Active with Processor[Int] inside
    p_int := Processor(run = double_int/1)
    active_int := Active(proc = p_int)
    r1 := try_process(active_int, 20, 0)
    error += if r1 == 40 { 0 } else { 1 }

    // Test 2b: Tagged union Inactive
    inactive: MaybeProcessor[Int] := Inactive()
    r2 := try_process(inactive, 20, -1)
    error += if r2 == -1 { 0 } else { 1 }

    // Test 2c: Tagged union Active with Processor[Bool]
    p_bool := Processor(run = not_bool/1)
    active_bool := Active(proc = p_bool)
    r3 := try_process(active_bool, false, false)
    error += if r3 == true { 0 } else { 1 }

    // Test 2d: Tagged union with different processor in same type
    p_int2 := Processor(run = negate_int/1)
    active_neg := Active(proc = p_int2)
    r4 := try_process(active_neg, 7, 0)
    error += if r4 == -7 { 0 } else { 1 }

    // Test 3: Chain - fn ptr returns struct whose fn ptr is then called
    c1 := Chain(first = make_doubler_proc/1)
    r5 := chain_apply(c1, 5)
    // make_doubler_proc(5) returns Processor with run=double_int
    // then run(5) = 10
    error += if r5 == 10 { 0 } else { 1 }

    c2 := Chain(first = make_tripler_proc/1)
    r6 := chain_apply(c2, 4)
    // make_tripler_proc(4) returns Processor with run=triple_int
    // then run(4) = 12
    error += if r6 == 12 { 0 } else { 1 }

    // Test 4: Holder specialized at function pointer type
    h1 := Holder(item = double_int/1)
    r7 := h1.item(8)
    error += if r7 == 16 { 0 } else { 1 }

    h2 := Holder(item = triple_int/1)
    r8 := h2.item(8)
    error += if r8 == 24 { 0 } else { 1 }

    // Extract fn ptrs from holders and use them
    f1 := h1.item
    f2 := h2.item
    r9 := f1(3) + f2(3)
    // 6 + 9 = 15
    error += if r9 == 15 { 0 } else { 1 }

    // Test 5: Outer[Int] - two-level nesting
    o1 := Outer(get_inner = make_processor_from_int/1)
    r10 := outer_chain(o1, 10)
    // make_processor_from_int(10) returns Processor with run=triple_int
    // then run(10) = 30
    error += if r10 == 30 { 0 } else { 1 }

    error
}
