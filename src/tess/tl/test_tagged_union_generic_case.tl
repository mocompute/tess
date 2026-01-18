#module main

T(a) = | Some { v: a }
       | None

main() {
    good := T_Some(v = 123)

    bad := case good: T {
        s: Some { s.v }
        n: None { 0 }
    }
    bad - 123
}
