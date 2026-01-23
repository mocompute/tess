#module main
#import <stdlib.tl>

// Define a struct with multiple fields
Buffer : { data: Ptr(CChar), size: Int, capacity: Int }

// Another test struct
Point : { x: Int, y: Int, z: Int }

main() {
  error := 0

  // Test creating struct with uninitialized fields
  buf := Buffer(
    data = c_malloc(1024),
    size = void,           // Left uninitialized
    capacity = 1024
  )

  // Initialize the uninitialized field later
  buf.size = 0

  // Verify all fields have expected values
  error += if buf.size == 0 {0} else {1}
  error += if buf.capacity == 1024 {0} else {1}

  // Test modifying the previously uninitialized field
  buf.size = 100
  error += if buf.size == 100 {0} else {1}

  // Test with multiple uninitialized fields
  p := Point(x = void, y = 10, z = void)
  p.x = 5
  p.z = 15

  error += if p.x == 5 {0} else {1}
  error += if p.y == 10 {0} else {1}
  error += if p.z == 15 {0} else {1}

  c_free(buf.data)
  error
}
