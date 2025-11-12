#module Array
#import <Alloc.tl>
#import <stdlib.tl>
#import <stdio.tl> // for fatal()

// The Array struct. Conventionally, when a module primarily provides functions which operate on a single
// struct, that struct is called `T`. This avoids redundant module-qualified struct names, like
// `Array.Array`.
T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

TPtr = Ptr(T(a))

// -- Synopsis --

// Construct an empty array with elements of type a.
empty(a: Type) -> T(a)

// Deallocate array buffer.
free(self: TPtr) -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: TPtr, count: Int) -> Void

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)

// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: TPtr, x: a) -> Void



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
  // Note cannot use TPtr in param because `a` typevar is used in body of fun
  if self->capacity < count {
    p : Ptr(a) := c_realloc(self->v, count * _type_width_aligned(a))
    if p == null { _tl_fatal_("oom") }
    self->v = p
  }
  void
}

with_capacity(a: Type, count: Int) {
  width      := _type_width_aligned(a)
  p : Ptr(a) := c_malloc(count * width)
  T ( v = p, capacity = count, size = 0 )
}

// -- Forward declarations are required if functions are referenced before their definitions.
_grow      (self: TPtr, at_least: Int) -> Void
_is_full   (self: TPtr)                -> Bool

push(self, x) {
  if _is_full(self) { _grow(self, self->capacity + 1) }
  self->v[self->size] = x
  self->size = self->size + 1
  void
}

_grow(self, at_least) {
  if self->capacity >= at_least { return () }
  count := self->capacity
  if count < 1 { count = 1 }
  while count < at_least { count = count * 2 }
  reserve(self, count)
}

_is_full(self) { self->capacity == self->size }
