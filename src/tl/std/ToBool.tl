// ToBool — trait for types that can be converted to a Bool representation.
//
// A type satisfying ToBool must provide a unary to_bool function returning Bool.
// Used as a variadic bound: check_all(args: ...ToBool) applies to_bool to each arg.

#module ToBool

ToBool[T]: { to_bool(a: T) -> Bool }


// -- Implementations for builtin type families --

#module Int
to_bool(n) -> Bool { n != 0 }

#module UInt
to_bool(n) -> Bool { n != 0 }

#module Float
to_bool(f) -> Bool { f != 0.0 }

#module Bool
to_bool(b) -> Bool { b }

#module CString
to_bool(cs) -> Int { if cs && cs.[0] {true} else {false} }

#module String
to_bool(s) -> Int { if s.is_empty() {false} else {true} }
