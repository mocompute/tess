// Comparing Option with null must be a type error — Option is not a pointer type.
// Without this check, null silently unifies with Option and generates invalid C.
#module main

main() {
    x: Option[Int] := Some(42)
    if x == null { 1 } else { 0 }
}
