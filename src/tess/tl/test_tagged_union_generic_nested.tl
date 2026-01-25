#module main

Option(a) : | Some { v: a }
            | None

main() {
    nested := Option_Some(v = Option_Some(v = 42))
    0
}
