// Tests that a `when` expression as the last expression in a function
// correctly returns its value when one arm diverges (_tl_fatal_).
// Currently the transpiler generates `tl_resN;` (dead statement)
// instead of `return tl_resN;` in this case.

#module main
#import <cstdlib.tl>

unwrap_or_die(opt: Option[Int]) -> Int {
    when opt {
        s: Some { s.v }
        n: None {
            c_exit(1)
            _tl_fatal_("unreachable")
        }
    }
}

main() {
    error := 0
    if unwrap_or_die(Some(42)) != 42 { error += 1 }
    if unwrap_or_die(Some(7)) != 7 { error += 1 }
    error
}
