#module MyOption

T[a]: | MySome { v: a }
      | MyNone

#module main

_test_match_path() {
    val       := MyOption.MySome(42)
    s: MySome := val else { return 1 }
    if s.v != 42 { return 1 }
    0
}

_no_match_helper() {
    val: MyOption.T[CInt] := MyOption.MyNone
    s:   MySome             := val else { 99 }
    s.v + 1
}

_test_no_match_fallback() {
    if _no_match_helper() != 99 { return 1 }
    0
}

_match_helper() {
    val       := MyOption.MySome(10)
    s: MySome := val else { 99 }
    s.v + 1
}

_test_match_continuation() {
    if _match_helper() != 11 { return 1 }
    0
}

_test_diverging_still_works() {
    val: MyOption.T[CInt] := MyOption.MyNone
    s:   MySome             := val else { return 0 }
    1
}

main() {
    r := _test_match_path()
    r = r + _test_no_match_fallback()
    r = r + _test_match_continuation()
    r = r + _test_diverging_still_works()
    r
}
