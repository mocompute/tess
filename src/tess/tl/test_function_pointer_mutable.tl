#module main
#import <stdlib.tl>

// Test: Reassignment and mutation of function pointers
// Tests various mutable operations on function pointers

double(x: Int) -> Int { x * 2 }
triple(x: Int) -> Int { x * 3 }
negate(x: Int) -> Int { 0 - x }
add_one(x: Int) -> Int { x + 1 }

// Struct with function pointer for mutation tests
FnCtx : { fn: (Int) -> Int, id: Int }

main() {
  error := 0

  // Test 1: Reassigning function pointer variable
  fp := double/1
  result1 := fp(5)
  error += if result1 == 10 {0} else {1}

  fp = triple/1  // reassign
  result2 := fp(5)
  error += if result2 == 15 {0} else {1}

  fp = negate/1  // reassign again
  result3 := fp(7)
  error += if result3 == -7 {0} else {1}

  // Test 2: Reassigning function pointer field in struct
  ctx := FnCtx(fn = double/1, id = 1)
  result4 := ctx.fn(4)
  error += if result4 == 8 {0} else {1}

  // Test 3: Mutation through pointer to struct
  ctx_ptr := ctx.&
  ctx_ptr->fn = add_one/1
  result5 := ctx.fn(10)  // original variable sees the change
  error += if result5 == 11 {0} else {1}

  // Test 4: Swapping function pointers
  fn_a := double/1
  fn_b := triple/1

  // Verify before swap
  error += if fn_a(3) == 6 {0} else {1}
  error += if fn_b(3) == 9 {0} else {1}

  // Swap
  temp := fn_a
  fn_a = fn_b
  fn_b = temp

  // Verify after swap
  error += if fn_a(3) == 9 {0} else {1}  // now triple
  error += if fn_b(3) == 6 {0} else {1}  // now double

  // Test 5: Multiple mutations to same struct field
  ctx2 := FnCtx(fn = double/1, id = 2)
  sum := ctx2.fn(1)  // 2
  ctx2_ptr := ctx2.&
  ctx2_ptr->fn = triple/1
  sum += ctx2.fn(1)  // 3
  ctx2_ptr->fn = add_one/1
  sum += ctx2.fn(1)  // 2
  error += if sum == 7 {0} else {1}

  error
}
