// ToInt — trait for types that can be converted to an Int representation.
//
// A type satisfying ToInt must provide a unary to_int function returning Int.
// Used as a variadic bound: add_all(args: ...ToInt) applies to_int to each arg.

#module ToInt

ToInt[T]: { to_int(a: T) -> Int }


// -- Implementations for builtin type families --

#module Int
to_int(n) -> Int { n }

#module UInt
to_int(n) -> Int { res: Int := n, res }

#module Float
to_int(f) -> Int { res: Int := f, res }

#module Bool
to_int(b) -> Int { if b {1} else {0} }


#module CString
to_int(cs) -> Int { if cs && cs.[0] {1} else {0} }

#module String
to_int(s) -> Int { if s.is_empty() {0} else {1} }
