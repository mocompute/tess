// The Array module. Tutorial style comments are included for this module.
//
// The `#` symbol introduces a hash directive, similar to the C preprocessor, but TL does not include a
// preprocessor.
//
// All code must reside in a module. Module names provide a namespace and are used while parsing for
// deduplication: the first use of a module name takes precedence over later uses. This is similar to the C
// practice of using include guards.
//
// Users of the Array module must `#import` it (see below) and must access its identifers by prepending
// `Array.` before the identifier: e.g. `Array.reserve(arr, 16)` will call the reserve function in this
// module. TL provides no way to avoid the use of the module prefix, except for the identifiers declared in
// the `builtin` module, which must be used with no prefix.
#module Array

// The `#import` directive includes the specified file and makes any modules and identifiers declared in the
// file available in the current scope (still namespaced, as described above). The search path is defined
// using the `-I` option(s) on the compiler command line.
//
// Any TL file may define one or more modules. While conventionally a single file defines a single module,
// this is not required. However, multiple files may NOT be used to declare identifiers for the same module.
// In other words, a module must be fully defined in a single file.
#import <Alloc.tl>

// Certain common C include files are wrapped by TL files which provide type information for the C
// functions. Conventionally, it is preferred to `#import` the corresponding TL file when using a standard C
// include.
//
// The directive `#include` is also supported (see the contents of <stdlib.tl> for example). `#include`
// simply includes the given C file in the transpiled output. All `#include` directives are hoisted to the
// top of the transpiled output file.
//
// Any C function may be called directly, using the prefix `c_`, e.g. `c_malloc(1024)`. TL type information
// is required, however, for the program to compile. For example, `c_malloc` is declared like this:
// `c_malloc(size: CSize) -> Ptr(T)` This indicates the function takes a single C `size_t` argument, and
// returns a pointer to an arbitrary type `T`. See `Array.reserve()` for the syntax used to cast the return
// value from `c_malloc` to its appropriate type.
#import <stdlib.tl>
#import <stdio.tl> // for fatal()

// The Array struct. Conventionally, when a module primarily provides functions which operate on a single
// struct, that struct is called `T`. This avoids redundant module-qualified struct names, like
// `Array.Array`.
//
// The syntax `T(a)` looks like a function call. In this context, at the top level of the file and followed
// by a `:`, it introduces a type definition. The `(a)` portion indicates that this is a generic type, with
// `a` being used as a type variable. Any legal identifier may be used.
//
// `Ptr(a)` is a builtin type, indicating a pointer to an arbitrary type `a`. In this case, the `a` is the
// generic type variable declared by `T(a)`.
//
// Int is a builtin type, corresponding to C `long long`, which is guaranteed by the standard to be a signed
// integer at least 64 bits long.
T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

// This syntax declares a type alias. `TPtr` may be used in place of `Ptr(T(a))` in all contexts. However,
// since this is a generic type (using the type variable `a`), note that use of `TPtr` does NOT provide
// access to `a`. Refer to the definition of `reserve()` below: notice that it must use `Ptr(T(a))` because
// the body of the function needs to refer to the `a` type variable explicitly.
TPtr = Ptr(T(a))

// -- Synopsis --

// This is an optional section which introduces the public API of this module and their type definitions. It
// demonstrates the syntax used for forward declarations. Functions (and identifiers in general) must be
// declared before their use.

// Construct an empty array with elements of type a.
empty(a: Type) -> T(a)

// Deallocate array buffer.
free(self: TPtr) -> Void

// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: TPtr, x: a) -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: TPtr, count: Int) -> Void

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)




// -- Implementation --

// -- Forward declarations are required if functions are referenced before their definitions.
_grow               (self: TPtr, at_least: Int) -> Void
_is_full            (self: TPtr)                -> Bool
_type_width_aligned (a: Type)                   -> Int

// --

empty(a: Type) {
  T ( v = null, capacity = 0, size = 0 )
}

free(self) {
  c_free(self->v)
  self->v = null
  void
}

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

push(self, x) {
  if _is_full(self) { _grow(self, self->capacity + 1) }
  self->v[self->size] = x
  self->size = self->size + 1
  void
}

// -- Private functions --

_is_full(self) { self->capacity == self->size }

_grow(self, at_least) {
  if self->capacity >= at_least { return () }
  count := self->capacity
  if count < 1 { count = 1 }
  while count < at_least { count = count * 2 }
  reserve(self, count)
}


_type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }
