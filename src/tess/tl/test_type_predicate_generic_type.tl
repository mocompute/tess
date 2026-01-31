#module main

unwrap_both(opt_or_res, default: T) -> T {
    if opt_or_res :: Option(T) {
        case opt_or_res: Option(T) {
            s: Some { s.v }
            n: None { default }
        }
    } else if opt_or_res :: Result(T, U) {
        case opt_or_res: Result(T, U) {
            o: Ok { o.v }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_("unwrap: invalid type")
    }
}

main() {
    error := 0

    // Test with Option: Some
    opt_some := Some(42)
    error = error + if unwrap_both(opt_some, 0) == 42 { 0 } else { 1 }

    // Test with Option: None
    opt_none: Option(Int) := None()
    error = error + if unwrap_both(opt_none, 99) == 99 { 0 } else { 1 }

    // Test with Result: Ok
    res_ok: Result(Int, Int) := Ok(10)
    error = error + if unwrap_both(res_ok, 0) == 10 { 0 } else { 1 }

    // Test with Result: Err
    res_err: Result(Int, Int) := Err(5)
    error = error + if unwrap_both(res_err, 77) == 77 { 0 } else { 1 }

    error
}
