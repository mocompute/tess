#module main
#import <fatal.tl>

// Test that _tl_fatal_ compiles and can be used in conditional code
// Note: We don't actually call it since it terminates the program

conditionally_fail(should_fail: Bool) {
  if should_fail {
    _tl_fatal_("This error should never be reached")
  }
  void
}

main() {
  error := 0

  // Call the function but with false to avoid actual termination
  conditionally_fail(false)

  // If we reach here, the test passed (the function compiled correctly)
  error
}
