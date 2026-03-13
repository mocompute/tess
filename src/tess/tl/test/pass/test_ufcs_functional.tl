#module main
#import <Array.tl>

// Test: Functional-style method chains via UFCS

main() {
    error := 0

    arr := Array.empty[Int]()
    arr.push(1)
    arr.push(2)
    arr.push(3)
    arr.push(4)
    arr.push(5)

    // map — double each element
    doubled := arr.map((x) { x * 2 })
    error += if doubled.size == 5zu {0} else {1}
    error += if doubled.get(0zu) == 2 {0} else {1}
    error += if doubled.get(4zu) == 10 {0} else {1}

    // filter — keep elements > 3
    big := arr.filter((x) { x > 3 })
    error += if big.size == 2zu {0} else {1}
    error += if big.get(0zu) == 4 {0} else {1}

    // reduce — sum
    sum := arr.reduce(0, (a, b) { a + b })
    error += if sum == 15 {0} else {1}

    // any_of / all
    error += if arr.any_of((x) { x > 4 }) == true {0} else {1}
    error += if arr.any_of((x) { x > 10 }) == false {0} else {1}
    error += if arr.all((x) { x > 0 }) == true {0} else {1}
    error += if arr.all((x) { x > 3 }) == false {0} else {1}

    // foreach — side effect (just verify it doesn't crash)
    count := 0
    arr.foreach((x) { count = count + 1 })
    error += if count == 5 {0} else {1}

    // Chained: map then filter then reduce
    result := arr.map((x) { x * 2 }).filter((x) { x > 4 }).reduce(0, (a, b) { a + b })
    error += if result == 24 {0} else {1}    // 6+8+10

    // sorted + chained access
    arr2 := Array.empty[Int]()
    arr2.push(5)
    arr2.push(1)
    arr2.push(3)
    sorted := arr2.sorted((a, b) { a - b })
    error += if sorted.get(0zu) == 1 {0} else {1}
    error += if sorted.get(2zu) == 5 {0} else {1}

    // cleanup
    doubled.free()
    big.free()
    sorted.free()
    arr2.free()
    arr.free()

    error
}
