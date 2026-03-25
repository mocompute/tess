#module main
#import <Print.tl>

add(a: Int, b: Int) -> Int { a + b }

main() {
    x := 10
    y := 20
    error := 0

    s1 := f"sum = {x + y}"
    if s1 != "sum = 30" { error += 1 }

    s2 := f"result = {add(x, y)}"
    if s2 != "result = 30" { error += 1 }

    s3 := f"val = {({a := 3; a + 7})}"
    if s3 != "val = 10" { error += 1 }

    s4 := f"double = {add(add(1, 2), add(3, 4))}"
    if s4 != "double = 10" { error += 1 }

    error
}
