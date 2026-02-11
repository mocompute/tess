#module main

main() {
    error := 0

    // Test unwrap with Option: Some
    opt_some := Some(42)
    error = error + if unwrap(opt_some, 0) == 42 { 0 } else { 1 }

    // Test unwrap with Option: None
    opt_none: Option[Int] := None()
    error = error + if unwrap(opt_none, 99) == 99 { 0 } else { 1 }

    // Test unwrap with Result: Ok
    res_ok: Result(Int, Int) := Ok(10)
    error = error + if unwrap(res_ok, 0) == 10 { 0 } else { 1 }

    // Test unwrap with Result: Err
    res_err: Result(Int, Int) := Err(5)
    error = error + if unwrap(res_err, 77) == 77 { 0 } else { 1 }

    error
}
