#module main

// Runtime failure test: narrowing conversion overflow.
// When compiled with --bounds-check, assigning a value that exceeds
// the target type's range must abort.

main() {
    a: Int := 100000
    b: CShort := a   // CShort max is 32767 — this must abort
    result: CInt := b
    result
}
