#module main

// Test: Tagged union variant containing struct with function pointer field
// This tests the interaction between tagged unions and function pointers
// Note: Due to compiler limitations, we extract the struct to a local variable
// before accessing nested fields in pattern match branches
// FIXME: add a test of direct access of nested fields in tagged union case expression.

double(x: Int) -> Int { x * 2 }
triple(x: Int) -> Int { x * 3 }
negate(x: Int) -> Int { 0 - x }

// Struct with function pointer
FnCtx : { fn: (Int) -> Int }

// Tagged union that uses the struct in a variant
Callback : | Handler { ctx: FnCtx }
          | Value   { n: Int }
          | None

// Helper to apply callback safely
apply_handler(ctx: FnCtx, x: Int) -> Int {
  ctx.fn(x)
}

main() {
  error := 0

  // Test 1: Create Handler variant with function pointer in struct
  ctx1 := FnCtx(fn = double/1)
  handler1 := Callback.Handler(ctx = ctx1)
  result1 := case handler1: Callback {
    h: Handler { apply_handler(h.ctx, 5) }
    v: Value   { v.n }
    n: None    { 0 }
  }
  error += if result1 == 10 {0} else {1}

  // Test 2: Handler with different function
  ctx2 := FnCtx(fn = triple/1)
  handler2 := Callback.Handler(ctx = ctx2)
  result2 := case handler2: Callback {
    h: Handler { apply_handler(h.ctx, 5) }
    else       { 0 }
  }
  error += if result2 == 15 {0} else {1}

  // Test 3: Value variant
  val := Callback.Value(n = 42)
  result3 := case val: Callback {
    h: Handler { 0 }
    v: Value   { v.n }
    n: None    { 0 }
  }
  error += if result3 == 42 {0} else {1}

  // Test 4: None variant
  none_cb := Callback.None()
  result4 := case none_cb: Callback {
    h: Handler { 99 }
    v: Value   { 99 }
    n: None    { 7 }
  }
  error += if result4 == 7 {0} else {1}

  // Test 5: Pattern match and invoke with negate function
  ctx3 := FnCtx(fn = negate/1)
  handler3 := Callback.Handler(ctx = ctx3)
  result5 := case handler3: Callback {
    h: Handler { apply_handler(h.ctx, 10) }
    else       { 0 }
  }
  error += if result5 == -10 {0} else {1}

  error
}
