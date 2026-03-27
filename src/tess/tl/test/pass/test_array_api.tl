#module main
#import <Array.tl>

test_get() {
    error := 0
    arr   := Array.init(0, 5zu)
    i     := 0
    while i < 5, i += 1 { arr.v.[i] = i * 10 }
    if Array.get(arr, 0zu) != 0 { error += 1 }
    if Array.get(arr, 2zu) != 20 { error += 1 }
    if Array.get(arr, 4zu) != 40 { error += 1 }
    Array.free(arr.&)
    error
}

test_front_back() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if Array.front(arr) != 10 { error += 1 }
    if Array.back(arr) != 30 { error += 1 }
    Array.free(arr.&)
    error
}

test_pop() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    if Array.pop(arr.&) != 3 { error += 1 }
    if arr.size != 2zu { error += 1 }
    if Array.pop(arr.&) != 2 { error += 1 }
    if arr.size != 1zu { error += 1 }
    Array.free(arr.&)
    error
}

test_clear() {
    error := 0
    arr   := Array.init(42, 5zu)
    Array.clear(arr.&)
    if arr.size != 0zu { error += 1 }
    if arr.capacity < 5zu { error += 1 }
    Array.free(arr.&)
    error
}

test_insert() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    // Insert 2 at index 1: [1, 2, 3, 4]
    Array.insert(arr.&, 1zu, 2)
    if arr.size != 4zu { error += 1 }
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 3 { error += 1 }
    if arr.v.[3] != 4 { error += 1 }
    // Insert 0 at front
    Array.insert(arr.&, 0zu, 0)
    if arr.v.[0] != 0 { error += 1 }
    if arr.size != 5zu { error += 1 }
    // Insert 5 at end
    Array.insert(arr.&, 5zu, 5)
    if arr.v.[5] != 5 { error += 1 }
    if arr.size != 6zu { error += 1 }
    Array.free(arr.&)
    error
}

test_remove() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    Array.push(arr.&, 40)
    // Remove index 1 -> 20
    val := Array.remove(arr.&, 1zu)
    if val != 20 { error += 1 }
    if arr.size != 3zu { error += 1 }
    if arr.v.[0] != 10 { error += 1 }
    if arr.v.[1] != 30 { error += 1 }
    if arr.v.[2] != 40 { error += 1 }
    Array.free(arr.&)
    error
}

test_swap_remove() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    Array.push(arr.&, 40)
    // swap_remove index 1 -> returns 20, replaces with last (40)
    val := Array.swap_remove(arr.&, 1zu)
    if val != 20 { error += 1 }
    if arr.size != 3zu { error += 1 }
    if arr.v.[0] != 10 { error += 1 }
    if arr.v.[1] != 40 { error += 1 }
    if arr.v.[2] != 30 { error += 1 }
    Array.free(arr.&)
    error
}

test_resize() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    // Grow to 5 with fill value 99
    Array.resize(arr.&, 5zu, 99)
    if arr.size != 5zu { error += 1 }
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 99 { error += 1 }
    if arr.v.[3] != 99 { error += 1 }
    if arr.v.[4] != 99 { error += 1 }
    // Shrink to 2
    Array.resize(arr.&, 2zu, 0)
    if arr.size != 2zu { error += 1 }
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    Array.free(arr.&)
    error
}

test_shrink_to_fit() {
    error := 0
    arr   := Array.with_capacity[Int](100zu)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.shrink_to_fit(arr.&)
    if arr.size != 3zu { error += 1 }
    if arr.capacity != 3zu { error += 1 }
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 3 { error += 1 }
    // Shrink empty
    Array.clear(arr.&)
    Array.shrink_to_fit(arr.&)
    if arr.capacity != 0zu { error += 1 }
    error
}

test_truncate() {
    error := 0
    arr   := Array.init(7, 5zu)
    Array.truncate(arr.&, 3zu)
    if arr.size != 3zu { error += 1 }
    // Truncate beyond size is no-op
    Array.truncate(arr.&, 10zu)
    if arr.size != 3zu { error += 1 }
    Array.free(arr.&)
    error
}

test_append() {
    error := 0
    a     := Array.empty[Int]()
    Array.push(a.&, 1)
    Array.push(a.&, 2)
    b := Array.empty[Int]()
    Array.push(b.&, 3)
    Array.push(b.&, 4)
    Array.append(a.&, b)
    if a.size != 4zu { error += 1 }
    if a.v.[0] != 1 { error += 1 }
    if a.v.[1] != 2 { error += 1 }
    if a.v.[2] != 3 { error += 1 }
    if a.v.[3] != 4 { error += 1 }
    Array.free(a.&)
    Array.free(b.&)
    error
}

test_clone() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    c := Array.clone(arr)
    if c.size != 3zu { error += 1 }
    if c.v.[0] != 10 { error += 1 }
    if c.v.[1] != 20 { error += 1 }
    if c.v.[2] != 30 { error += 1 }
    // Modify original, clone should be independent
    arr.v.[0] = 999
    if c.v.[0] != 10 { error += 1 }
    Array.free(arr.&)
    Array.free(c.&)
    error
}

test_copy_range() {
    error := 0
    arr   := Array.empty[Int]()
    i     := 0
    while i < 5, i += 1 { Array.push(arr.&, i * 10) }
    // copy [1, 4) -> [10, 20, 30]
    s := Array.copy(arr, 1zu, 4zu)
    if s.size != 3zu { error += 1 }
    if s.v.[0] != 10 { error += 1 }
    if s.v.[1] != 20 { error += 1 }
    if s.v.[2] != 30 { error += 1 }
    // Empty copy
    e := Array.copy(arr, 2zu, 2zu)
    if e.size != 0zu { error += 1 }
    Array.free(arr.&)
    Array.free(s.&)
    error
}

test_slice() {
    error := 0
    arr   := Array.empty[Int]()
    i     := 0
    while i < 5, i += 1 { Array.push(arr.&, i * 10) }
    // slice [1, 4) -> non-owning view of [10, 20, 30]
    s := Array.slice(arr, 1zu, 4zu)
    if s.size != 3zu { error += 1 }
    if s.v.[0] != 10 { error += 1 }
    if s.v.[1] != 20 { error += 1 }
    if s.v.[2] != 30 { error += 1 }
    // Mutation through original is visible through slice
    arr.v.[1] = 999
    if s.v.[0] != 999 { error += 1 }
    // Empty slice
    e := Array.slice(arr, 2zu, 2zu)
    if e.size != 0zu { error += 1 }
    Array.free(arr.&)
    // No free needed for Slice (non-owning)
    error
}

test_from_ptr() {
    error := 0
    arr   := Array.init(0, 3zu)
    arr.v.[0] = 100
    arr.v.[1] = 200
    arr.v.[2] = 300
    result := Array.from_ptr(arr.v, 3zu)
    if result.size != 3zu { error += 1 }
    if result.v.[0] != 100 { error += 1 }
    if result.v.[1] != 200 { error += 1 }
    if result.v.[2] != 300 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_is_empty() {
    error := 0
    arr   := Array.empty[Int]()
    if !Array.is_empty(arr) { error += 1 }
    Array.push(arr.&, 1)
    if Array.is_empty(arr) { error += 1 }
    Array.free(arr.&)
    error
}

test_contains() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.contains(arr, 20) { error += 1 }
    if Array.contains(arr, 99) { error += 1 }
    Array.free(arr.&)
    error
}

test_index_of() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    when Array.index_of(arr, 20) {
        s: Some { if s.v != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    when Array.index_of(arr, 99) {
        s: Some { error += 1 }
        n: None { }
    }
    when Array.index_of(arr, 10) {
        s: Some { if s.v != 0zu { error += 1 } }
        n: None { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_reverse() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.reverse(arr.&)
    if arr.v.[0] != 4 { error += 1 }
    if arr.v.[1] != 3 { error += 1 }
    if arr.v.[2] != 2 { error += 1 }
    if arr.v.[3] != 1 { error += 1 }
    // Odd number of elements
    Array.push(arr.&, 5)
    Array.reverse(arr.&)
    if arr.v.[0] != 5 { error += 1 }
    if arr.v.[4] != 4 { error += 1 }
    Array.free(arr.&)
    error
}

test_swap() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    Array.swap(arr.&, 0zu, 2zu)
    if arr.v.[0] != 30 { error += 1 }
    if arr.v.[2] != 10 { error += 1 }
    if arr.v.[1] != 20 { error += 1 }
    Array.free(arr.&)
    error
}

test_map() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, (x) { x * 10 })
    if result.size != 3zu { error += 1 }
    if result.v.[0] != 10 { error += 1 }
    if result.v.[1] != 20 { error += 1 }
    if result.v.[2] != 30 { error += 1 }
    // Original unchanged
    if arr.v.[0] != 1 { error += 1 }
    // Empty array
    e := Array.empty[Int]()
    r := Array.map(e, (x) { x * 10 })
    if r.size != 0zu { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_filter() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, (x) { x > 3 })
    if result.size != 2zu { error += 1 }
    if result.v.[0] != 4 { error += 1 }
    if result.v.[1] != 5 { error += 1 }
    // Filter none
    r2 := Array.filter(arr, (x) { x > 100 })
    if r2.size != 0zu { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    Array.free(r2.&)
    error
}

test_any_of() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    if !Array.any_of(arr, (x) { x == 2 }) { error += 1 }
    if Array.any_of(arr, (x) { x == 99 }) { error += 1 }
    // Empty array
    e := Array.empty[Int]()
    if Array.any_of(e, (x) { x == 1 }) { error += 1 }
    Array.free(arr.&)
    error
}

test_all() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 2)
    Array.push(arr.&, 4)
    Array.push(arr.&, 6)
    if !Array.all(arr, (x) { x > 0 }) { error += 1 }
    if Array.all(arr, (x) { x > 3 }) { error += 1 }
    // Empty array -> all is vacuously true
    e := Array.empty[Int]()
    if !Array.all(e, (x) { x > 0 }) { error += 1 }
    Array.free(arr.&)
    error
}

test_find() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    when Array.find(arr, (x) { x == 20 }) {
        s: Some { if s.v != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    when Array.find(arr, (x) { x == 99 }) {
        s: Some { error += 1 }
        n: None { }
    }
    when Array.find(arr, (x) { x > 15 }) {
        s: Some { if s.v != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    Array.free(arr.&)
    error
}


test_foreach() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    count := 0
    Array.foreach(arr, (x) { count = count + 1 })
    if count != 3 { error += 1 }
    Array.free(arr.&)
    error
}

test_reduce() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    sum := Array.reduce(arr, 0, (acc, x) { acc + x })
    if sum != 10 { error += 1 }
    // With initial value
    sum2 := Array.reduce(arr, 100, (acc, x) { acc + x })
    if sum2 != 110 { error += 1 }
    // Empty array
    e    := Array.empty[Int]()
    sum3 := Array.reduce(e, 42, (acc, x) { acc + x })
    if sum3 != 42 { error += 1 }
    Array.free(arr.&)
    error
}


main() {
    error := 0
    error += test_get()
    error += test_front_back()
    error += test_pop()
    error += test_clear()
    error += test_insert()
    error += test_remove()
    error += test_swap_remove()
    error += test_resize()
    error += test_shrink_to_fit()
    error += test_truncate()
    error += test_append()
    error += test_clone()
    error += test_copy_range()
    error += test_slice()
    error += test_from_ptr()
    error += test_is_empty()
    error += test_contains()
    error += test_index_of()
    error += test_reverse()
    error += test_swap()
    error += test_map()
    error += test_filter()
    error += test_any_of()
    error += test_all()
    error += test_find()
    error += test_foreach()
    error += test_reduce()
    error
}
