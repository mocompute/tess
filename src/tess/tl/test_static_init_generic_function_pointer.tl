#module Ops

// Test: Static initialization of generic structs containing function pointers
// where the generic parameter constrains the function type

double(x: Int) -> Int { x * 2 }
triple(x: Int) -> Int { x * 3 }

// Generic struct with function pointer using generic type in signature
Processor(T) : { process: (T) -> T }

// Static initialization with Int specialization
int_doubler: Processor(Int) := Processor(process = double/1)
int_tripler: Processor(Int) := Processor(process = triple/1)

// Helper to apply processor
apply(p: Processor(a), x: a) -> a {
  p.process(x)
}


#module main

main() {
  error := 0

  // Test 1: Use statically initialized int_doubler
  result1 := Ops.apply(Ops.int_doubler, 5)
  error += if result1 == 10 {0} else {1}

  // Test 2: Use statically initialized int_tripler
  result2 := Ops.apply(Ops.int_tripler, 5)
  error += if result2 == 15 {0} else {1}

  // Test 3: Direct access to static struct field
  result3 := Ops.int_doubler.process(7)
  error += if result3 == 14 {0} else {1}

  // Test 4: Direct access to static struct field
  result4 := Ops.int_tripler.process(4)
  error += if result4 == 12 {0} else {1}

  error
}
