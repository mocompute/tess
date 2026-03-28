#module main
#import <Print.tl>

main() {
    error := 0

    // INT_MIN (LLONG_MIN) in binary — exercises the fixed UB path
    // -9223372036854775808 = LLONG_MIN
    // Its binary magnitude is 1 followed by 63 zeros.
    min_val: Int := -9223372036854775807 - 1
    s            := f"{min_val:b}"
    if s != "-1000000000000000000000000000000000000000000000000000000000000000" { error += 1 }

    error
}
