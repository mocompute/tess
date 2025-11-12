#module Array
#import <Alloc.tl>
#import <stdlib.tl>

// The Array struct. Conventionally, when a module primarily provides functions which operate on a single
// struct, that struct is called `T`. This avoids redundant module-qualified struct names, like
// `Array.Array`.
T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

// -- Synopsis --

// Construct an empty array with elements of type a.
empty(a: Type) -> T(a)

// Deallocate array buffer.
free(self: Ptr(T(a))) -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: Ptr(T(a)), count: Int) -> Void

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)

// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: Ptr(T(a)), x: a) -> Void



// -- Implementation --

empty(a: Type) {
  T ( v = null, capacity = 0, size = 0 )
}

free(self) {
  c_free(self->v)
  self->v = null
  void
}

_type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }

reserve(self: Ptr(T(a)), count: Int) {
  if self->capacity < count {
    // TODO: assume no failure is possible
    p : Ptr(a) := c_realloc(self->v, count * _type_width_aligned(a))
    self->v = p
  }
  void
}

with_capacity(a: Type, count: Int) {
  width      := _type_width_aligned(a)
  p : Ptr(a) := c_malloc(count * width)
  T ( v = p, capacity = count, size = 0 )
}

_grow(self, at_least) {
  if self->capacity >= at_least { return () }
  count := self->capacity
  if count < 1 { count = 1 }
  while count < at_least { count = count * 2 }
  reserve(self, count)
}

_is_full(self) { self->capacity == self->size }

push(self, x) {
  if _is_full(self) { _grow(self, self->capacity + 1) }
  self->v[self->size] = x
  self->size = self->size + 1
  void
}
