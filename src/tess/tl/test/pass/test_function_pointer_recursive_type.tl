#module main
#import <cstdlib.tl>

// Test: Recursive type containing function pointer
// Implements a linked list of callbacks pattern

double (x: Int) -> Int { x * 2 }
triple (x: Int) -> Int { x * 3 }
add_one(x: Int) -> Int { x + 1 }

// Struct to hold function pointer
Handler: { fn: (Int) -> Int }

// Recursive struct: linked list node with function pointer
Node: { handler: Handler, value: Int, next: Ptr[Node] }

// Helper to apply handler
apply_handler(h: Handler, x: Int) -> Int {
    h.fn(x)
}

main() {
    error := 0

    // Create nodes manually (linked list of callbacks)
    // Node 3 (tail): add_one
    node3_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node3_ptr->handler = Handler(fn = add_one/1)
    node3_ptr->value   = 3
    node3_ptr->next    = null

    // Node 2: triple
    node2_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node2_ptr->handler = Handler(fn = triple/1)
    node2_ptr->value   = 2
    node2_ptr->next    = node3_ptr

    // Node 1 (head): double
    node1_ptr: Ptr[Node] := c_malloc(sizeof[Node]())
    node1_ptr->handler = Handler(fn = double/1)
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
