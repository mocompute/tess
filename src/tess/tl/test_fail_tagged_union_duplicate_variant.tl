#module main

// Two tagged unions with the same variant name should be a compile error
Option: | Some { v: Int }
        | None

Result: | Ok { v: Int }
        | None

main() {
    x := Some(v = 1)
    0
}
