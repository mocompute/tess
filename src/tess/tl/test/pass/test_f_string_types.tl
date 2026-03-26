#module main
#import <Print.tl>

main() {
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
