// The Array module. Tutorial style comments are included for this module.
//
// The `#` symbol introduces a hash directive, similar to the C preprocessor, but TL does not include a
// C preprocessor.
//
// All code must reside in a module. Module names provide a namespace and are used while parsing for
// deduplication: the first use of a module name takes precedence over later uses. This is similar to the C
// practice of using include guards.
//
// Users of the Array module must `#import` it (see below) and must access its identifers by prepending
// `Array.` before the identifier: e.g. `Array.reserve(arr, 16)` will call the reserve function in this
// module. TL provides no way to avoid the use of the module prefix, except for the identifiers declared in
// the `builtin` module, which must be used with no prefix. Identifiers that are not qualified are
// automatically scoped to the current module. Modules may not be nested: so `Foo.Bar.baz()` is not
// permitted.
#module Array

// The `#import` directive includes the specified file and makes any modules and identifiers declared in the
// file available in the current scope (still namespaced, as described above). The search paths are defined
// using `-I` options on the compiler command line.
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
// is required, however, for the program to successfully type check. For example, `c_malloc` is declared
// like this: `c_malloc(size: CSize) -> Ptr(T)` This indicates the function takes a single C `size_t`
// argument, and returns a pointer to an arbitrary type `T`. See `Array.reserve()` for the syntax used to
// cast the return value from `c_malloc` to its appropriate type.
#import <stdlib.tl>
#import <stdio.tl> // for fatal()

// The Array struct. Conventionally, when a module primarily provides functions which operate on a single
// struct, that struct is called `T`. This avoids redundant module-qualified struct names, like
// `Array.Array`.
//
// The syntax `T(a)` looks like a function call. In this context, at the top level of the file and followed
// by a `:`, it introduces a type definition. The `(a)` portion indicates that this is a generic type, with
// `a` being used as a type variable. Any legal identifier may be used as a type variable. Conventionally,
// either lowercase single letters such as `a` and `b` are used, or uppercase single letters such as `T` and
// `U` are used. In this case, since `T` is used to actually name the main struct type of this module, a
// lowercase `a` is more appropriate.
//
// `Ptr(a)` is a builtin type, indicating a pointer to an arbitrary type `a`. In this case, the `a` is the
// generic type variable declared by `T(a)`.
//
// Int is a builtin type, corresponding to C `long long`, which is guaranteed by the C standard to be a
// signed integer at least 64 bits long.
T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

// This syntax declares a type alias. `TPtr` may be used in place of `Ptr(T(a))` in all contexts. However,
// since this is a generic type (using the type variable `a`), note that use of `TPtr` does NOT provide
// access to `a`. To understand the consequence of this, refer to the definition of `reserve()` below:
// notice that it must use `Ptr(T(a))` because the body of the function needs to refer to the `a` type
// variable explicitly.
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

// The declaration `(a: Type, ...)` below demonstrates the use of a generic type argument. In C++, the
// corresponding syntax would be `template<typename T> with_capacity<T>(...)` but in TL, type arguments are
// declared alongside other formal parameters. Conventionally, they appear first. `Type` is a builtin type
// constructor which takes a single argument being the identifier or type constructor of a type, but
// conversion between callsite usage and consumer usage is handled automatically.

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)



// -- Implementation --

// -- Forward declarations are required if functions are referenced before their definitions.
// Conventionally, a preceding `_` indicates a function intended to be private. However, all toplevel
// identifiers may be accessed from any scope. For example, `Array._grow` can be used to access the
// function, even though it is indicated to be private.
_grow               (self: TPtr, at_least: Int) -> Void
_is_full            (self: TPtr)                -> Bool
_type_width_aligned (a: Type)                   -> Int

// --

empty(a: Type) {
  // The syntax `T( ... )` indicates a type constructor. Here, the type `T` is defined earlier in the file.
  // A type constructor must include every field; it is not permitted to omit any field. The syntax `field =
  // value` is used exclusively for type constructors. Although the syntax is similar to a function call
  // with named arguments, ordinary TL functions do NOT support named arguments.
  //
  // The field `v` includes a type annotation, which is required here in order to make use of the type
  // argument in the function parameters, and to ensure the returned struct has a non-generic (concrete)
  // type.
  T ( v : Ptr(a) = null, capacity = 0, size = 0 )
}

free(self) {
  // `c_free` is transpiled to `free` and access the C stdlib.h `free` function. `->` is used in this
  // context as the member access through pointer operator, identical to C.
  c_free(self->v)

  // The field `v` is defined with the type `Ptr(a)`. `null` is compatible with any Ptr type.
  self->v = null

  // All functions must return a value, and the value of the final expression in the body of the function is
  // used as the return value. For functions which return no value, they must return the pseudo-value
  // `void`. If this `void` expression was omitted, the return value of this function would be `null`, with
  // the type `Ptr(a)`, inferred from the type of the `v` field of the `T(a)` struct. Since the type
  // `Ptr(a)` is not compatible with the type `Void`, this would generate a type error.
  void
}

// The first formal parameter of this function is given the annotation `Type`. This is a special type which
// indicates a literal type argument, such as `Int` or `Ptr(T)`.
with_capacity(a: Type, count: Int) {

  // The `:=` operator indicates a variable declaration. Variables must be declared at the point of their
  // first use. Subsequently, a new value may be assigned to the variable using the `=` operator.
  //
  // This statement also introduces the use of a type variable as a type argument. Note that type arguments
  // co-exist with other formal parameters. Conventionally, they appear first.
  width      := _type_width_aligned(a)

  // This statement demonstrates the use of a type annotation on a variable being declared. In this case,
  // since `c_malloc` is declared as returning a generic type, the annotation effectively casts the return
  // value to the expected type of the variable `p`. In this case, the annotation is NOT needed, because the
  // type of `p` can be inferred by its point of use in the `v = p` assignment of the type constructor on
  // the next line.
  p : Ptr(a) := c_malloc(count * width)
  T ( v = p, capacity = count, size = 0 )
}

reserve(self: Ptr(T(a)), count: Int) {
  // Note cannot use TPtr in parameter because `a` typevar is used in body of function
  if self->capacity < count {
    p := c_realloc(self->v, count * _type_width_aligned(a))

    // _tl_fatal_ is a TL intrinsic that prints a message to stderr and exits with status 1.
    if p == null { _tl_fatal_("oom") }
    self->v = p
  }
  void
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
  // This `if` statement demonstrates the use of the return statement. Not that unlike C, the return statement
  // must always state an explicit value to return. `void` may be used to indicate a void return.
  if self->capacity >= at_least { return void }

  count := self->capacity
  if count < 1 { count = 1 }

  // This statement introduces a `while` statement. There is a variation that includes an 'update'
  // expression, similar to that of a C for loop, but that is not shown here.
  while count < at_least { count = count * 2 }

  // Notice that there is no need here to add a `void` expression, since the call to `reserve()` itself returns `void`.
  reserve(self, count)
}


_type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }
