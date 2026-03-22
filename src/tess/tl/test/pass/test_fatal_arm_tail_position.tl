// Tests that if-else and case expressions with a _tl_fatal_/c_exit diverging
// arm correctly return their value when used as the last expression in a
// function (tail position). Companion to test_when_last_expr_return.tl which
// covers the when-expression case.

#module main
#import <cstdlib.tl>

// if-else in tail position, else arm diverges
if_else_fatal(x) {
    if x > 0 { x * 2 }
    else {
        c_exit(1)
        _tl_fatal_(c"unreachable")
    }
}

// if-else in tail position, then arm diverges
if_else_fatal_reverse(x) {
    if x < 0 {
        c_exit(1)
        _tl_fatal_(c"unreachable")
    }
    else { x + 1 }
}

// case in tail position, else arm diverges
case_fatal(x) {
    case x {
        1 { 10 }
        2 { 20 }
        else {
            c_exit(1)
            _tl_fatal_(c"unreachable")
        }
    }
}

main() {
    error := 0
    if if_else_fatal(5) != 10 { error += 1 }
    if if_else_fatal_reverse(5) != 6 { error += 1 }
    if case_fatal(1) != 10 { error += 1 }
    if case_fatal(2) != 20 { error += 1 }
    error
}
