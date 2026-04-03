#module main
#import <Print.tl>
#import <String.tl>

add(a: Int, b: Int) -> Int { a + b }

test_basic() {
    name  := "world"
    age   := 42
    error := 0

    s1 := f"hello {name}"
    if s1 != "hello world" { error += 1 }

    s2 := f"hello {name}, age {age}"
    if s2 != "hello world, age 42" { error += 1 }

    s3 := f"{name}"
    if s3 != "world" { error += 1 }

    s4 := f"start {name} middle {age} end"
    if s4 != "start world middle 42 end" { error += 1 }

    error
}

test_expr() {
    x     := 10
    y     := 20
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

test_escape() {
    x     := 42
    error := 0

    // Escaped braces produce literal { and }
    s1 := f"value: {{{x}}}"
    if s1 != "value: {42}" { error += 1 }

    // Just escaped braces, no interpolation
    s2 := f"no interp: {{hello}}"
    if s2 != "no interp: {hello}" { error += 1 }

    // Escaped open brace adjacent to interpolation
    s3 := f"{{{x}"
    if s3 != "{42" { error += 1 }

    // Escaped close brace after interpolation
    s4 := f"{x}}}"
    if s4 != "42}" { error += 1 }

    error
}

test_adjacent() {
    x     := "hello"
    y     := "world"
    error := 0

    s1 := f"{x}{y}"
    if s1 != "helloworld" { error += 1 }

    s2 := f"{x}{y}!"
    if s2 != "helloworld!" { error += 1 }

    s3 := f"!{x}{y}"
    if s3 != "!helloworld" { error += 1 }

    error
}

test_nested_braces() {
    error := 0

    // Block expression with braces inside interpolation hole
    s1 := f"val = {({a := 10; b := 20; a + b})}"
    if s1 != "val = 30" { error += 1 }

    // Nested block expressions (brace depth > 1)
    s2 := f"val = {({x := ({y := 5; y * 2}); x + 1})}"
    if s2 != "val = 11" { error += 1 }

    // Multiple holes each with braces
    s3 := f"{({1 + 2})} and {({3 + 4})}"
    if s3 != "3 and 7" { error += 1 }

    error
}

test_no_holes() {
    error := 0

    s := f"hello world"
    if s != "hello world" { error += 1 }

    s2 := f""
    if s2 != "" { error += 1 }

    // Escaped braces but no actual interpolation
    s3 := f"{{braces}}"
    if s3 != "{braces}" { error += 1 }

    error
}

test_types() {
    error := 0

    i := 42
    f := 3.14
    b := true
    s := "hello"

    s1 := f"int={i}"
    if s1 != "int=42" { error += 1 }

    s2 := f"float={f}"
    if s2 != "float=3.14" { error += 1 }

    s3 := f"bool={b}"
    if s3 != "bool=true" { error += 1 }

    s4 := f"str={s}"
    if s4 != "str=hello" { error += 1 }

    s5 := f"{i} {f} {b} {s}"
    if s5 != "42 3.14 true hello" { error += 1 }

    s6 := f"val={42}"
    if s6 != "val=42" { error += 1 }

    s7 := f"{0} and {100}"
    if s7 != "0 and 100" { error += 1 }

    s8 := f"{42u}"
    if s8 != "42" { error += 1 }

    s9 := f"{42z}"
    if s9 != "42" { error += 1 }

    s10 := f"{42zu}"
    if s10 != "42" { error += 1 }

    s11 := f"{3.14}"
    if s11 != "3.14" { error += 1 }

    s12 := f"{true} and {false}"
    if s12 != "true and false" { error += 1 }

    error
}

test_backslash() {
    x     := 42
    error := 0

    // Backslash escapes in literal segments
    s1 := f"line1\nline2"
    if s1 != "line1\nline2" { error += 1 }

    // Backslash in segment before interpolation hole
    s2 := f"tab\there: {x}"
    if s2 != "tab\there: 42" { error += 1 }

    // Backslash in segment after interpolation hole
    s3 := f"{x}\ttab"
    if s3 != "42\ttab" { error += 1 }

    // Backslash-backslash (literal backslash)
    s4 := f"path: C:\\\\dir\\\\{x}"
    if s4 != "path: C:\\\\dir\\\\42" { error += 1 }

    error
}

main() {
    error := 0
    error += test_basic()
    error += test_expr()
    error += test_escape()
    error += test_adjacent()
    error += test_nested_braces()
    error += test_no_holes()
    error += test_types()
    error += test_backslash()
    error
}
