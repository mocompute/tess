#module TestInit

// Global variable initialized to a sentinel value
initialized_value := 0

// Module init function - should run before main()
__init() {
  initialized_value = 42
}

get_value() { initialized_value }

#module main

main() {
  error := 0

  // Check that TestInit.__init() has already run
  error += if TestInit.get_value() == 42 {0} else {1}

  error
}
