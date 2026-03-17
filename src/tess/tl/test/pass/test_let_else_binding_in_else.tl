#module main

// Bug: a := binding inside a let-else's else block causes a syntax error.
//
// The else block of let-else must diverge (return/break/continue), but
// adding a := binding before the diverging statement triggers
// "expected_value" at the enclosing while loop.
//
// Workaround: avoid := bindings in let-else else blocks; move the
// computation before the let-else, or use when instead.

main() -> CInt {
    opt: Option[Int] := Some(42)
    i                := 0
    while i < 1 {
        s: Some := opt else {
            x := 99                     // <-- this := causes the parse error
            i += 1
            continue
        }
        i += 1
    }
    ret: CInt := 0
    ret
}
