#module main
#import <Array.tl>

// Test break statement in for loop
// Loop should exit early when we find a value >= 5
main() {
  xs := Array.init(0, 10)

  // Initialize array with values 0..9
  i := 0
  for x.& in xs {
    x.* = i
    i = i + 1
  }

  // Find first value >= 5 and exit
  found := 0
  for x in xs {
    if x >= 5 {
      found = x
      break
    }
  }
  // found should be 5
  found - 5
}
