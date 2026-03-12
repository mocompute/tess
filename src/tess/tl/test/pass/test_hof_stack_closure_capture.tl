#module main
#import <Array.tl>

// Stack closures with captures passed to stdlib Array HOFs.

test_map_capture() {
    error  := 0
    offset: Int := 100
    arr    := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, (x) { x + offset })
    if result.size != 3zu { error += 1 }
    if result.v.[0] != 101 { error += 1 }
    if result.v.[1] != 102 { error += 1 }
    if result.v.[2] != 103 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_filter_capture() {
    error      := 0
    threshold: Int := 3
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, (x) { x > threshold })
    if result.size != 2zu { error += 1 }
    if result.v.[0] != 4 { error += 1 }
    if result.v.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_reduce_capture() {
    error    := 0
    scale: Int := 10
    arr      := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    sum := Array.reduce(arr, 0, (acc, x) { acc + x * scale })
    if sum != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_foreach_capture() {
    error := 0
    total := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    Array.foreach(arr, (x) { total = total + x })
    if total != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_any_of_capture() {
    error    := 0
    target: Int := 20
    arr      := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.any_of(arr, (x) { x == target }) { error += 1 }
    missing: Int := 99
    if Array.any_of(arr, (x) { x == missing }) { error += 1 }
    Array.free(arr.&)
    error
}

test_all_capture() {
    error    := 0
    bound: Int := 5
    arr      := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.all(arr, (x) { x > bound }) { error += 1 }
    high_bound: Int := 15
    if Array.all(arr, (x) { x > high_bound }) { error += 1 }
    Array.free(arr.&)
    error
}

test_find_capture() {
    error      := 0
    threshold: Int := 15
    arr        := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    when Array.find(arr, (x) { x > threshold }) {
        s: Some { if s.v != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_sort_capture() {
    error      := 0
    direction: Int := 1  // 1 = ascending, -1 = descending
    arr        := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.sort(arr.&, (a, b) { (a - b) * direction })
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 3 { error += 1 }

    // Now descending
    direction = 0 - 1
    Array.sort(arr.&, (a, b) { (a - b) * direction })
    if arr.v.[0] != 3 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 1 { error += 1 }
    Array.free(arr.&)
    error
}

main() {
    error: CInt := 0
    error += test_map_capture()
    error += test_filter_capture()
    error += test_reduce_capture()
    error += test_foreach_capture()
    error += test_any_of_capture()
    error += test_all_capture()
    error += test_find_capture()
    error += test_sort_capture()
    error
}
