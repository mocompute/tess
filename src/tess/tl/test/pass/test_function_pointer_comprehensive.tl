// Combined function pointer tests
// Sources: test_function_pointer_{argument,array,higher_order_return,in_struct,
//          in_struct_direct,in_struct_direct_2,mutable,pointer,recursive_type,value}.tl

#module Foo
#import <cstdlib.tl>

// Return type annotation is required because this function will not be specialized, because it is only
// every referenced through a function pointer.
malloc(count: CSize) -> Ptr[Int] {
    c_malloc(count)
}

Ctx_isd2[t1]: {
    malloc_fun: t1,
}

init_isd2() {
    Ctx_isd2(malloc_fun = malloc/1)
}


#module main
#import <cstdlib.tl>

// --- Helpers from test_function_pointer_argument ---
add1_arg(x) { x + 1 }
foo_arg(f, x) { f(x) }

// --- Helpers from test_function_pointer_value ---
add1_val(x) { x + 1 }
foo_val(f, x) { f(x) }

// --- Helpers from test_function_pointer_array ---
double_array (x: Int) -> Int { x * 2 }
triple_array (x: Int) -> Int { x * 3 }
negate_array (x: Int) -> Int { 0 - x }
add_one_array(x: Int) -> Int { x + 1 }

FnHolder: { fn: (Int) -> Int }

// --- Helpers from test_function_pointer_higher_order_return ---
double_hor (x: Int) -> Int { x * 2 }
triple_hor (x: Int) -> Int { x * 3 }
negate_hor (x: Int) -> Int { 0 - x }
add_one_hor(x: Int) -> Int { x + 1 }

Op: | Double
    | Triple
    | Negate
    | AddOne

get_operation(op: Op) {
    when op {
        d: Double { double_hor/1 }
        t: Triple { triple_hor/1 }
        n: Negate { negate_hor/1 }
        a: AddOne { add_one_hor/1 }
    }
}

select_fn(use_first: Bool, f1, f2) {
    if use_first { f1 } else { f2 }
}

// --- Helpers from test_function_pointer_in_struct ---
f1_is() { 1 }
f2_is() { 2 }

Ctx_is[t1, t2]: {
    p1: t1, p2: t2
}

init_is() {
    Ctx_is( p1 = f1_is/0, p2 = f2_is/0 )
}

// --- Helpers from test_function_pointer_in_struct_direct ---
f1_isd() { 1 }
f2_isd() { 2 }

Ctx_isd[t1]: {
    p1: t1, p2: t1
}

init_isd() {
    Ctx_isd( p1 = f1_isd/0, p2 = f2_isd/0 )
}

// --- Helpers from test_function_pointer_mutable ---
double_mut (x: Int) -> Int { x * 2 }
triple_mut (x: Int) -> Int { x * 3 }
negate_mut (x: Int) -> Int { 0 - x }
add_one_mut(x: Int) -> Int { x + 1 }

FnCtx: { fn: (Int) -> Int, id: Int }

// --- Helpers from test_function_pointer_pointer ---
negate_ptr (x) { 0 - x }
add_one_ptr(x) { x + 1 }

// --- Helpers from test_function_pointer_recursive_type ---
double_rec (x: Int) -> Int { x * 2 }
triple_rec (x: Int) -> Int { x * 3 }
add_one_rec(x: Int) -> Int { x + 1 }

Handler: { fn: (Int) -> Int }
Node: { handler: Handler, value: Int, next: Ptr[Node] }

apply_handler(h: Handler, x: Int) -> Int {
    h.fn(x)
}

// ============================================================
// Test functions
// ============================================================

test_argument() {
    foo_arg(add1_arg/1, -1)
}

test_array() {
    error := 0

    // Test 1: Array of structs containing function pointers
    fns: CArray[FnHolder, 4] := void
    fns.[0] = FnHolder(fn = double_array/1)
    fns.[1] = FnHolder(fn = triple_array/1)
    fns.[2] = FnHolder(fn = negate_array/1)
    fns.[3] = FnHolder(fn = add_one_array/1)

    // Test 2: Invoke first function (double)
    result1 := fns.[0].fn(5)
    error += if result1 == 10 {0} else {1}

    // Test 3: Invoke second function (triple)
    result2 := fns.[1].fn(5)
    error += if result2 == 15 {0} else {1}

    // Test 4: Invoke third function (negate)
    result3 := fns.[2].fn(7)
    error += if result3 == -7 {0} else {1}

    // Test 5: Invoke fourth function (add_one)
    result4 := fns.[3].fn(10)
    error += if result4 == 11 {0} else {1}

    // Test 6: Iterate and sum results for input 2
    sum := 0
    i   := 0
    while (i < 4) {
        holder := fns.[i]
        sum += holder.fn(2)
        i += 1
    }
    // double(2)=4, triple(2)=6, negate(2)=-2, add_one(2)=3 => 4+6-2+3=11
    error += if sum == 11 {0} else {1}

    // Test 7: Decay array to pointer and invoke
    ptr: Ptr[FnHolder] := fns
    result7            := ptr.[1].fn(4) // triple(4) = 12
    error += if result7 == 12 {0} else {1}

    error
}

test_higher_order_return() {
    error := 0

    // Test 1: Get double operation and apply
    op1     := Double()
    fn1     := get_operation(op1)
    result1 := fn1(5)
    error += if result1 == 10 {0} else {1}

    // Test 2: Get triple operation and apply
    op2     := Triple()
    fn2     := get_operation(op2)
    result2 := fn2(5)
    error += if result2 == 15 {0} else {1}

    // Test 3: Get negate operation and apply
    op3     := Negate()
    fn3     := get_operation(op3)
    result3 := fn3(7)
    error += if result3 == -7 {0} else {1}

    // Test 4: Get add_one operation and apply
    op4     := AddOne()
    fn4     := get_operation(op4)
    result4 := fn4(10)
    error += if result4 == 11 {0} else {1}

    // Test 5: Select between two function pointers (true case)
    fn5     := select_fn(true, double_hor/1, triple_hor/1)
    result5 := fn5(4)
    error += if result5 == 8 {0} else {1}

    // Test 6: Select between two function pointers (false case)
    fn6     := select_fn(false, double_hor/1, triple_hor/1)
    result6 := fn6(4)
    error += if result6 == 12 {0} else {1}

    // Test 7: Chain get_operation calls
    op_a := Triple()
    op_b := AddOne()
    fn_a := get_operation(op_a)
    fn_b := get_operation(op_b)
    // Apply both: triple(2) + add_one(2) = 6 + 3 = 9
    result7 := fn_a(2) + fn_b(2)
    error += if result7 == 9 {0} else {1}

    error
}

test_in_struct() {
    ctx := init_is()
    p   := ctx.p1
    p() - 1
}

test_in_struct_direct() {
    ctx := init_isd()
    ctx.p1() - 1
}

test_in_struct_direct_2() {
    ctx := Foo.init_isd2()
    p   := ctx.malloc_fun(16)
    c_free(p)
    0
}

test_mutable() {
    error := 0

    // Test 1: Reassigning function pointer variable
    fp      := double_mut/1
    result1 := fp(5)
    error += if result1 == 10 {0} else {1}

    fp = triple_mut/1                       // reassign
    result2 := fp(5)
    error += if result2 == 15 {0} else {1}

    fp = negate_mut/1                       // reassign again
    result3 := fp(7)
    error += if result3 == -7 {0} else {1}

    // Test 2: Reassigning function pointer field in struct
    ctx     := FnCtx(fn = double_mut/1, id = 1)
    result4 := ctx.fn(4)
    error += if result4 == 8 {0} else {1}

    // Test 3: Mutation through pointer to struct
    ctx_ptr := ctx.&
    ctx_ptr->fn = add_one_mut/1
    result5 := ctx.fn(10)               // original variable sees the change
    error += if result5 == 11 {0} else {1}

    // Test 4: Swapping function pointers
    fn_a := double_mut/1
    fn_b := triple_mut/1

    // Verify before swap
    error += if fn_a(3) == 6 {0} else {1}
    error += if fn_b(3) == 9 {0} else {1}

    // Swap
    temp := fn_a
    fn_a = fn_b
    fn_b = temp

    // Verify after swap
    error += if fn_a(3) == 9 {0} else {1} // now triple
    error += if fn_b(3) == 6 {0} else {1} // now double

    // Test 5: Multiple mutations to same struct field
    ctx2     := FnCtx(fn = double_mut/1, id = 2)
    sum      := ctx2.fn(1)              // 2
    ctx2_ptr := ctx2.&
    ctx2_ptr->fn = triple_mut/1
    sum += ctx2.fn(1)                   // 3
    ctx2_ptr->fn = add_one_mut/1
    sum += ctx2.fn(1)                   // 2
    error += if sum == 7 {0} else {1}

    error
}

test_pointer() {
    error := 0

    // Create pointer to function pointer
    fp := negate_ptr/1
    pp := fp.&

    // Dereference to local, then invoke: negate(5) = -5
    f       := pp.*
    result1 := f(5)
    error += if result1 == -5 {0} else {1}

    // Mutate through pointer to pointer
    pp.* = add_one_ptr/1
    result2 := fp(10)                   // fp now points to add_one
    error += if result2 == 11 {0} else {1}

    error
}

test_recursive_type() {
    error := 0

    // Create nodes manually (linked list of callbacks)
    // Node 3 (tail): add_one
    node3_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node3_ptr->handler = Handler(fn = add_one_rec/1)
    node3_ptr->value   = 3
    node3_ptr->next    = null

    // Node 2: triple
    node2_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node2_ptr->handler = Handler(fn = triple_rec/1)
    node2_ptr->value   = 2
    node2_ptr->next    = node3_ptr

    // Node 1 (head): double
    node1_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node1_ptr->handler = Handler(fn = double_rec/1)
    node1_ptr->value   = 1
    node1_ptr->next    = node2_ptr

    // Test 1: Invoke first node's handler (double)
    result1 := apply_handler(node1_ptr->handler, 5)
    error += if result1 == 10 {0} else {1}

    // Test 2: Invoke second node's handler (triple)
    result2 := apply_handler(node1_ptr->next->handler, 5)
    error += if result2 == 15 {0} else {1}

    // Test 3: Invoke third node's handler (add_one)
    result3 := apply_handler(node1_ptr->next->next->handler, 10)
    error += if result3 == 11 {0} else {1}

    // Test 4: Verify values are stored correctly
    error += if node1_ptr->value == 1 {0} else {1}
    error += if node2_ptr->value == 2 {0} else {1}
    error += if node3_ptr->value == 3 {0} else {1}

    // Test 5: Walk the list and accumulate results for input 2
    sum     := 0
    current := node1_ptr
    while (current != null) {
        sum += apply_handler(current->handler, 2)
        current = current->next
    }
    // double(2)=4, triple(2)=6, add_one(2)=3 => 13
    error += if sum == 13 {0} else {1}

    // Test 6: Third node's next is null
    error += if node3_ptr->next == null {0} else {1}

    // Cleanup
    c_free(node1_ptr)
    c_free(node2_ptr)
    c_free(node3_ptr)

    error
}

test_value() {
    fp := add1_val/1
    foo_val(fp, -1)
}

// ============================================================
// Main: run all tests and sum error counts
// ============================================================

main() {
    test_argument()
    + test_array()
    + test_higher_order_return()
    + test_in_struct()
    + test_in_struct_direct()
    + test_in_struct_direct_2()
    + test_mutable()
    + test_pointer()
    + test_recursive_type()
    + test_value()
}
