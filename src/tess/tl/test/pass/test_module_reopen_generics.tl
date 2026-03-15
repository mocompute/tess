// Test generic types and functions across module reopenings.
#module M

Box[T]: { v: T }

make_box[T](x: T) -> Box[T] { Box(v = x) }

#module M.Helper
helper() -> CInt { 1 }

#module M

// Reopened: add generic function that uses type from first opening
wrap[T](x: T) -> Box[T] { make_box(x) }

// Reopened: access generic type from original section
get_box_int(b: Box[CInt]) -> CInt { b.v }

// Cross-opening call
wrap_with_helper(x: CInt) -> Box[CInt] { wrap(x + M.Helper.helper()) }

#module main
main() {
    error: CInt := 0

    // Specialize with Int
    bi := M.wrap(42)
    if M.get_box_int(bi) != 42 { error += 1 }

    // Specialize with Bool
    bb := M.wrap(true)
    if !bb.v { error += 1 }

    // Cross-opening function call
    bh := M.wrap_with_helper(10)
    if bh.v != 11 { error += 1 }

    error
}
