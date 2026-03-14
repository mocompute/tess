#module main

// Else block has a := binding but does NOT diverge — must be rejected.

main() -> CInt {
    opt: Option[Int] := Some(42)
    i := 0
    while i < 1 {
        s: Some := opt else {
            x := 99
            i += 1
            // missing continue/break/return — not diverging
        }
        i += 1
    }
    ret: CInt := 0
    ret
}
