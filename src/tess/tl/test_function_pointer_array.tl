#module main
#import <stdlib.tl>

// Test: Array of function pointers with iteration and invocation

double(x: Int) -> Int { x * 2 }
triple(x: Int) -> Int { x * 3 }
negate(x: Int) -> Int { 0 - x }
add_one(x: Int) -> Int { x + 1 }

// Struct to hold function pointer (workaround for CArray type parameter)
FnHolder : { fn: (Int) -> Int }

main() {
  error := 0

  // Test 1: Array of structs containing function pointers
  fns := CArray(FnHolder, 4)
  fns[0] = FnHolder(fn = double/1)
  fns[1] = FnHolder(fn = triple/1)
  fns[2] = FnHolder(fn = negate/1)
  fns[3] = FnHolder(fn = add_one/1)

  // Test 2: Invoke first function (double)
  result1 := fns[0].fn(5)
  error += if result1 == 10 {0} else {1}

  // Test 3: Invoke second function (triple)
  result2 := fns[1].fn(5)
  error += if result2 == 15 {0} else {1}

  // Test 4: Invoke third function (negate)
  result3 := fns[2].fn(7)
  error += if result3 == -7 {0} else {1}

  // Test 5: Invoke fourth function (add_one)
  result4 := fns[3].fn(10)
  error += if result4 == 11 {0} else {1}

  // Test 6: Iterate and sum results for input 2
  sum := 0
  i := 0
  while (i < 4) {
    holder := fns[i]
    sum += holder.fn(2)
    i += 1
  }
  // double(2)=4, triple(2)=6, negate(2)=-2, add_one(2)=3 => 4+6-2+3=11
  error += if sum == 11 {0} else {1}

  // Test 7: Decay array to pointer and invoke
  ptr := fns
  result7 := ptr[1].fn(4)  // triple(4) = 12
  error += if result7 == 12 {0} else {1}

  error
}
