#module main

// Test continue statement in while loop
// Sum only even numbers from 0 to 9
main() {
  i := 0
  sum := 0
  while i < 10 {
    i = i + 1
    // Skip odd numbers
    if i % 2 == 1 {
      continue
    }
    sum = sum + i
  }
  // sum should be 2 + 4 + 6 + 8 + 10 = 30
  sum - 30
}
