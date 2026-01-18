#module main

T(a) = | Some { v: a }
       | None

unwrap(opt: T(Int)): Int {
    case opt: T {
        s: Some { s.v }
        n: None { 0 }
    }
}

main() {
    val := T_Some(v = 42)
    unwrap(val)
}
