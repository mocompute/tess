#module main
#import <Print.tl>

test_align() {
    error := 0

    x := 42

    s1 := f"{x:>10}"
    if s1 != "        42" { error += 1 }

    s2 := f"{x:<10}"
    if s2 != "42        " { error += 1 }

    s3 := f"{x:^10}"
    if s3 != "    42    " { error += 1 }

    error
}

test_combined() {
    error := 0

    pi := 3.14159265

    s1 := f"{pi:>+10.2f}"
    if s1 != "     +3.14" { error += 1 }

    x  := 255
    s2 := f"{x:#010x}"
    if s2 != "0x000000ff" { error += 1 }

    s3 := f"pi is {pi:.2f}, hex is {x:#x}"
    if s3 != "pi is 3.14, hex is 0xff" { error += 1 }

    error
}

test_fill() {
    error := 0

    x := 42

    s1 := f"{x:*>10}"
    if s1 != "********42" { error += 1 }

    s2 := f"{x:.<10}"
    if s2 != "42........" { error += 1 }

    s3 := f"{x:-^10}"
    if s3 != "----42----" { error += 1 }

    error
}

test_float_precision() {
    error := 0

    pi := 3.14159265

    s1 := f"{pi:.2f}"
    if s1 != "3.14" { error += 1 }

    s2 := f"{pi:.0f}"
    if s2 != "3" { error += 1 }

    s3 := f"{pi:.4f}"
    if s3 != "3.1416" { error += 1 }

    x  := 1.0
    s4 := f"{x:.2f}"
    if s4 != "1.00" { error += 1 }

    error
}

test_float_scientific() {
    error := 0

    x := 12345.6789

    s1 := f"{x:e}"
    if s1 != "1.234568e+04" { error += 1 }

    s2 := f"{x:.3e}"
    if s2 != "1.235e+04" { error += 1 }

    s3 := f"{x:.2E}"
    if s3 != "1.23E+04" { error += 1 }

    error
}

// Tests that generic-only format specs (width/align/fill)
// work with any ToString type, even without ToStringFormat.
test_generic_only() {
    error := 0

    s := "hello"

    s1 := f"{s:>20}"
    if s1 != "               hello" { error += 1 }

    s2 := f"{s:*^20}"
    if s2 != "*******hello********" { error += 1 }

    b  := true
    s3 := f"{b:>10}"
    if s3 != "      true" { error += 1 }

    error
}

test_int_bases() {
    error := 0

    x := 42

    s1 := f"{x:o}"
    if s1 != "52" { error += 1 }

    s2 := f"{x:#o}"
    if s2 != "0o52" { error += 1 }

    s3 := f"{x:b}"
    if s3 != "101010" { error += 1 }

    s4 := f"{x:#b}"
    if s4 != "0b101010" { error += 1 }

    error
}

test_int_decimal() {
    error := 0

    x := 42

    s1 := f"{x:d}"
    if s1 != "42" { error += 1 }

    s2 := f"{x:8d}"
    if s2 != "      42" { error += 1 }

    s3 := f"{x:08d}"
    if s3 != "00000042" { error += 1 }

    s4 := f"{x:+d}"
    if s4 != "+42" { error += 1 }

    y  := -7
    s5 := f"{y:+d}"
    if s5 != "-7" { error += 1 }

    s6 := f"{y:6d}"
    if s6 != "    -7" { error += 1 }

    u: UInt := 255
    s7      := f"{u:d}"
    if s7 != "255" { error += 1 }

    error
}

test_int_hex() {
    error := 0

    x := 255

    s1 := f"{x:x}"
    if s1 != "ff" { error += 1 }

    s2 := f"{x:X}"
    if s2 != "FF" { error += 1 }

    s3 := f"{x:#x}"
    if s3 != "0xff" { error += 1 }

    s4 := f"{x:#X}"
    if s4 != "0XFF" { error += 1 }

    error
}

test_int_min_binary() {
    error := 0

    // INT_MIN (LLONG_MIN) in binary -- exercises the fixed UB path
    // -9223372036854775808 = LLONG_MIN
    // Its binary magnitude is 1 followed by 63 zeros.
    min_val: Int := -9223372036854775807 - 1
    s            := f"{min_val:b}"
    if s != "-1000000000000000000000000000000000000000000000000000000000000000" { error += 1 }

    error
}

test_large_width() {
    error := 0

    // Width > 256 exercises the heap-allocation path in apply_layout
    s := f"{'x':>300}"
    if s.len() != 300 { error += 1 }

    // Width > 4096 gets capped
    s2 := f"{'y':>5000}"
    if s2.len() != 4096 { error += 1 }

    error
}

// Verifies that colons inside nested braces/parens in an f-string expression
// are NOT misinterpreted as the start of a format specifier.
test_named_arg_colon() {
    error := 0

    // Colon inside a block expression (brace depth > 0)
    s1 := f"val = {({x: Int := 42; x})}"
    if s1 != "val = 42" { error += 1 }

    // Function call inside f-string (no format spec)
    s2 := f"sum = {({a := 10; b := 20; a + b})}"
    if s2 != "sum = 30" { error += 1 }

    error
}

test_sign() {
    error := 0

    x := 42
    y := -42

    s1 := f"{x:+}"
    if s1 != "+42" { error += 1 }

    s2 := f"{y:+}"
    if s2 != "-42" { error += 1 }

    s3 := f"{x: }"
    if s3 != " 42" { error += 1 }

    s4 := f"{y: }"
    if s4 != "-42" { error += 1 }

    error
}

test_width() {
    error := 0

    x  := 42
    s1 := f"{x:10}"
    if s1 != "        42" { error += 1 }

    s  := "hi"
    s2 := f"{s:10}"
    if s2 != "        hi" { error += 1 }

    error
}

test_zero_pad() {
    error := 0

    x := 42

    s1 := f"{x:05}"
    if s1 != "00042" { error += 1 }

    s2 := f"{x:08x}"
    if s2 != "0000002a" { error += 1 }

    y  := -42
    s3 := f"{y:08}"
    if s3 != "-0000042" { error += 1 }

    error
}

main() {
    error := 0
    error = error + test_align()
    error = error + test_combined()
    error = error + test_fill()
    error = error + test_float_precision()
    error = error + test_float_scientific()
    error = error + test_generic_only()
    error = error + test_int_bases()
    error = error + test_int_decimal()
    error = error + test_int_hex()
    error = error + test_int_min_binary()
    error = error + test_large_width()
    error = error + test_named_arg_colon()
    error = error + test_sign()
    error = error + test_width()
    error = error + test_zero_pad()
    error
}
