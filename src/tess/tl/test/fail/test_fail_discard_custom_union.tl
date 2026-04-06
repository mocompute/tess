#module main

// User-defined two-variant tagged union — should also be subject to discard detection
Outcome: | Success { value: Int }
         | Failure { code: Int }

do_something() -> Outcome {
    Success(42)
}

main() {
    do_something()                      // discarded Outcome (two-variant union) — must fail
    0
}
