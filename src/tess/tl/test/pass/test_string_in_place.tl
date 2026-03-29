#module main
#import <String.tl>
#import <cstdio.tl>

// -- replace_byte_in_place --

test_replace_byte_in_place_small() {
    error := 0
    s     := "hello"
    s.replace_byte_in_place('l', 'r')
    if s != "herro" { error += 1 }
    s.free()
    error
}

test_replace_byte_in_place_big() {
    error := 0
    s     := String.copy("this is a big string!!")
    s.replace_byte_in_place('i', 'o')
    if s != "thos os a bog strong!!" { error += 1 }
    s.free()
    error
}

test_replace_byte_in_place_no_match() {
    error := 0
    s     := "hello"
    s.replace_byte_in_place('z', 'x')
    if s != "hello" { error += 1 }
    s.free()
    error
}

// -- to_upper_in_place --

test_to_upper_in_place_small() {
    error := 0
    s     := "hello"
    s.to_upper_in_place()
    if s != "HELLO" { error += 1 }
    s.free()
    error
}

test_to_upper_in_place_big() {
    error := 0
    s     := String.copy("this is a big string!!")
    s.to_upper_in_place()
    if s != "THIS IS A BIG STRING!!" { error += 1 }
    s.free()
    error
}

// -- to_lower_in_place --

test_to_lower_in_place_small() {
    error := 0
    s     := "HELLO"
    s.to_lower_in_place()
    if s != "hello" { error += 1 }
    s.free()
    error
}

test_to_lower_in_place_big() {
    error := 0
    s     := String.copy("THIS IS A BIG STRING!!")
    s.to_lower_in_place()
    if s != "this is a big string!!" { error += 1 }
    s.free()
    error
}


// -- Runner --

_check(name: CString, r: Int) -> Int {
    if r != 0 { c_printf(c"FAIL %s (%d)\n", name, r) }
    r
}

main() {
    error := 0
    error += _check(c"replace_byte_in_place_small",       test_replace_byte_in_place_small())
    error += _check(c"replace_byte_in_place_big",         test_replace_byte_in_place_big())
    error += _check(c"replace_byte_in_place_no_match",    test_replace_byte_in_place_no_match())
    error += _check(c"to_upper_in_place_small",           test_to_upper_in_place_small())
    error += _check(c"to_upper_in_place_big",             test_to_upper_in_place_big())
    error += _check(c"to_lower_in_place_small",           test_to_lower_in_place_small())
    error += _check(c"to_lower_in_place_big",             test_to_lower_in_place_big())
    result: CInt := error
    result
}
