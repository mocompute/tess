#module Array
#import <Alloc.tl>
#import <stdlib.tl>

// The Array struct.
T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

// -- Synopsis --

// Construct an empty array with elements of type a.
empty(a: Type) -> T(a)

// Deallocate array buffer.
free(arr: T(a)) -> Nil

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(arr: Ptr(T(a)), count: Int) -> Nil

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)


// -- Implementation --

empty(a: Type) {
  T { v = null, capacity = 0, size = 0 }
}

free(arr) { c_free(arr.v) }

type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }

reserve(arr: Ptr(T(a)), count: Int) {
  if arr->capacity < count {
    // TODO: assume no failure is possible
    p : Ptr(a) := c_realloc(arr->v, count * type_width_aligned(a))
    arr->v = p
  }
}

with_capacity(a: Type, count: Int) {
  width      := type_width_aligned(a)
  p : Ptr(a) := c_malloc(count * width)
  T { v = p, capacity = count, size = 0 }
}
