#module Wrapper

Wrapper[T]: { value: T }

to_bool[T](wrapper: Wrapper[T]) -> Bool { wrapper.value.to_bool() }

#module main
#import <String.tl>


test_int() {
    error := 0
    error += if 0 {1} else {0}
    error += if 1 {0} else {1}
    error += if -1 {0} else {1}
    error
}

test_uint() {
    error := 0
    error += if 0zu {1} else {0}
    error += if 1zu {0} else {1}
    error
}

test_float() {
    error := 0
    error += if 0.0 {1} else {0}
    error += if 1.0 {0} else {1}
    error += if -1.0 {0} else {1}
    error
}

test_bool() {
    error := 0
    error += if false {1} else {0}
    error += if true {0} else {1}
    error
}

test_cstring() {
    error := 0
    error += if c"" {1} else {0}
    error += if null {1} else {0}
    error += if c"hello" {0} else {1}
    error
}

test_string() {
    error := 0
    error += if "" {1} else {0}
    error += if "hello" {0} else {1}
    error
}

test_int_var() {
    error := 0
    zero  := 0
    one   := 1
    neg   := -1
    error += if zero {1} else {0}
    error += if one {0} else {1}
    error += if neg {0} else {1}
    error
}

test_uint_var() {
    error := 0
    zero  := 0zu
    one   := 1zu
    error += if zero {1} else {0}
    error += if one {0} else {1}
    error
}

test_float_var() {
    error := 0
    zero  := 0.0
    one   := 1.0
    neg   := -1.0
    error += if zero {1} else {0}
    error += if one {0} else {1}
    error += if neg {0} else {1}
    error
}

test_bool_var() {
    error := 0
    f     := false
    t     := true
    error += if f {1} else {0}
    error += if t {0} else {1}
    error
}

test_cstring_var() {
    error           := 0
    empty           := c""
    nul: Ptr[CChar] := null
    hello           := c"hello"
    error += if empty {1} else {0}
    error += if nul {1} else {0}
    error += if hello {0} else {1}
    error
}

test_string_var() {
    error := 0
    empty := ""
    hello := "hello"
    error += if empty {1} else {0}
    error += if hello {0} else {1}
    error
}

test_null() {
    if null {1} else {0}
}

test_wrapper() {
    error := 0
    zero  := Wrapper(value = 0)
    one   := Wrapper(value = 1)
    error += if zero {1} else {0}
    error += if one {0} else {1}
    error
}

_get_int    () { 42       }
_get_uint   () { 42zu     }
_get_float  () { 42.0     }
_get_bool   () { true     }
_get_cstring() { c"hello" }
_get_string () { "hello"  }

test_int_call() {
    error := 0
    error += if _get_int() {0} else {1}
    error
}

test_uint_call() {
    error := 0
    error += if _get_uint() {0} else {1}
    error
}

test_float_call() {
    error := 0
    error += if _get_float() {0} else {1}
    error
}

test_bool_call() {
    error := 0
    error += if _get_bool() {0} else {1}
    error
}

test_cstring_call() {
    error := 0
    error += if _get_cstring() {0} else {1}
    error
}

test_string_call() {
    error := 0
    error += if _get_string() {0} else {1}
    error
}

main() {
    error := 0
    error += test_int()
    error += test_uint()
    error += test_float()
    error += test_bool()
    error += test_cstring()
    error += test_string()
    error += test_int_var()
    error += test_uint_var()
    error += test_float_var()
    error += test_bool_var()
    error += test_cstring_var()
    error += test_string_var()
    error += test_null()
    error += test_int_call()
    error += test_uint_call()
    error += test_float_call()
    error += test_bool_call()
    error += test_cstring_call()
    error += test_string_call()
    error += test_wrapper()
    error
}
