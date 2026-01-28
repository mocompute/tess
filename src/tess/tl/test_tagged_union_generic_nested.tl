#module main

Option(a) : | Some { v: a }
            | None

main() {
    nested := Option.Some(v = Option.Some(v = 42))
    0
}
