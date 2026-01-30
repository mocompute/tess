#module main

Option(a): | Some { v: a }
           | None

main() {
    nested := Some(Some(42))
    0
}
