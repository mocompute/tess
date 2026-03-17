#module main

// Pattern 1: T :: ConcreteType
is_int_type[T]  () { T :: Int   }
is_float_type[T]() { T :: Float }

// Pattern 2: x :: T (should already work, verify it)
matches_param[T](x: T) { x :: T }

// Combined patterns
combined[T](x: T) { T :: Int && x :: T }

main() {
    error := 0

    // T :: Int where T = Int (true)
    error += if is_int_type[Int]() { 0 } else { 1 }

    // T :: Int where T = Float (false)
    error += if is_int_type[Float]() { 1 } else { 0 }

    // T :: Float where T = Float (true)
    error += if is_float_type[Float]() { 0 } else { 1 }

    // x :: T where x: Int, T = Int (true)
    error += if matches_param[Int](42) { 0 } else { 1 }

    // Combined: T :: Int && x :: T
    error += if combined[Int](42) { 0 } else { 1 }

    error
}
