// Combined tagged union test file.
// Merges all test_tagged_union*.tl files into a single comprehensive test.

// =============================================================================
// Non-main modules (renamed to avoid collisions)
// =============================================================================

// --- From test_tagged_union.tl: originally #module Foo ---
#module TU_Foo1
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

// --- From test_tagged_union_bail.tl: originally #module MyOption ---
#module TU_MyOption1
T[a]: | MySome { value: a }
      | MyNone

// --- From test_tagged_union_case_annotation.tl: originally #module Shapes ---
#module TU_Shapes
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

// --- From test_tagged_union_cross_module_positional.tl: originally #module Foo ---
#module TU_Foo2
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

// --- From test_tagged_union_nullary_bare_own_module.tl: originally #module Foo ---
#module TU_Foo3
Op: | A { v: Int } | B

make_b() -> Op {
    B
}

check() -> CInt {
    a           := A(42)
    b           := B
    error: CInt := 0
    error += when a { x: A { 0 } y: B { 1 } }
    error += when b { x: A { 1 } y: B { 0 } }
    error
}

// --- From test_tagged_union_multi_same_module.tl: originally #module Lib ---
#module TU_Lib
Color: | Red | Green | Blue
Shape: | Circle { radius: Float } | Square { length: Float }

make_circle(r: Float) -> Shape {
    Circle(r)
}

// --- From test_tagged_union_option.tl: originally #module MyOption ---
#module TU_MyOption2
T[a]: | Has { v: a }
      | Empty

test1() -> CInt {
    error: CInt := 0

    good   := TU_MyOption2.Has(123)
    result := when good {
        x: Has { x.v }
        n: Empty { 456 }
    }
    error += if result - 123 == 0 {0} else {1}

    good   := TU_MyOption2.Has(0.5)
    result := when good {
        x: Has { x.v }
        n: Empty { 45.6 }
    }
    error += if result - 0.5 == 0.0 {0} else {1}

    error
}

// --- From test_tagged_union_scoped_bare_cross_module.tl: originally #module Foo ---
#module TU_Foo4
Op: | A | B

// --- From test_tagged_union_scoped_variant.tl: originally #module Foo ---
#module TU_Foo5
Shape: | Circle { radius: Float }
       | Square { length: Float }

// --- From test_tagged_union_unscoped.tl: originally #module Foo ---
#module TU_Foo6
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

// =============================================================================
// #module main
// =============================================================================

#module main

// --- Types from test_tagged_union_bail_nested.tl ---
OptInt: | Val { v: Int } | OptIntEmpty

// --- Types from test_tagged_union_carray.tl ---
CArrayShape: | CArrayCircle { radius: Float }
             | CArraySquare { length: Float }
             | CArrayMyNone

// --- Types from test_tagged_union_function_pointer.tl ---
FnCtx: { fn: (Int) -> Int }

Callback: | Handler { ctx: FnCtx }
          | CBValue { n: Int }
          | CBNone

// --- Types from test_tagged_union_generic.tl ---
// Variant names prefixed with Opt to avoid clashing with external module variant names
MyOpt[a]: | OptSome { value: a }
          | OptNone

Box[a]: | BoxHas { v: a }
        | BoxEmpty

Either[a, b]: | Left  { v: a }
              | Right { v: b }

IntHandler: { fn: (Int) -> Int }

Outcome[OT]: | Success { value: OT }
             | Failure { code: Int, handler: IntHandler }

// --- Types from test_tagged_union_generic_case.tl ---
GCaseOption[a]: | GCaseSome { value: a }
                | GCaseNone

// --- Types from test_tagged_union_generic_func.tl ---
GFuncOption[a]: | GFuncSome { value: a }
                | GFuncNone

// --- Types from test_tagged_union_generic_param.tl ---
GParamOption[a]: | GParamSome { value: a }
                 | GParamNone

// --- Types from test_tagged_union_generic_scoped.tl ---
GScopedOpt[a]: | GScopedSome { value: a } | GScopedNone

// --- Types from test_tagged_union_many_variants.tl ---
Token: | IntLit    { v: Int }
       | FloatLit  { v: Float }
       | BoolLit   { v: Bool }
       | StringLit { v: CString }
       | Ident     { name: CString }
       | Eof

// --- Types from test_tagged_union_mutable_case.tl ---
MutShape: | MutCircle { radius: Int }
          | MutSquare { length: Int }

// --- Types from test_tagged_union_nested_when.tl ---
NWOption[a]: | NWSome { value: a }
             | NWNone

// --- Types from test_tagged_union_nullary_bare.tl ---
NBOp: | NBA { v: Int } | NBB

// --- Types from test_tagged_union_pointer_field.tl ---
Container: | Owned { value: Int }
           | Ref { ptr: Ptr[Int] }
           | ContainerEmpty

// --- Types from test_tagged_union_recursive_type.tl ---
IntList: | Cons { head: Int, tail: Ptr[IntList] }
         | Nil

// --- Types from test_tagged_union_scoped_construction.tl ---
SCShape: | SCCircle { radius: Float }
         | SCSquare { length: Float }
         | SCMyNone

// --- Types from test_tagged_union_scoped_variant_as_union.tl ---
SVOp: | SVA | SVB

// =============================================================================
// Helper functions
// =============================================================================

// --- From test_tagged_union_bail_nested.tl ---
optint_eq(a: OptInt, b: OptInt) -> Bool {
    sa: Val := a else {
        nb: OptIntEmpty := b else { return false }
        return true
    }
    sb: Val := b else { return false }
    sa.v == sb.v
}

// --- From test_tagged_union_function_pointer.tl ---
fp_double(x: Int) -> Int { x * 2 }
fp_triple(x: Int) -> Int { x * 3 }
fp_negate(x: Int) -> Int { 0 - x }

fp_apply_handler(ctx: FnCtx, x: Int) -> Int {
    ctx.fn(x)
}

// --- From test_tagged_union_generic.tl ---
my_unwrap[a](opt: MyOpt[a], default: a) -> a {
    when opt {
        s: OptSome { s.value }
        n: OptNone { default }
    }
}

my_unwrap_int(opt: MyOpt[Int]) -> Int {
    when opt {
        s: OptSome { s.value }
        n: OptNone { 0 }
    }
}

make_box_has(x) {
    BoxHas(x)
}

gen_double_int(x: Int) -> Int { x * 2 }
gen_negate    (x: Int) -> Int { 0 - x }

apply_int_handler(h: IntHandler, x: Int) -> Int {
    h.fn(x)
}

// --- From test_tagged_union_generic_func.tl ---
gfunc_unwrap[a](opt: GFuncOption[a], default: a) -> a {
    when opt {
        s: GFuncSome { s.value }
        n: GFuncNone { default }
    }
}

// --- From test_tagged_union_generic_param.tl ---
gparam_unwrap(opt: GParamOption[Int]) -> Int {
    when opt {
        s: GParamSome { s.value }
        n: GParamNone { 0 }
    }
}

// --- From test_tagged_union_many_variants.tl ---
classify(tok: Token) -> Int {
    when tok {
        i: IntLit { 1 }
        f: FloatLit { 2 }
        b: BoolLit { 3 }
        s: StringLit { 4 }
        d: Ident { 5 }
        e: Eof { 0 }
    }
}

// --- From test_tagged_union_scoped_variant_as_union.tl ---
sv_f(op: SVOp) -> Bool {
    when op {
        a: SVA { true }
        b: SVB { false }
    }
}

// =============================================================================
// Test functions (one per original file)
// =============================================================================

// --- From test_tagged_union.tl ---
test_tagged_union() {
    error := 0

    circle := TU_Foo1.Circle(2.0)
    square := TU_Foo1.Square(1.5)
    none   := TU_Foo1.MyNone()

    area1 := when circle {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        n:  MyNone { 0.0 }
    }
    error += if area1 >= 12.0 {0} else {1}

    area2 := when square {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        else { 0.0 }
    }
    error += if area2 >= 2.25 {0} else {1}

    area3 := when none {
        c: Circle { c.radius * c.radius * 3.14159 }
        else { -1.0 }
    }
    error += if area3 == -1.0 {0} else {1}

    area4 := when square.& {
        sq: Square { sq->length = 3.0
            sq->length * sq->length }
        else { 0.0 }
    }
    error += if area4 >= 9.0 {0} else {1}

    error
}

// --- From test_tagged_union_bail.tl ---
test_bail_basic() {
    val       := TU_MyOption1.MySome(42)
    s: MySome := val else { return 1 }
    if s.value == 42 {0} else {1}
}

test_bail_else_path() {
    val       := if 0 == 1 { TU_MyOption1.MySome(0) } else { TU_MyOption1.MyNone() }
    s: MySome := val else { return 0 }
    1
}

test_bail_remaining_body() {
    val       := TU_MyOption1.MySome(10)
    s: MySome := val else { return 1 }
    x         := s.value + 5
    y         := x * 2
    if y == 30 {0} else {1}
}

test_bail_multiple() {
    a         := TU_MyOption1.MySome(10)
    b         := TU_MyOption1.MySome(20)
    x: MySome := a else { return 1 }
    y: MySome := b else { return 1 }
    if x.value + y.value == 30 {0} else {1}
}

test_bail_generic() {
    val       := TU_MyOption1.MySome(3.14)
    s: MySome := val else { return 1 }
    if s.value > 3.0 {0} else {1}
}

test_bail_break() {
    val    := TU_MyOption1.MySome(99)
    result := 1
    while 1 {
        s: MySome := val else { break }
        result = if s.value == 99 {0} else {1}
        break
    }
    result
}

test_bail_continue() {
    result := 0
    i      := 0
    while i < 3 {
        v := if i == 1 { TU_MyOption1.MySome(42) } else { TU_MyOption1.MyNone() }
        i = i + 1
        s: MySome := v else { continue }
        result = result + s.value
    }
    if result == 42 {0} else {1}
}

// --- From test_tagged_union_bail_nested.tl ---
test_bail_nested() {
    error := 0

    a := Val(10)
    b := Val(20)
    n := OptInt.OptIntEmpty

    error += if optint_eq(a, a) {0} else {1}
    error += if optint_eq(a, b) {1} else {0}
    error += if optint_eq(n, n) {0} else {1}
    error += if optint_eq(a, n) {1} else {0}

    error
}

// --- From test_tagged_union_carray.tl ---
test_carray() {
    error := 0

    shapes: CArray[CArrayShape, 4] := void
    shapes.[0] = CArrayCircle(1.0)
    shapes.[1] = CArraySquare(2.0)
    shapes.[2] = CArrayMyNone()
    shapes.[3] = CArrayCircle(3.0)

    s0 := shapes.[0]
    r0 := when s0 {
        c: CArrayCircle { c.radius }
        else { 0.0 }
    }
    error += if r0 == 1.0 {0} else {1}

    s1 := shapes.[1]
    r1 := when s1 {
        s: CArraySquare { s.length }
        else { 0.0 }
    }
    error += if r1 == 2.0 {0} else {1}

    s2 := shapes.[2]
    r2 := when s2 {
        n: CArrayMyNone { -1.0 }
        else { 0.0 }
    }
    error += if r2 == -1.0 {0} else {1}

    sum                   := 0.0
    ptr: Ptr[CArrayShape] := shapes
    i                     := 0
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c: CArrayCircle { c.radius }
            else { 0.0 }
        }
    }
    error += if sum == 4.0 {0} else {1}

    error
}

// --- From test_tagged_union_case_annotation.tl ---
test_case_annotation() {
    error := 0

    s1 := TU_Shapes.Circle(2.0)
    r1 := case s1: Shape {
        c: Circle { c.radius }
        s: Square { s.length }
        n: MyNone { 0.0 }
    }
    error += if r1 >= 2.0 {0} else {1}

    s2 := TU_Shapes.Square(3.0)
    r2 := case s2: Shape {
        c: Circle { c.radius }
        else { -1.0 }
    }
    error += if r2 == -1.0 {0} else {1}

    error
}

// --- From test_tagged_union_cross_module_positional.tl ---
test_cross_module_positional() {
    error := 0

    s1 := TU_Foo2.Shape.Circle(2.0)
    r1 := when s1 { c: Circle { c.radius } s: Square { s.length } n: MyNone { 0.0 } }
    error += if r1 >= 2.0 {0} else {1}

    s2 := TU_Foo2.Shape.Square(3.0)
    r2 := when s2 { c: Circle { c.radius } s: Square { s.length } n: MyNone { 0.0 } }
    error += if r2 >= 3.0 {0} else {1}

    s3 := TU_Foo2.Shape.MyNone()
    r3 := when s3 { c: Circle { 1.0 } s: Square { 1.0 } n: MyNone { 0.0 } }
    error += if r3 == 0.0 {0} else {1}

    error
}

// --- From test_tagged_union_function_pointer.tl ---
test_function_pointer() {
    error := 0

    ctx1     := FnCtx(fn = fp_double/1)
    handler1 := Handler(ctx1)
    result1  := when handler1 {
        h: Handler { fp_apply_handler(h.ctx, 5) }
        v: CBValue { v.n }
        n: CBNone { 0 }
    }
    error += if result1 == 10 {0} else {1}

    ctx2     := FnCtx(fn = fp_triple/1)
    handler2 := Handler(ctx2)
    result2  := when handler2 {
        h: Handler { fp_apply_handler(h.ctx, 5) }
        else { 0 }
    }
    error += if result2 == 15 {0} else {1}

    val     := CBValue(42)
    result3 := when val {
        h: Handler { 0 }
        v: CBValue { v.n }
        n: CBNone { 0 }
    }
    error += if result3 == 42 {0} else {1}

    none_cb := CBNone()
    result4 := when none_cb {
        h: Handler { 99 }
        v: CBValue { 99 }
        n: CBNone { 7 }
    }
    error += if result4 == 7 {0} else {1}

    ctx3     := FnCtx(fn = fp_negate/1)
    handler3 := Handler(ctx3)
    result5  := when handler3 {
        h: Handler { fp_apply_handler(h.ctx, 10) }
        else { 0 }
    }
    error += if result5 == -10 {0} else {1}

    error
}

// --- From test_tagged_union_generic.tl ---
test_generic_basic() {
    good: MyOpt[Int] := OptNone()
    0
}

test_generic_case() {
    good := OptSome(123)

    res := when good {
        s: OptSome { s.value }
        n: OptNone { 0 }
    }
    res - 123
}

test_generic_func() {
    val := OptSome(42)

    val_float := OptSome(0.5)
    if my_unwrap(val_float, 0.0) != 0.5 { return 1 }

    my_unwrap(val, 0) - 42
}

test_generic_param() {
    val          := OptSome(42)
    result: CInt := my_unwrap_int(val) - 42
    result
}

test_generic_nested() {
    nested := BoxHas(BoxHas(42))
    0
}

test_generic_return() {
    val := make_box_has(42)
    0
}

test_generic_multi() {
    x: Either[Int, Bool] := Left(42)
    0
}

test_generic_scoped() {
    o1 := MyOpt.OptSome(42)
    r  := when o1 { s: OptSome { s.value } n: OptNone { 0 } }
    if r == 42 {0} else {1}
}

test_generic_function_pointer() {
    error := 0

    ok_int  := Success(42)
    result1 := when ok_int {
        o: Success { o.value }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result1 == 42 {0} else {1}

    ok_float := Success(3.14)
    result2  := when ok_float {
        o: Success { o.value }
        e: Failure { 0.0 }
    }
    error += if result2 == 3.14 {0} else {1}

    h1                 := IntHandler(fn = gen_double_int/1)
    err1: Outcome[Int] := Failure(5, h1)

    result3 := when err1 {
        o: Success { 0 }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result3 == 10 {0} else {1}

    h2                 := IntHandler(fn = gen_negate/1)
    err2: Outcome[Int] := Failure(7, h2)

    result4 := when err2 {
        o: Success { 0 }
        e: Failure { apply_int_handler(e.handler, e.code) }
    }
    error += if result4 == -7 {0} else {1}

    error
}

// --- From test_tagged_union_generic_case.tl (standalone) ---
test_generic_case_standalone() {
    good := GCaseSome(123)

    res := when good {
        s: GCaseSome { s.value }
        n: GCaseNone { 0 }
    }
    res - 123
}

// --- From test_tagged_union_generic_func.tl (standalone) ---
test_generic_func_standalone() {
    val := GFuncSome(42)

    val_float := GFuncSome(0.5)
    if gfunc_unwrap(val_float, 0.0) != 0.5 { return 1 }

    gfunc_unwrap(val, 0) - 42
}

// --- From test_tagged_union_generic_param.tl (standalone) ---
test_generic_param_standalone() {
    val          := GParamSome(42)
    result: CInt := gparam_unwrap(val) - 42
    result
}

// --- From test_tagged_union_generic_scoped.tl (standalone) ---
test_generic_scoped_standalone() {
    o1 := GScopedOpt.GScopedSome(42)
    r  := when o1 { s: GScopedSome { s.value } n: GScopedNone { 0 } }
    if r == 42 {0} else {1}
}

// --- From test_tagged_union_many_variants.tl ---
test_many_variants() {
    error := 0

    error += if classify(IntLit(42)) == 1 {0} else {1}
    error += if classify(FloatLit(3.14)) == 2 {0} else {1}
    error += if classify(BoolLit(true)) == 3 {0} else {1}
    error += if classify(StringLit(c"hi")) == 4 {0} else {1}
    error += if classify(Ident(c"foo")) == 5 {0} else {1}
    error += if classify(Eof()) == 0 {0} else {1}

    tok        := Ident(c"x")
    is_literal := when tok {
        i: IntLit { 1 }
        f: FloatLit { 1 }
        b: BoolLit { 1 }
        s: StringLit { 1 }
        else { 0 }
    }
    error += if is_literal == 0 {0} else {1}

    tok1 := IntLit(99)
    tok2 := FloatLit(2.5)

    v1 := when tok1 {
        i: IntLit { i.v }
        else { 0 }
    }
    error += if v1 == 99 {0} else {1}

    v2 := when tok2 {
        f: FloatLit { f.v }
        else { 0.0 }
    }
    error += if v2 == 2.5 {0} else {1}

    error
}

// --- From test_tagged_union_multi_same_module.tl ---
test_multi_same_module() {
    error := 0

    s1 := TU_Lib.Circle(5.0)
    r1 := when s1 { c: Circle { c.radius } s: Square { s.length } }
    error += if r1 >= 5.0 {0} else {1}

    c1 := TU_Lib.Color.Red
    r2 := when c1 { r: Red { 1 } g: Green { 2 } b: Blue { 3 } }
    error += if r2 == 1 {0} else {1}

    s2 := TU_Lib.make_circle(7.0)
    r3 := when s2 { c: Circle { c.radius } s: Square { s.length } }
    error += if r3 >= 7.0 {0} else {1}

    error
}

// --- From test_tagged_union_mutable_case.tl ---
test_mutable_case() {
    shape := MutCircle(3)

    when shape.& {
        c: MutCircle { c->radius = 5 }
        else { }
    }

    result := when shape {
        c: MutCircle { c.radius }
        s: MutSquare { s.length }
    }

    ret: CInt := result - 5
    ret
}

// --- From test_tagged_union_nested_when.tl ---
test_nested_when() {
    outer := NWSome(NWSome(42))

    result := when outer {
        s: NWSome {
            inner := s.value
            when inner {
                s2: NWSome { s2.value }
                n:  NWNone { 0 }
            }
        }
        n: NWNone { 0 }
    }
    result - 42
}

// --- From test_tagged_union_nullary_bare.tl ---
test_nullary_bare() {
    a     := NBA(42)
    b     := NBB
    error := 0
    error += when a { x: NBA { 0 } y: NBB { 1 } }
    error += when b { x: NBA { 1 } y: NBB { 0 } }
    error
}

// --- From test_tagged_union_nullary_bare_own_module.tl ---
test_nullary_bare_own_module() {
    TU_Foo3.check()
}

// --- From test_tagged_union_option.tl ---
test_option() {
    error := 0
    error += TU_MyOption2.test1()
    error
}

// --- From test_tagged_union_pointer_field.tl ---
test_pointer_field() {
    error := 0

    val    := 42
    r      := Ref(val.&)
    result := when r {
        o: Owned { o.value }
        r: Ref { r.ptr.* }
        e: ContainerEmpty { 0 }
    }
    error += if result == 42 {0} else {1}

    when r.& {
        r: Ref { r->ptr.* = 99 }
        else { }
    }
    error += if val == 99 {0} else {1}

    null_ptr: Ptr[Int] := null
    null_ref           := Ref(null_ptr)
    is_null            := when null_ref {
        r: Ref { if r.ptr == null {1} else {0} }
        else { 0 }
    }
    error += if is_null == 1 {0} else {1}

    owned  := Owned(77)
    result := when owned {
        o: Owned { o.value }
        else { 0 }
    }
    error += if result == 77 {0} else {1}

    empty  := ContainerEmpty()
    result := when empty {
        e: ContainerEmpty { -1 }
        else { 0 }
    }
    error += if result == -1 {0} else {1}

    error
}

// --- From test_tagged_union_recursive_type.tl ---
test_recursive_type() {
    nil: IntList := Nil()
    n1           := Cons(1, nil.&)
    n2           := Cons(2, n1.&)

    result := when n2 {
        c: Cons { c.head }
        n: Nil { 0 }
    }
    ret: CInt := result - 2
    ret
}

// --- From test_tagged_union_scoped_bare_cross_module.tl ---
test_scoped_bare_cross_module() {
    error := 0

    x := TU_Foo4.Op.A
    r := when x {
        a: A { 1 }
        b: B { 0 }
    }
    error += if r == 1 { 0 } else { 1 }

    error
}

// --- From test_tagged_union_scoped_construction.tl ---
test_scoped_construction() {
    error := 0

    s1 := SCShape.SCCircle(2.0)
    r1 := when s1 { c: SCCircle { c.radius } s: SCSquare { s.length } n: SCMyNone { 0.0 } }
    error += if r1 >= 2.0 {0} else {1}

    s2 := SCShape.SCCircle(radius = 3.0)
    r2 := when s2 { c: SCCircle { c.radius } else { 0.0 } }
    error += if r2 >= 3.0 {0} else {1}

    s3 := SCCircle(radius = 4.0)
    r3 := when s3 { c: SCCircle { c.radius } else { 0.0 } }
    error += if r3 >= 4.0 {0} else {1}

    s4 := SCShape.SCMyNone()
    r4 := when s4 { c: SCCircle { 1.0 } s: SCSquare { 1.0 } n: SCMyNone { 0.0 } }
    error += if r4 == 0.0 {0} else {1}

    error
}

// --- From test_tagged_union_scoped_variant.tl ---
test_scoped_variant() {
    error := 0

    shape1 := TU_Foo5.Shape.Circle(radius = 2.0)

    r1 := when shape1 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r1 >= 2.0 {0} else {1}

    shape2 := TU_Foo5.Circle(radius = 3.0)

    r2 := when shape2 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r2 >= 3.0 {0} else {1}

    shape3 := TU_Foo5.Shape.Square(length = 4.0)

    r3 := when shape3 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r3 >= 4.0 {0} else {1}

    error
}

// --- From test_tagged_union_scoped_variant_as_union.tl ---
test_scoped_variant_as_union() {
    error := 0

    x := SVOp.SVA
    r := sv_f(x)
    error += if r { 0 } else { 1 }

    error
}

// --- From test_tagged_union_unscoped.tl ---
test_unscoped() {
    error := 0

    circle := TU_Foo6.Circle(2.0)
    square := TU_Foo6.Square(1.5)
    none   := TU_Foo6.MyNone()

    area1 := when circle {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        n:  MyNone { 0.0 }
    }
    error += if area1 >= 12.0 {0} else {1}

    area2 := when square {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        else { 0.0 }
    }
    error += if area2 >= 2.25 {0} else {1}

    area3 := when square.& {
        sq: Square { sq->length = 3.0
            sq->length * sq->length }
        else { 0.0 }
    }
    error += if area3 >= 9.0 {0} else {1}

    error
}

// =============================================================================
// Main: run all tests
// =============================================================================

main() {
    error := 0
    error += test_tagged_union()
    error += test_bail_basic()
    error += test_bail_else_path()
    error += test_bail_remaining_body()
    error += test_bail_multiple()
    error += test_bail_generic()
    error += test_bail_break()
    error += test_bail_continue()
    error += test_bail_nested()
    error += test_carray()
    error += test_case_annotation()
    error += test_cross_module_positional()
    error += test_function_pointer()
    error += test_generic_basic()
    error += test_generic_case()
    error += test_generic_func()
    error += test_generic_param()
    error += test_generic_nested()
    error += test_generic_return()
    error += test_generic_multi()
    error += test_generic_scoped()
    error += test_generic_function_pointer()
    error += test_generic_case_standalone()
    error += test_generic_func_standalone()
    error += test_generic_param_standalone()
    error += test_generic_scoped_standalone()
    error += test_many_variants()
    error += test_multi_same_module()
    error += test_mutable_case()
    error += test_nested_when()
    error += test_nullary_bare()
    error += test_nullary_bare_own_module()
    error += test_option()
    error += test_pointer_field()
    error += test_recursive_type()
    error += test_scoped_bare_cross_module()
    error += test_scoped_construction()
    error += test_scoped_variant()
    error += test_scoped_variant_as_union()
    error += test_unscoped()
    error
}
