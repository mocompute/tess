#module main
#import <Alloc.tl>

T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

TPtr = Ptr(T(a))


empty(a: Type) -> T(a)
reserve(self: Ptr(T(a)), count: Int) -> Void
with_capacity(a: Type, count: Int) -> T(a)

_grow               (self: TPtr, at_least: Int) -> Void
_is_full            (self: TPtr)                -> Bool
_type_width_aligned (a: Type)                   -> Int


empty(a: Type) {
  T ( v : Ptr(a) = null, capacity = 0, size = 0 )
}

with_capacity(a: Type, count: Int) {
  width      := _type_width_aligned(a)
  p : Ptr(a) := c_malloc(count * width)
  T ( v = p, capacity = count, size = 0 )
}

reserve(self: Ptr(T(a)), count: Int) {
  if self->capacity < count {
    p := c_realloc(self->v, count * _type_width_aligned(a))
    if p == null { _tl_fatal_("oom") }
    self->v = p
  }
  void
}


push(self, x) {
  if _is_full(self) { _grow(self, self->capacity + 1) }

  // Assigning to an array element through a pointer is identical to C.
  self->v[self->size] = x

  // It's a bit wordy compared to C, but there is no ++ operator.
  self->size = self->size + 1
  void
}

_is_full(self) { self->capacity == self->size }

_grow(self, at_least) {
  // This `if` statement demonstrates the use of the return statement. Not that unlike C, the return statement
  // must always state an explicit value to return. `void` may be used to indicate a void return.
  if self->capacity >= at_least { return void }

  count := self->capacity
  if count < 1 { count = 1 }

  // This statement introduces a `while` statement. There is a variation that includes an 'update'
  // expression, similar to that of a C `for` loop, but that is not shown here. `break` and `continue` are
  // also supported.
  while count < at_least { count = count * 2 }

  // Notice that there is no need here to add a `void` expression, since the call to `reserve()` itself
  // returns `void`.
  reserve(self, count)
}

_type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }
