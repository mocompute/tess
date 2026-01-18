#module main

T(a) = | Some { v: a }
       | None

unwrap(opt: T(a), default: a) -> a {
    case opt: T {
        s: Some { s.v }
        n: None { default }
    }
}

main() {
    val := T_Some(v = 42)
    unwrap(val, 0) - 42
}
