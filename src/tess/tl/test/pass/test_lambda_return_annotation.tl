#module main

apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

main() {
    error := 0

    // basic return type annotation
    add := (a: Int, b: Int) -> Int { a + b }
    if add(2, 3) != 5 { error += 1 }

    // return type annotation with no parameter annotations
    inc := (x) -> Int { x + 1 }
    if inc(10) != 11 { error += 1 }

    // return type annotation on closure
    offset := 100
    shifted := (x: Int) -> Int { x + offset }
    if shifted(5) != 105 { error += 1 }

    // return type annotation on lambda passed as argument
    result := apply((x: Int) -> Int { x * 2 }, 7)
    if result != 14 { error += 1 }

    // void return type
    count := 0
    bump := (n: Int) -> Void { count = count + n, void }
    bump(3)
    if count != 3 { error += 1 }

    error
}
