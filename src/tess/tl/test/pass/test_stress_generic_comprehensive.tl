// Combined stress test for generics.
// Merged from: test_stress_generic_types, test_stress_generic_fn_ptr_struct,
// test_stress_generic_fn_ptr_struct_adversarial, test_stress_generic_tu_alias,
// test_stress_generic_tu_alias_adversarial, test_stress_higher_order_generic_tu,
// test_stress_higher_order_generic_tu_adversarial,
// test_stress_module_scoped_generic_tu, test_stress_module_scoped_generic_tu_adversarial,
// test_stress_weak_int_generics, test_stress_weak_int_generics_adversarial.

#import <cstdlib.tl>

// ============================================================
// From: test_stress_generic_types (modules: main)
// ============================================================

#module GT

Inner[a]: | IHas { value: a }
          | IEmpty

Outer[a]: | OHas { inner: Inner[a] }
          | OEmpty

wrap[a](v: a) -> Inner[a] {
    IHas(v)
}

Either[a, b]: | Left  { v: a }
              | Right { v: b }

swap_either[a, b](e: Either[a, b]) -> Either[b, a] {
    when e {
        l: Left { Right(l.v) }
        r: Right { Left(r.v) }
    }
}

my_unwrap[a](opt: Inner[a], default: a) -> a {
    when opt {
        ih: IHas { ih.value }
        ie: IEmpty { default }
    }
}

// ============================================================
// From: test_stress_generic_fn_ptr_struct (modules: main)
// ============================================================

#module GFPS

Handler[T]: { process: (T) -> T, tag: Int }

DualHandler[T]: { transform: (T) -> T, predicate: (T) -> Bool }

Wrapper[T]: { value: T, modify: (T) -> T }

double_int (x: Int)  -> Int  { x * 2  }
negate_int (x: Int)  -> Int  { 0 - x  }
not_bool   (x: Bool) -> Bool { !x     }
is_positive(x: Int)  -> Bool { x > 0  }
is_true    (x: Bool) -> Bool { x      }
add_ten    (x: Int)  -> Int  { x + 10 }
flip       (x: Bool) -> Bool { !x     }
triple_int (x: Int)  -> Int  { x * 3  }

apply_wrapper[T](w: Wrapper[T]) -> T {
    w.modify(w.value)
}

invoke_handler[T](h: Handler[T], x: T) -> T {
    h.process(x)
}

// ============================================================
// From: test_stress_generic_fn_ptr_struct_adversarial (modules: main)
// ============================================================

#module GFPSA

Box[T]: { value: T }

Processor[T]: { run: (T) -> T }

Maker[T]: { make: (T) -> Box[T] }

MaybeProcessor[T]: | Active { proc: Processor[T] }
                   | Inactive

Chain[T]: { first: (T) -> Processor[T] }

Holder[T]: { item: T }

Outer[T]: { get_inner: (T) -> Processor[T] }

wrap_int (x: Int)  -> Box[Int]  { Box(value = x) }
wrap_bool(x: Bool) -> Box[Bool] { Box(value = x) }

double_int(x: Int)  -> Int  { x * 2 }
triple_int(x: Int)  -> Int  { x * 3 }
negate_int(x: Int)  -> Int  { 0 - x }
not_bool  (x: Bool) -> Bool { !x    }
id_bool   (x: Bool) -> Bool { x     }

try_process[T](mp: MaybeProcessor[T], val: T, fallback: T) -> T {
    when mp {
        a: Active { a.proc.run(val) }
        i: Inactive { fallback }
    }
}

make_doubler_proc(x: Int) -> Processor[Int] {
    Processor[Int](run = double_int/1)
}

make_tripler_proc(x: Int) -> Processor[Int] {
    Processor[Int](run = triple_int/1)
}

chain_apply[T](c: Chain[T], seed: T) -> T {
    intermediate := c.first(seed)
    intermediate.run(seed)
}

make_processor_from_int(x: Int) -> Processor[Int] {
    Processor[Int](run = triple_int/1)
}

outer_chain[T](o: Outer[T], val: T) -> T {
    proc := o.get_inner(val)
    proc.run(val)
}

// ============================================================
// From: test_stress_generic_tu_alias
// ============================================================

#module TUDefs

Res[T]: | ROk  { value: T }
        | RErr { code: Int }

#module TUAliases

IntRes = TUDefs.Res[Int]

#module TUNestedAlias

IR = TUAliases.IntRes

// ============================================================
// From: test_stress_generic_tu_alias_adversarial
// ============================================================

#module TUA_A

Res[T]: | ROk  { value: T }
        | RErr { code: Int }

#module TUA_B

IntRes1 = TUA_A.Res[Int]
IntRes2 = TUA_A.Res[Int]

#module TUA_C

IR = TUA_B.IntRes1

#module TUA_D

Either[L, R]: | Left  { value: L }
              | Right { value: R }

IntStrEither = Either[Int, Int]

#module TUAWrapper

T[X]: | Wrap { inner: X }
      | WrapEmpty

#module TUA_F

IntWrap = TUAWrapper[Int]

// ============================================================
// From: test_stress_higher_order_generic_tu
// ============================================================

#module HOGT

Opt[T]: | Val { value: T }
        | Nope

opt_map[T, U](o: Opt[T], f) -> Opt[U] {
    when o {
        s: Val { Val(f(s.value)) }
        n: Nope { Nope() }
    }
}

opt_flatmap[T, U](o: Opt[T], f) -> Opt[U] {
    when o {
        s: Val { f(s.value) }
        n: Nope { Nope() }
    }
}

opt_unwrap[T](o: Opt[T], def: T) -> T {
    when o {
        s: Val { s.value }
        n: Nope { def }
    }
}

double_int(x: Int) -> Int { x * 2  }
add_ten   (x: Int) -> Int { x + 10 }

safe_div(x: Int) -> Opt[Int] {
    if x == 0 { Nope() } else { Val(100 / x) }
}

int_to_positive(x: Int) -> Opt[Int] {
    if x > 0 { Val(x) } else { Nope() }
}

call_and_unwrap(f, arg: Int, def: Int) -> Int {
    result: Opt[Int] := f(arg)
    when result {
        s: Val { s.value }
        n: Nope { def }
    }
}

compose_and_call(f, g, x: Int) -> Opt[Int] {
    g(f(x))
}

// ============================================================
// From: test_stress_higher_order_generic_tu_adversarial
// ============================================================

#module HOGTA

MyList[T]: | Cons { head: T, tail: Ptr[MyList[T]] }
           | LNil

MyRes[T]: | MyOk  { value: T }
          | MyErr { code: Int }

Opt[T]: | Val { value: T }
        | Nope

make_nil[T]() -> Ptr[MyList[T]] {
    p: Ptr[MyList[T]] := c_malloc(sizeof[MyList[T]]())
    p.* = LNil()
    p
}

make_cons[T](head: T, tail: Ptr[MyList[T]]) -> Ptr[MyList[T]] {
    p: Ptr[MyList[T]] := c_malloc(sizeof[MyList[T]]())
    p.* = Cons(head, tail)
    p
}

fold[T, U](lst: Ptr[MyList[T]], acc: U, f) -> U {
    node := lst.*
    when node {
        c: Cons { fold(c.tail, f(acc, c.head), f) }
        n: LNil { acc }
    }
}

list_sum_to_res(lst: Ptr[MyList[Int]]) -> MyRes[Int] {
    total := fold(lst, 0, (acc: Int, x: Int) { acc + x })
    if total > 0 { MyOk(total) } else { MyErr(0 - total) }
}

apply_or_default[T](o: Opt[T], f, def: T) -> T {
    when o {
        s: Val { f(s.value) }
        n: Nope { def }
    }
}

double_int(x: Int)         -> Int  { x * 2 }
add_ints  (a: Int, b: Int) -> Int  { a + b }
not_bool  (b: Bool)        -> Bool { !b    }

opt_unwrap[T](o: Opt[T], def: T) -> T {
    when o {
        s: Val { s.value }
        n: Nope { def }
    }
}

res_unwrap[T](r: MyRes[T], def: T) -> T {
    when r {
        o: MyOk { o.value }
        e: MyErr { def }
    }
}

opt_to_res[T](o: Opt[T], err_code: Int) -> MyRes[T] {
    when o {
        s: Val { MyOk(s.value) }
        n: Nope { MyErr(err_code) }
    }
}

flatten_opt_res[T](o: Opt[MyRes[T]], def: T) -> T {
    when o {
        s: Val {
            when s.value {
                ok: MyOk { ok.value }
                er: MyErr { def }
            }
        }
        n: Nope { def }
    }
}

process_nested(o: Opt[MyRes[Int]], f, def: Int) -> Int {
    inner := flatten_opt_res(o, def)
    f(inner)
}

// ============================================================
// From: test_stress_module_scoped_generic_tu
// ============================================================

#module MSGT_A

Outcome[a]: | Good { v: a }
            | Bad { code: Int }
            | Vacant

unwrap_or[a](r: Outcome[a], default: a) -> a {
    when r {
        g: Good { g.v }
        b: Bad { default }
        n: Vacant { default }
    }
}

is_good[a](r: Outcome[a]) -> Bool {
    when r {
        g: Good { true }
        b: Bad { false }
        n: Vacant { false }
    }
}

#module MSGT_B

Pair[a]: | Both   { fst: a, snd: a }
         | Single { only: a }
         | Neither

first_or[a](p: Pair[a], default: a) -> a {
    when p {
        b: Both { b.fst }
        s: Single { s.only }
        n: Neither { default }
    }
}

check_outcome[a](r: MSGT_A.Outcome[a], fallback: a) -> a {
    MSGT_A.unwrap_or(r, fallback)
}

// ============================================================
// From: test_stress_module_scoped_generic_tu_adversarial
// ============================================================

#module MSGTA_M1

Status[a]: | Ready { v: a }
           | Waiting
           | Done { v: a }

extract[a](s: Status[a], default: a) -> a {
    when s {
        r: Ready { r.v }
        w: Waiting { default }
        d: Done { d.v }
    }
}

#module MSGTA_M2

Status[a]: | Ready { v: a }
           | Waiting
           | Done { v: a }

extract[a](s: Status[a], default: a) -> a {
    when s {
        r: Ready { r.v }
        w: Waiting { default }
        d: Done { d.v }
    }
}

#module MSGTA_Nest

Wrap[a]: | Inner { payload: MSGTA_M1.Status[a] }
         | Leaf { v: a }
         | Nil

get_inner[a](w: Wrap[a], default: a) -> a {
    when w {
        i: Inner { MSGTA_M1.extract(i.payload, default) }
        l: Leaf { l.v }
        n: Nil { default }
    }
}

// ============================================================
// From: test_stress_weak_int_generics
// ============================================================

#module WIG

identity[T](x: T) -> T { x     }
wrap[T]    (x: T) -> T { x     }
double[T]  (x: T) -> T { x + x }

Pair[T]:                            { first: T, second: T         }
make_pair[T](a: T, b: T) -> Pair[T] { Pair(first = a, second = b) }

Box[T]:                     { value: T       }
make_box[T](x: T) -> Box[T] { Box(value = x) }

MyOpt[T]: | MySome { value: T }
          | MyNone

inner_generic[T](x: T) -> T { x }

outer_generic[T](x: T) -> T {
    inner_generic(x)
}

outer_with_literal[T](x: T) -> Pair[T] {
    make_pair(x, x)
}

step1[T](x: T) -> T { identity(x) }
step2[T](x: T) -> T { step1(x)    }
step3[T](x: T) -> T { step2(x)    }
step4[T](x: T) -> T { step3(x)    }

// ============================================================
// From: test_stress_weak_int_generics_adversarial
// ============================================================

#module WIGA

identity[T](x: T) -> T { x }

Wrapper[T]:                         { value: T           }
make_wrapper[T](x: T) -> Wrapper[T] { Wrapper(value = x) }

MyOpt[T]: | MySome { value: T }
          | MyNone

coerce[T](x: T) -> T { x }

wrap_in_opt[T](x: T) -> MyOpt[Wrapper[T]] {
    MySome(make_wrapper(x))
}

apply_thunk[T](f: () -> T) -> T { f() }

transform[T](x: T, f) -> T { f(x) }

level1[T](x: T) -> T { identity(x) }
level2[T](x: T) -> T { level1(x)   }
level3[T](x: T) -> T { level2(x)   }

level1_box[T](x: T) -> Wrapper[T] { make_wrapper(x) }
level2_box[T](x: T) -> Wrapper[T] { level1_box(x)   }
level3_box[T](x: T) -> Wrapper[T] { level2_box(x)   }

mixed[T](generic_val: T, concrete_val: Int) -> T {
    if concrete_val > 0 { generic_val } else { generic_val }
}

add_and_wrap[T](x: T, offset: Int) -> Wrapper[T] {
    make_wrapper(x)
}

maybe_wrap[T](x: T, do_wrap: Int) -> MyOpt[T] {
    if do_wrap > 0 { MySome(x) } else { MyNone() }
}

chain_id_to_box[T] (x: T) -> Wrapper[T]        { make_wrapper(identity(x))  }
chain_box_to_opt[T](x: T) -> MyOpt[Wrapper[T]] { MySome(chain_id_to_box(x)) }

// ============================================================
// Main module
// ============================================================

#module main

// --- Helpers for TU alias tests ---

tu_extract[T](r: TUDefs.Res[T], default: T) -> T {
    when r {
        o: ROk { o.value }
        e: RErr { default }
    }
}

tu_extract_aliased(r: TUAliases.IntRes, default: Int) -> Int {
    when r {
        o: ROk { o.value }
        e: RErr { default }
    }
}

// --- Helpers for TU alias adversarial tests ---

tua_unwrap_res[T](r: TUA_A.Res[T], default: T) -> T {
    when r {
        o: ROk { o.value }
        e: RErr { default }
    }
}

tua_unwrap_either[L, R](e: TUA_D.Either[L, R], left_default: L, right_default: R) -> Int {
    when e {
        l: Left { 1 }
        r: Right { 2 }
    }
}

// ============================================================
// Test functions
// ============================================================

test_generic_types() {
    error := 0

    // test_nested_generic_unions
    ( {
     x: GT.Outer[Int] := GT.OHas(GT.IHas(42))
     result           := when x {
     oh: OHas {
     when oh.inner {
     ih: IHas { ih.value }
     ie: IEmpty { -1 }
     }
     }
     oe: OEmpty { -2 }
     }
     error += if result == 42 {0} else {1}
     void
     })

    // test_nested_generic_unions_float
    ( {
     x: GT.Outer[Float] := GT.OHas(GT.IHas(3.14))
     result             := when x {
     oh: OHas {
     when oh.inner {
     ih: IHas { ih.value }
     ie: IEmpty { 0.0 }
     }
     }
     oe: OEmpty { 0.0 }
     }
     error += if result > 3.0 {0} else {1}
     void
     })

    // test_generic_func_returning_union
    ( {
     x      := GT.wrap(99)
     result := when x {
     ih: IHas { ih.value }
     ie: IEmpty { 0 }
     }
     error += if result == 99 {0} else {1}
     void
     })

    // test_multi_type_params
    ( {
     x: GT.Either[Int, Float] := GT.Left(10)
     y                        := GT.swap_either(x)
     result                   := when y {
     l: Left { 0 }
     r: Right { r.v }
     }
     error += if result == 10 {0} else {1}
     void
     })

    // test_generic_unwrap_chain
    ( {
     a                := GT.wrap(100)
     b: GT.Inner[Int] := GT.IEmpty()
     r1               := GT.my_unwrap(a, 0)
     r2               := GT.my_unwrap(b, -1)
     error += if r1 == 100 {0} else {1}
     error += if r2 == -1 {0} else {1}
     void
     })

    // test_type_param_is_union
    ( {
     nested: GT.Inner[GT.Inner[Int]] := GT.IHas(GT.IHas(77))
     result                          := when nested {
     outer: IHas {
     when outer.value {
     inner: IHas { inner.value }
     ie:    IEmpty { 0 }
     }
     }
     oe: IEmpty { 0 }
     }
     error += if result == 77 {0} else {1}
     void
     })

    error
}

test_generic_fn_ptr_struct() {
    error := 0

    // Test 1a: Handler[Int] with double
    h1 := GFPS.Handler(process = GFPS.double_int/1, tag = 1)
    r1 := h1.process(5)
    error += if r1 == 10 { 0 } else { 1 }

    // Test 1b: Handler[Int] with negate
    h2 := GFPS.Handler(process = GFPS.negate_int/1, tag = 2)
    r2 := h2.process(7)
    error += if r2 == -7 { 0 } else { 1 }

    // Test 1c: Handler[Bool] specialization
    h3 := GFPS.Handler(process = GFPS.not_bool/1, tag = 3)
    r3 := h3.process(true)
    error += if r3 == false { 0 } else { 1 }

    // Test 2a: DualHandler[Int]
    dh1 := GFPS.DualHandler(transform = GFPS.add_ten/1, predicate = GFPS.is_positive/1)
    r4  := dh1.transform(5)
    error += if r4 == 15 { 0 } else { 1 }
    r5 := dh1.predicate(r4)
    error += if r5 == true { 0 } else { 1 }

    // Test 2b: DualHandler[Bool]
    dh2 := GFPS.DualHandler(transform = GFPS.flip/1, predicate = GFPS.is_true/1)
    r6  := dh2.transform(false)
    error += if r6 == true { 0 } else { 1 }
    r7 := dh2.predicate(r6)
    error += if r7 == true { 0 } else { 1 }

    // Test 3: Wrapper with value and fn ptr on same type
    w1 := GFPS.Wrapper(value = 100, modify = GFPS.double_int/1)
    r8 := GFPS.apply_wrapper(w1)
    error += if r8 == 200 { 0 } else { 1 }

    w2 := GFPS.Wrapper(value = true, modify = GFPS.not_bool/1)
    r9 := GFPS.apply_wrapper(w2)
    error += if r9 == false { 0 } else { 1 }

    // Test 4: Pass Handler to generic function
    r10 := GFPS.invoke_handler(h1, 20)
    error += if r10 == 40 { 0 } else { 1 }

    r11 := GFPS.invoke_handler(h3, false)
    error += if r11 == true { 0 } else { 1 }

    // Test 5: Multiple instances with different fn ptrs
    handlers_a := GFPS.Handler(process = GFPS.double_int/1, tag = 10)
    handlers_b := GFPS.Handler(process = GFPS.triple_int/1, tag = 20)
    handlers_c := GFPS.Handler(process = GFPS.negate_int/1, tag = 30)

    sum := handlers_a.process(3) + handlers_b.process(3) + handlers_c.process(3)
    error += if sum == 12 { 0 } else { 1 }

    sum2 := GFPS.invoke_handler(handlers_a, 4) + GFPS.invoke_handler(handlers_b, 4) + GFPS.invoke_handler(handlers_c, 4)
    error += if sum2 == 16 { 0 } else { 1 }

    error
}

test_generic_fn_ptr_struct_adversarial() {
    error := 0

    // Test 1a: Maker[Int]
    m1 := GFPSA.Maker(make = GFPSA.wrap_int/1)
    b1 := m1.make(42)
    error += if b1.value == 42 { 0 } else { 1 }

    // Test 1b: Maker[Bool]
    m2 := GFPSA.Maker(make = GFPSA.wrap_bool/1)
    b2 := m2.make(true)
    error += if b2.value == true { 0 } else { 1 }

    // Test 1c: Multiple Maker[Int] instances
    m3 := GFPSA.Maker(make = GFPSA.wrap_int/1)
    b3 := m3.make(99)
    error += if b3.value == 99 { 0 } else { 1 }

    // Test 2a: Tagged union Active with Processor[Int] inside
    p_int      := GFPSA.Processor(run = GFPSA.double_int/1)
    active_int := GFPSA.Active(proc = p_int)
    r1         := GFPSA.try_process(active_int, 20, 0)
    error += if r1 == 40 { 0 } else { 1 }

    // Test 2b: Tagged union Inactive
    inactive: GFPSA.MaybeProcessor[Int] := GFPSA.Inactive()
    r2                                  := GFPSA.try_process(inactive, 20, -1)
    error += if r2 == -1 { 0 } else { 1 }

    // Test 2c: Tagged union Active with Processor[Bool]
    p_bool      := GFPSA.Processor(run = GFPSA.not_bool/1)
    active_bool := GFPSA.Active(proc = p_bool)
    r3          := GFPSA.try_process(active_bool, false, false)
    error += if r3 == true { 0 } else { 1 }

    // Test 2d: Tagged union with different processor
    p_int2     := GFPSA.Processor(run = GFPSA.negate_int/1)
    active_neg := GFPSA.Active(proc = p_int2)
    r4         := GFPSA.try_process(active_neg, 7, 0)
    error += if r4 == -7 { 0 } else { 1 }

    // Test 3: Chain
    c1 := GFPSA.Chain(first = GFPSA.make_doubler_proc/1)
    r5 := GFPSA.chain_apply(c1, 5)
    error += if r5 == 10 { 0 } else { 1 }

    c2 := GFPSA.Chain(first = GFPSA.make_tripler_proc/1)
    r6 := GFPSA.chain_apply(c2, 4)
    error += if r6 == 12 { 0 } else { 1 }

    // Test 4: Holder specialized at function pointer type
    h1 := GFPSA.Holder(item = GFPSA.double_int/1)
    r7 := h1.item(8)
    error += if r7 == 16 { 0 } else { 1 }

    h2 := GFPSA.Holder(item = GFPSA.triple_int/1)
    r8 := h2.item(8)
    error += if r8 == 24 { 0 } else { 1 }

    f1 := h1.item
    f2 := h2.item
    r9 := f1(3) + f2(3)
    error += if r9 == 15 { 0 } else { 1 }

    // Test 5: Outer[Int] - two-level nesting
    o1  := GFPSA.Outer(get_inner = GFPSA.make_processor_from_int/1)
    r10 := GFPSA.outer_chain(o1, 10)
    error += if r10 == 30 { 0 } else { 1 }

    error
}

test_generic_tu_alias() {
    error := 0

    // Test 1: Use alias and full generic form in same program
    via_alias:   TUAliases.IntRes := TUDefs.ROk(10)
    via_generic: TUDefs.Res[Int]  := TUDefs.ROk(20)

    error += if tu_extract(via_alias, 0) == 10 { 0 } else { 1 }
    error += if tu_extract(via_generic, 0) == 20 { 0 } else { 1 }

    // Test 2: Alias used as function parameter type
    ok_val := TUDefs.ROk(42)
    error += if tu_extract_aliased(ok_val, 0) == 42 { 0 } else { 1 }

    err_val: TUAliases.IntRes := TUDefs.RErr(code = 1)
    error += if tu_extract_aliased(err_val, 99) == 99 { 0 } else { 1 }

    // Test 3: Nested alias (alias of alias)
    nested_ok: TUNestedAlias.IR := TUDefs.ROk(77)
    error += if tu_extract(nested_ok, 0) == 77 { 0 } else { 1 }

    // Test 4: Generic function called with aliased and non-aliased
    a1: TUAliases.IntRes := TUDefs.ROk(100)
    a2: TUDefs.Res[Int]  := TUDefs.ROk(200)
    error += if tu_extract(a1, 0) == 100 { 0 } else { 1 }
    error += if tu_extract(a2, 0) == 200 { 0 } else { 1 }

    // Test 5: Pattern matching (when) on aliased tagged union
    aliased_val: TUAliases.IntRes := TUDefs.ROk(55)
    matched                       := when aliased_val {
        o: ROk { o.value }
        e: RErr { e.code }
    }
    error += if matched == 55 { 0 } else { 1 }

    nested_val: TUNestedAlias.IR := TUDefs.RErr(code = 7)
    matched2                     := when nested_val {
        o: ROk { o.value }
        e: RErr { e.code }
    }
    error += if matched2 == 7 { 0 } else { 1 }

    error
}

test_generic_tu_alias_adversarial() {
    error := 0

    // Test 1: Multiple aliases to the same specialization
    v1: TUA_B.IntRes1  := TUA_A.ROk(10)
    v2: TUA_B.IntRes2  := TUA_A.ROk(20)
    v3: TUA_A.Res[Int] := TUA_A.ROk(30)

    error += if tua_unwrap_res(v1, 0) == 10 { 0 } else { 1 }
    error += if tua_unwrap_res(v2, 0) == 20 { 0 } else { 1 }
    error += if tua_unwrap_res(v3, 0) == 30 { 0 } else { 1 }

    // Test 2: Chained alias
    v4: TUA_C.IR := TUA_A.ROk(40)
    error += if tua_unwrap_res(v4, 0) == 40 { 0 } else { 1 }

    // Test 3: Two-param generic tagged union with alias
    e1: TUA_D.IntStrEither     := TUA_D.Left(v = 100)
    e2: TUA_D.Either[Int, Int] := TUA_D.Right(v = 200)

    r1 := when e1 {
        l: Left { l.value }
        r: Right { r.value }
    }
    error += if r1 == 100 { 0 } else { 1 }

    r2 := when e2 {
        l: Left { l.value }
        r: Right { r.value }
    }
    error += if r2 == 200 { 0 } else { 1 }

    error += if tua_unwrap_either(e1, 0, 0) == 1 { 0 } else { 1 }
    error += if tua_unwrap_either(e2, 0, 0) == 2 { 0 } else { 1 }

    // Test 4: Cross-module usage
    cross1: TUA_B.IntRes1 := TUA_A.RErr(code = 5)
    cross2: TUA_B.IntRes2 := TUA_A.RErr(code = 6)
    cross3: TUA_C.IR      := TUA_A.RErr(code = 7)

    m1 := when cross1 { o: ROk { o.value } e: RErr { e.code } }
    m2 := when cross2 { o: ROk { o.value } e: RErr { e.code } }
    m3 := when cross3 { o: ROk { o.value } e: RErr { e.code } }

    error += if m1 == 5 { 0 } else { 1 }
    error += if m2 == 6 { 0 } else { 1 }
    error += if m3 == 7 { 0 } else { 1 }

    // Test 5: Auto-collapse + alias interaction
    w1                  := TUAWrapper.Wrap(42)
    w2: TUA_F.IntWrap   := TUAWrapper.Wrap(99)
    w3: TUAWrapper[Int] := TUAWrapper.WrapEmpty

    r3 := when w1 { w: Wrap { w.inner } e: WrapEmpty { 0 } }
    r4 := when w2 { w: Wrap { w.inner } e: WrapEmpty { 0 } }
    r5 := when w3 { w: Wrap { w.inner } e: WrapEmpty { -1 } }

    error += if r3 == 42 { 0 } else { 1 }
    error += if r4 == 99 { 0 } else { 1 }
    error += if r5 == -1 { 0 } else { 1 }

    error
}

test_higher_order_generic_tu() {
    error := 0

    // Test 1: basic map over Val
    v1 := HOGT.Val(5)
    r1 := HOGT.opt_map(v1, HOGT.double_int/1)
    error += if HOGT.opt_unwrap(r1, 0) == 10 {0} else {1}

    // Test 2: map over Nope
    n1: HOGT.Opt[Int] := HOGT.Nope()
    r2                := HOGT.opt_map(n1, HOGT.double_int/1)
    error += if HOGT.opt_unwrap(r2, 0) == 0 {0} else {1}

    // Test 3: flatmap with success
    v2 := HOGT.Val(5)
    r3 := HOGT.opt_flatmap(v2, HOGT.safe_div/1)
    error += if HOGT.opt_unwrap(r3, 0) == 20 {0} else {1}

    // Test 4: flatmap with failure
    v3 := HOGT.Val(0)
    r4 := HOGT.opt_flatmap(v3, HOGT.safe_div/1)
    error += if HOGT.opt_unwrap(r4, -1) == -1 {0} else {1}

    // Test 5: chain map then flatmap
    v4 := HOGT.Val(3)
    r5 := HOGT.opt_map(v4, HOGT.add_ten/1)
    r6 := HOGT.opt_flatmap(r5, HOGT.int_to_positive/1)
    error += if HOGT.opt_unwrap(r6, 0) == 13 {0} else {1}

    // Test 6: chain flatmap then map
    v5 := HOGT.Val(10)
    r7 := HOGT.opt_flatmap(v5, HOGT.safe_div/1)
    r8 := HOGT.opt_map(r7, HOGT.double_int/1)
    error += if HOGT.opt_unwrap(r8, 0) == 20 {0} else {1}

    // Test 7: call_and_unwrap
    r9 := HOGT.call_and_unwrap(HOGT.safe_div/1, 4, -1)
    error += if r9 == 25 {0} else {1}

    // Test 8: call_and_unwrap with Nope result
    r10 := HOGT.call_and_unwrap(HOGT.safe_div/1, 0, -1)
    error += if r10 == -1 {0} else {1}

    // Test 9: compose_and_call
    r11 := HOGT.compose_and_call(HOGT.double_int/1, HOGT.safe_div/1, 3)
    error += if HOGT.opt_unwrap(r11, 0) == 16 {0} else {1}

    // Test 10: multiple map calls chained
    v6  := HOGT.Val(1)
    r12 := HOGT.opt_map(v6, HOGT.double_int/1)
    r13 := HOGT.opt_map(r12, HOGT.double_int/1)
    r14 := HOGT.opt_map(r13, HOGT.add_ten/1)
    error += if HOGT.opt_unwrap(r14, 0) == 14 {0} else {1}

    // Test 11: flatmap chain (monadic pipeline)
    v7  := HOGT.Val(2)
    r15 := HOGT.opt_flatmap(v7, HOGT.safe_div/1)
    r16 := HOGT.opt_flatmap(r15, HOGT.int_to_positive/1)
    error += if HOGT.opt_unwrap(r16, 0) == 50 {0} else {1}

    // Test 12: map with lambda
    v8  := HOGT.Val(7)
    r17 := HOGT.opt_map(v8, (x: Int) { x * x })
    error += if HOGT.opt_unwrap(r17, 0) == 49 {0} else {1}

    // Test 13: generic function returning TU passed as function pointer
    r18 := HOGT.call_and_unwrap(HOGT.int_to_positive/1, 42, -1)
    error += if r18 == 42 {0} else {1}

    // Test 14: negative case
    r19 := HOGT.call_and_unwrap(HOGT.int_to_positive/1, -5, -1)
    error += if r19 == -1 {0} else {1}

    // Test 15: compose_and_call with add_ten and safe_div
    r20 := HOGT.compose_and_call(HOGT.add_ten/1, HOGT.safe_div/1, 0)
    error += if HOGT.opt_unwrap(r20, 0) == 10 {0} else {1}

    error
}

test_higher_order_generic_tu_adversarial() {
    error := 0

    // Test 1: fold over a list to sum
    lst1 := HOGTA.make_cons(1, HOGTA.make_cons(2, HOGTA.make_cons(3, HOGTA.make_nil[Int]())))
    sum1 := HOGTA.fold(lst1, 0, (acc: Int, x: Int) { acc + x })
    error += if sum1 == 6 {0} else {1}

    // Test 2: fold with multiplication
    lst2 := HOGTA.make_cons(2, HOGTA.make_cons(3, HOGTA.make_cons(4, HOGTA.make_nil[Int]())))
    prod := HOGTA.fold(lst2, 1, (acc: Int, x: Int) { acc * x })
    error += if prod == 24 {0} else {1}

    // Test 3: fold with function pointer
    lst3 := HOGTA.make_cons(10, HOGTA.make_cons(20, HOGTA.make_cons(30, HOGTA.make_nil[Int]())))
    sum3 := HOGTA.fold(lst3, 0, HOGTA.add_ints/2)
    error += if sum3 == 60 {0} else {1}

    // Test 4: map List -> MyRes
    lst4 := HOGTA.make_cons(5, HOGTA.make_cons(10, HOGTA.make_cons(15, HOGTA.make_nil[Int]())))
    r4   := HOGTA.list_sum_to_res(lst4)
    error += if HOGTA.res_unwrap(r4, 0) == 30 {0} else {1}

    // Test 5: cross-TU negative case
    lst5     := HOGTA.make_cons(-5, HOGTA.make_cons(-10, HOGTA.make_nil[Int]()))
    r5       := HOGTA.list_sum_to_res(lst5)
    err_code := when r5 {
        o: MyOk { 0 }
        e: MyErr { e.code }
    }
    error += if err_code == 15 {0} else {1}

    // Test 6: apply_or_default with Int
    o6 := HOGTA.Val(21)
    r6 := HOGTA.apply_or_default(o6, HOGTA.double_int/1, 0)
    error += if r6 == 42 {0} else {1}

    // Test 7: apply_or_default with Int, Nope case
    n7: HOGTA.Opt[Int] := HOGTA.Nope()
    r7                 := HOGTA.apply_or_default(n7, HOGTA.double_int/1, -1)
    error += if r7 == -1 {0} else {1}

    // Test 8: apply_or_default with Bool
    o8 := HOGTA.Val(true)
    r8 := HOGTA.apply_or_default(o8, HOGTA.not_bool/1, false)
    error += if r8 == false {0} else {1}

    // Test 9: opt_to_res
    o9 := HOGTA.Val(99)
    r9 := HOGTA.opt_to_res(o9, -1)
    error += if HOGTA.res_unwrap(r9, 0) == 99 {0} else {1}

    // Test 10: opt_to_res Nope case
    n10: HOGTA.Opt[Int] := HOGTA.Nope()
    r10                 := HOGTA.opt_to_res(n10, 42)
    err10               := when r10 {
        o: MyOk { 0 }
        e: MyErr { e.code }
    }
    error += if err10 == 42 {0} else {1}

    // Test 11: nested TU: Opt[MyRes[Int]]
    nested1 := HOGTA.Val(HOGTA.MyOk(77))
    r11     := HOGTA.flatten_opt_res(nested1, 0)
    error += if r11 == 77 {0} else {1}

    // Test 12: nested TU with MyErr inside
    r12: HOGTA.MyRes[Int] := HOGTA.MyErr(5)
    nested2               := HOGTA.Val(r12)
    r12b                  := HOGTA.flatten_opt_res(nested2, -1)
    error += if r12b == -1 {0} else {1}

    // Test 13: Nope at outer level
    nested3: HOGTA.Opt[HOGTA.MyRes[Int]] := HOGTA.Nope()
    r13                                  := HOGTA.flatten_opt_res(nested3, -2)
    error += if r13 == -2 {0} else {1}

    // Test 14: process_nested with function pointer
    nested4 := HOGTA.Val(HOGTA.MyOk(10))
    r14     := HOGTA.process_nested(nested4, HOGTA.double_int/1, 0)
    error += if r14 == 20 {0} else {1}

    // Test 15: closure capturing a tagged union
    captured_opt := HOGTA.Val(100)
    closure      := () { HOGTA.opt_unwrap(captured_opt, 0) }
    r15          := closure()
    error += if r15 == 100 {0} else {1}

    // Test 16: apply_or_default with lambda
    o16 := HOGTA.Val(5)
    r16 := HOGTA.apply_or_default(o16, (x: Int) { x * x * x }, 0)
    error += if r16 == 125 {0} else {1}

    error
}

test_module_scoped_generic_tu() {
    error := 0

    // Module A: Outcome[Int] via constructor paths
    r1 := MSGT_A.Good(42)
    error += if MSGT_A.unwrap_or(r1, 0) == 42 { 0 } else { 1 }

    r2 := MSGT_A.Outcome.Good(99)
    error += if MSGT_A.unwrap_or(r2, 0) == 99 { 0 } else { 1 }

    r3 := MSGT_A.Good(v = 7)
    error += if MSGT_A.unwrap_or(r3, 0) == 7 { 0 } else { 1 }

    r4: MSGT_A.Outcome[Int] := MSGT_A.Bad(code = 404)
    error += if MSGT_A.is_good(r4) { 1 } else { 0 }

    // Nullary variant: Vacant
    r5: MSGT_A.Outcome[Int] := MSGT_A.Vacant
    error += if MSGT_A.unwrap_or(r5, -1) == -1 { 0 } else { 1 }

    r6: MSGT_A.Outcome[Int] := MSGT_A.Outcome.Vacant
    error += if MSGT_A.is_good(r6) { 1 } else { 0 }

    // Outcome[Bool] - different specialization
    rb1 := MSGT_A.Good(true)
    error += if MSGT_A.unwrap_or(rb1, false) { 0 } else { 1 }

    rb2 := MSGT_A.Outcome.Good(false)
    error += if MSGT_A.unwrap_or(rb2, true) { 1 } else { 0 }

    // Module B: Pair[Int]
    p1 := MSGT_B.Both(fst = 10, snd = 20)
    error += if MSGT_B.first_or(p1, 0) == 10 { 0 } else { 1 }

    p2 := MSGT_B.Single(only = 77)
    error += if MSGT_B.first_or(p2, 0) == 77 { 0 } else { 1 }

    p3: MSGT_B.Pair[Int] := MSGT_B.Neither
    error += if MSGT_B.first_or(p3, -1) == -1 { 0 } else { 1 }

    // Pair[Bool]
    pb := MSGT_B.Both(fst = true, snd = false)
    error += if MSGT_B.first_or(pb, false) { 0 } else { 1 }

    // Pattern matching on module-scoped generic tagged unions
    val     := MSGT_A.Good(100)
    matched := when val {
        g: Good { g.v }
        b: Bad { b.code }
        n: Vacant { -1 }
    }
    error += if matched == 100 { 0 } else { 1 }

    val2: MSGT_A.Outcome[Int] := MSGT_A.Bad(code = 500)
    matched2                  := when val2 {
        g: Good { g.v }
        b: Bad { b.code }
        n: Vacant { -1 }
    }
    error += if matched2 == 500 { 0 } else { 1 }

    // Cross-module generic function
    cr := MSGT_A.Good(55)
    error += if MSGT_B.check_outcome(cr, 0) == 55 { 0 } else { 1 }

    cr2: MSGT_A.Outcome[Int] := MSGT_A.Vacant
    error += if MSGT_B.check_outcome(cr2, -1) == -1 { 0 } else { 1 }

    // Multiple specializations
    oi := MSGT_A.Good(10)
    ob := MSGT_A.Good(true)
    error += if MSGT_A.unwrap_or(oi, 0) == 10 { 0 } else { 1 }
    error += if MSGT_A.unwrap_or(ob, false) { 0 } else { 1 }

    error
}

test_module_scoped_generic_tu_adversarial() {
    error := 0

    // Disambiguating identically-named variants via module scope
    s1 := MSGTA_M1.Ready(42)
    s2 := MSGTA_M2.Ready(99)

    error += if MSGTA_M1.extract(s1, 0) == 42 { 0 } else { 1 }
    error += if MSGTA_M2.extract(s2, 0) == 99 { 0 } else { 1 }

    d1 := MSGTA_M1.Done(10)
    d2 := MSGTA_M2.Done(20)
    error += if MSGTA_M1.extract(d1, 0) == 10 { 0 } else { 1 }
    error += if MSGTA_M2.extract(d2, 0) == 20 { 0 } else { 1 }

    w1: MSGTA_M1.Status[Int] := MSGTA_M1.Waiting
    w2: MSGTA_M2.Status[Int] := MSGTA_M2.Waiting
    error += if MSGTA_M1.extract(w1, -1) == -1 { 0 } else { 1 }
    error += if MSGTA_M2.extract(w2, -2) == -2 { 0 } else { 1 }

    // Type-path scoped construction
    ts1 := MSGTA_M1.Status.Ready(77)
    ts2 := MSGTA_M2.Status.Ready(88)
    error += if MSGTA_M1.extract(ts1, 0) == 77 { 0 } else { 1 }
    error += if MSGTA_M2.extract(ts2, 0) == 88 { 0 } else { 1 }

    // Named field construction
    nf1 := MSGTA_M1.Ready(v = 33)
    nf2 := MSGTA_M2.Ready(v = 44)
    error += if MSGTA_M1.extract(nf1, 0) == 33 { 0 } else { 1 }
    error += if MSGTA_M2.extract(nf2, 0) == 44 { 0 } else { 1 }

    // Nested: variant containing another module's generic tagged union
    inner_status := MSGTA_M1.Ready(100)
    wrapped      := MSGTA_Nest.Inner(payload = inner_status)
    error += if MSGTA_Nest.get_inner(wrapped, 0) == 100 { 0 } else { 1 }

    leaf := MSGTA_Nest.Leaf(55)
    error += if MSGTA_Nest.get_inner(leaf, 0) == 55 { 0 } else { 1 }

    nil_val: MSGTA_Nest.Wrap[Int] := MSGTA_Nest.Nil
    error += if MSGTA_Nest.get_inner(nil_val, -1) == -1 { 0 } else { 1 }

    // Nested with Bool specialization
    inner_bool   := MSGTA_M1.Ready(true)
    wrapped_bool := MSGTA_Nest.Inner(payload = inner_bool)
    error += if MSGTA_Nest.get_inner(wrapped_bool, false) { 0 } else { 1 }

    // Multiple specializations in a single function
    a1 := MSGTA_M1.Ready(1)
    a2 := MSGTA_M1.Ready(true)
    a3 := MSGTA_M2.Ready(2)
    a4 := MSGTA_M2.Ready(false)

    error += if MSGTA_M1.extract(a1, 0) == 1 { 0 } else { 1 }
    error += if MSGTA_M1.extract(a2, false) { 0 } else { 1 }
    error += if MSGTA_M2.extract(a3, 0) == 2 { 0 } else { 1 }
    error += if MSGTA_M2.extract(a4, true) { 1 } else { 0 }

    // Pattern matching on cross-module values
    pm1     := MSGTA_M1.Done(v = 200)
    result1 := when pm1 {
        r: Ready { r.v }
        w: Waiting { -1 }
        d: Done { d.v }
    }
    error += if result1 == 200 { 0 } else { 1 }

    pm2     := MSGTA_M2.Status.Done(300)
    result2 := when pm2 {
        r: Ready { r.v }
        w: Waiting { -1 }
        d: Done { d.v }
    }
    error += if result2 == 300 { 0 } else { 1 }

    // Nested: pattern match on Wrap, then inner pattern match on Status
    deep    := MSGTA_Nest.Inner(payload = MSGTA_M1.Done(999))
    result3 := when deep {
        i: Inner {
            when i.payload {
                r: Ready { r.v }
                w: Waiting { -1 }
                d: Done { d.v }
            }
        }
        l: Leaf { l.v }
        n: Nil { -1 }
    }
    error += if result3 == 999 { 0 } else { 1 }

    error
}

test_weak_int_generics() {
    error := 0

    // 1. Generic function called with weak int literals
    ( {
     r := WIG.identity(42)
     error += if r == 42 {0} else {1}
     r2 := WIG.identity(100u)
     error += if r2 == 100u {0} else {1}
     void
     })

    // 2. Nested generic calls
    ( {
     r := WIG.identity(WIG.wrap(42))
     error += if r == 42 {0} else {1}
     r2 := WIG.identity(WIG.wrap(WIG.identity(99)))
     error += if r2 == 99 {0} else {1}
     r3 := WIG.wrap(WIG.identity(55u))
     error += if r3 == 55u {0} else {1}
     void
     })

    // 3. Store weak int in generic struct
    ( {
     b := WIG.make_box(42)
     error += if b.value == 42 {0} else {1}
     b2 := WIG.make_box(77u)
     error += if b2.value == 77u {0} else {1}
     void
     })

    // 4. Multiple weak int literals that must unify
    ( {
     p := WIG.make_pair(1, 2)
     error += if p.first == 1 {0} else {1}
     error += if p.second == 2 {0} else {1}
     p2 := WIG.make_pair(10u, 20u)
     error += if p2.first == 10u {0} else {1}
     error += if p2.second == 20u {0} else {1}
     void
     })

    // 5. Weak unsigned literals through generics
    ( {
     r := WIG.double(5u)
     error += if r == 10u {0} else {1}
     r2 := WIG.identity(WIG.wrap(255u))
     error += if r2 == 255u {0} else {1}
     void
     })

    // 6. Generic tagged union constructed with weak int literals
    ( {
     opt := WIG.MySome(42)
     error += when opt {
     s: MySome { if s.value == 42 {0} else {1} }
     n: MyNone { 1 }
     }
     opt2 := WIG.MySome(99u)
     error += when opt2 {
     s: MySome { if s.value == 99u {0} else {1} }
     n: MyNone { 1 }
     }
     opt3 := Some(123)
     error += if (s: Some := opt3 else { 0 }, s.value) == 123 {0} else {1}
     void
     })

    // 7. Generic function called from another generic function with weak int
    ( {
     r := WIG.outer_generic(42)
     error += if r == 42 {0} else {1}
     r2 := WIG.outer_generic(10u)
     error += if r2 == 10u {0} else {1}
     p := WIG.outer_with_literal(7)
     error += if p.first == 7 {0} else {1}
     error += if p.second == 7 {0} else {1}
     void
     })

    // 8. Chain of 3+ generic calls
    ( {
     r := WIG.step4(42)
     error += if r == 42 {0} else {1}
     r2 := WIG.step4(77u)
     error += if r2 == 77u {0} else {1}
     r3 := WIG.step4(WIG.identity(33))
     error += if r3 == 33 {0} else {1}
     void
     })

    error
}

test_weak_int_generics_adversarial() {
    error := 0

    // 1. Signed vs unsigned specializations
    ( {
     a := WIGA.coerce(42)
     error += if a == 42 {0} else {1}
     b := WIGA.coerce(42u)
     error += if b == 42u {0} else {1}
     c := WIGA.coerce(100)
     d := WIGA.coerce(100u)
     error += if c == 100 {0} else {1}
     error += if d == 100u {0} else {1}
     void
     })

    // 2. Struct in tagged union
    ( {
     opt := WIGA.wrap_in_opt(42)
     error += when opt {
     s: MySome { if s.value.value == 42 {0} else {1} }
     n: MyNone { 1 }
     }
     opt2 := WIGA.wrap_in_opt(99u)
     error += when opt2 {
     s: MySome { if s.value.value == 99u {0} else {1} }
     n: MyNone { 1 }
     }
     void
     })

    // 3. Higher-order weak int
    ( {
     r := WIGA.apply_thunk(() { 42 })
     error += if r == 42 {0} else {1}
     r2 := WIGA.apply_thunk(() { 77u })
     error += if r2 == 77u {0} else {1}
     r3 := WIGA.transform(10, (x) { x + 1 })
     error += if r3 == 11 {0} else {1}
     r4 := WIGA.transform(10u, (x) { x + 1u })
     error += if r4 == 11u {0} else {1}
     void
     })

    // 4. Deep indirection
    ( {
     r := WIGA.level3(42)
     error += if r == 42 {0} else {1}
     r2 := WIGA.level3(55u)
     error += if r2 == 55u {0} else {1}
     w := WIGA.level3_box(42)
     error += if w.value == 42 {0} else {1}
     w2 := WIGA.level3_box(99u)
     error += if w2.value == 99u {0} else {1}
     void
     })

    // 5. Mixed weak and concrete
    ( {
     r := WIGA.mixed(42, 10)
     error += if r == 42 {0} else {1}
     r2 := WIGA.mixed(42u, 10)
     error += if r2 == 42u {0} else {1}
     w := WIGA.add_and_wrap(77, 5)
     error += if w.value == 77 {0} else {1}
     w2 := WIGA.add_and_wrap(77u, 5)
     error += if w2.value == 77u {0} else {1}
     void
     })

    // 6. Weak int nested option
    ( {
     o := Some(42)
     error += if (s: Some := o else { 0 }, s.value) == 42 {0} else {1}
     nested := Some(Some(42))
     error += when nested {
     outer: Some {
     when outer.value {
     inner: Some { if inner.value == 42 {0} else {1} }
     n:     None { 1 }
     }
     }
     n: None { 1 }
     }
     opt_u := Some(99u)
     error += if (s: Some := opt_u else { 0u }, s.value) == 99u {0} else {1}
     void
     })

    // 7. Generic returning tagged union
    ( {
     r := WIGA.maybe_wrap(42, 1)
     error += when r {
     s: MySome { if s.value == 42 {0} else {1} }
     n: MyNone { 1 }
     }
     r2 := WIGA.maybe_wrap(42, 0)
     error += when r2 {
     s: MySome { 1 }
     n: MyNone { 0 }
     }
     r3 := WIGA.maybe_wrap(99u, 1)
     error += when r3 {
     s: MySome { if s.value == 99u {0} else {1} }
     n: MyNone { 1 }
     }
     void
     })

    // 8. Mixed container chain
    ( {
     r := WIGA.chain_box_to_opt(42)
     error += when r {
     s: MySome { if s.value.value == 42 {0} else {1} }
     n: MyNone { 1 }
     }
     r2 := WIGA.chain_box_to_opt(88u)
     error += when r2 {
     s: MySome { if s.value.value == 88u {0} else {1} }
     n: MyNone { 1 }
     }
     void
     })

    error
}

// ============================================================
// Main
// ============================================================

main() {
    error: CInt := 0
    error += test_generic_types()
    error += test_generic_fn_ptr_struct()
    error += test_generic_fn_ptr_struct_adversarial()
    error += test_generic_tu_alias()
    error += test_generic_tu_alias_adversarial()
    error += test_higher_order_generic_tu()
    error += test_higher_order_generic_tu_adversarial()
    error += test_module_scoped_generic_tu()
    error += test_module_scoped_generic_tu_adversarial()
    error += test_weak_int_generics()
    error += test_weak_int_generics_adversarial()
    error
}
