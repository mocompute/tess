#module main

// --- Helpers from test_type_predicate ---

t_int() {
    x := 1
    x :: Int
}

t_float() {
    x := 1.5
    x :: Float
}

t_invalid_int() {
    x := 2.3
    x :: Int
}

t_invalid_float() {
    x := 2
    x :: Float
}

// --- Helpers from test_type_predicate_branch ---

// Generic function that returns a different int depending on the type of x.
// Each call site is specialized, so the predicates resolve at compile time.
type_id(x) {
    if x ::      Int { 1 }
    else if x :: Float { 2 }
    else { 0 }
}

// --- Helpers from test_type_predicate_field ---

Foo: {
    i: Int,
    f: Float,
}

t_field() {
    foo := Foo(i = 1, f = 2.5)
    foo.i :: Int && foo.f :: Float
}

// --- Helpers from test_type_predicate_generic ---

is_int(x) { x :: Int }

// --- Helpers from test_type_predicate_type_arg ---

// Pattern 1: T :: ConcreteType
is_int_type[T]  () { T :: Int   }
is_float_type[T]() { T :: Float }

// Pattern 2: x :: T (should already work, verify it)
matches_param[T](x: T) { x :: T }

// Combined patterns
combined[T](x: T) { T :: Int && x :: T }

// --- Test functions ---

test_type_predicate() {
    error := 0

    error += if t_int() {0} else {1}
    error += if t_float() {0} else {1}

    error += if t_invalid_int() {1} else {0}
    error += if t_invalid_float() {1} else {0}

    error
}

test_type_predicate_branch() {
    error := 0
    error = error + if type_id(42) == 1 { 0 } else { 1 }
    error = error + if type_id(3.14) == 2 { 0 } else { 1 }
    error = error + if type_id(c"hey") == 0 { 0 } else { 1 }
    error
}

test_type_predicate_field() {
    error := 0
    error += if t_field() {0} else {1}
    error
}

test_type_predicate_generic() {
    error := 0

    // Each application of is_int is specialized based on the type of its argument.
    error := if is_int(1) {0} else {1}
    error := if is_int(0.5) {1} else {0}
    error := if is_int(c"abc") {1} else {0}

    error
}

test_type_predicate_type_arg() {
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

main() {
    error := 0
    error += test_type_predicate()
    error += test_type_predicate_branch()
    error += test_type_predicate_field()
    error += test_type_predicate_generic()
    error += test_type_predicate_type_arg()
    error
}
