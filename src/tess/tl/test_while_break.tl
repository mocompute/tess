#module main

// Test break statement in while loop
// Loop should exit early when i reaches 5
main() {
  i := 0
  while i < 10 {
    if i == 5 {
      break
    }
    i = i + 1
  }
  // i should be 5 (loop exited when i == 5)
  i - 5
}
