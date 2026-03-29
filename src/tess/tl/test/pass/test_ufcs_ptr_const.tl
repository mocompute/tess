// Test: UFCS dispatch on Ptr[Const[T]] parameters.
//
// When a function parameter is typed Ptr[Const[T]], UFCS method calls
// on that parameter should resolve to functions taking Ptr[Const[T]].
// Currently the compiler fails to find the method, requiring explicit
// Module.function(arg) calls instead.
//
// See: CommandLine.tl _parse_short() FIXME comments.
#import <String.tl>
#module main

_takes_ptr_const(s: Ptr[Const[String]]) -> CInt {
    // These should work via UFCS but currently don't:
    n := s.len()
    c := s.copy(0, n)
    if c.len() != n { return 1 }
    0
}

main() -> CInt {
    greeting := "hello"
    _takes_ptr_const(greeting.&)
}
