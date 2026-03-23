#module main

// Bug: Err(42) with a literal in a function returning Result[Void, CInt] fails
// to specialize — the Result type stays generic because T (the success type)
// is only known from the function's return type, not from the Err argument.
//
// Workaround: bind the literal to a typed variable first:
//   code: CInt := 42
//   Err(code)
//
// Or use a helper: _err(code: CInt) -> Result[Void, CInt] { Err(code) }

test() -> Result[Void, CInt] {
    Err(42)
}

main() {
    when test() {
        ok:  Ok { 1 }
        err: Err {
            if err.v != 42 { return 1 }
            0
        }
    }
}
