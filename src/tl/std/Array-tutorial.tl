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
// file available in the current scope (still namespaced, as described above).
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
#import <stdlib.tl>
#import <stdio.tl>
#import <fatal.tl>
#import <string.tl>

// The Array struct.
//
// The syntax `Array(T)` looks like a function call. In this context, at the top level of the file and
// followed by a `:`, it introduces a type definition. The `(T)` portion indicates that this is a generic
// type, with `T` being used as a type variable. Any legal identifier may be used as a type variable.
// Conventionally, either lowercase single letters such as `a` and `b` are used, or uppercase single letters
// such as `T` and `U` are used.
//
// `Ptr[T]` is a builtin type, indicating a pointer to an arbitrary type `T`. In this case, the `T` is the
// generic type variable declared by `Array(T)`.
//
// Int is a builtin type, corresponding to C `long long`, which is guaranteed by the C standard to be a
// signed integer at least 64 bits long.
Array(T): {
    v:        Ptr[T],
    size:     Int,
    capacity: Int,
}


// -- Synopsis --

// This is an optional section which introduces the public API of this module and their type definitions. It
// demonstrates the syntax used for forward declarations. Forward declarations for functions are NOT
// required, but they can provide useful documentation.

// The declaration `(T: Type, ...)` below demonstrates the use of a generic type argument. In C++, the
// corresponding syntax would be `template<typename T> with_capacity<T>(...)` but in TL, type arguments are
// declared alongside other formal parameters. Conventionally, they appear first. `Type` is a builtin type
// constructor which takes a single argument being the identifier or type constructor of a type, but
// conversion between callsite usage and consumer usage is handled automatically.

// All allocating functions take a Ptr[Allocator] argument as their first or second argument. Variants exist
// with no allocator argument: these use the default managed allocator (`Alloc.context.managed`).

// Construct an empty array with elements of type T.
empty(T: Type) -> Array(T)

// Construct an array with count elements of type T, each initialised to val.
init(alloc: Ptr[Allocator], val: T, count: Int) -> Array(T)
init(val: T, count: Int)                        -> Array(T)

// Allocate an empty array with elements of type T and an initial capacity of count.
with_capacity(alloc: Ptr[Allocator], T: Type, count: Int) -> Array(T)
with_capacity(T: Type, count: Int)                        -> Array(T)

// Allocate an empty array with elements of type T and an initial capacity of count, and set the size to the
// capacity, leaving the contents undefined.
with_capacity_undefined(alloc: Ptr[Allocator], T: Type, count: Int) -> Array(T)
with_capacity_undefined(T: Type, count: Int)                        -> Array(T)


// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: Ptr[Array(T]), alloc: Ptr[Allocator], x: T) -> Void
push(self: Ptr[Array(T]), x: T)                        -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: Ptr[Array(T]), alloc: Ptr[Allocator], count: Int) -> Void
reserve(self: Ptr[Array(T]), count: Int)                        -> Void

// Deallocate array buffer.
free(self: Ptr[Array], alloc: Ptr[Allocator]) -> Void
free(self: Ptr[Array])                        -> Void


// -- Implementation --

empty(T: Type) {
    // The syntax `Array( ... )` below indicates a type constructor. Here, the type `Array` is defined
    // earlier in the file. A type constructor must include every field; it is not permitted to omit any
    // field. The syntax `field = value` is used exclusively for type constructors. Although the syntax is
    // similar to a function call with named arguments, ordinary TL functions do NOT support named arguments.
    // This helps visually distinguish function calls from type constructors, and is an opinionated language
    // design choice to exclude parameter names from the formal API of TL libraries.
    //
    // The field `v` includes a type annotation, which is required here to ensure the compiler knows what
    // concrete type of `Array(T)` to specialize. It determines the specialization by examining the type of
    // the arguments provided to the generic `Array` type constructor.
    Array ( v: Ptr[T] = null, capacity = 0, size = 0 )
}

init(alloc, val: T, count) {
    // Note that `T` in the annotation of the `val` parameter is a type argument. It can be supplied as an
    // argument to `with_capacity`, which is expecting a type argument. Type arguments to a function expecting
    // a type argument may be type literals, such as `Int`, or another type argument.
    arr := with_capacity(alloc, T, count)
    arr.size             = count
    for p.& in arr { p.* = val }
    arr
}

// Default-allocator overload. Functions that allocate memory accept an explicit allocator argument. For
// convenience, an overload with no allocator argument is provided which uses the default managed allocator
// from `Alloc.context.managed`. This pattern is used throughout the Array module.
init(val: T, count: Int) {
    init(Alloc.context.managed, val, count)
}

free(self, alloc) {
    // The allocator interface provides free, malloc, and realloc functions. Instead of calling C stdlib
    // functions directly, we use the allocator's methods. This allows users to supply custom allocators.
    alloc->free(alloc, self->v)

    // The field `v` is defined with the type `Ptr[T]`. `null` is compatible with any Ptr type, similar to C
    // NULL or nullptr.
    self->v = null
}

free(self) {
    free(self, Alloc.context.managed)
}

// The first formal parameter of this function is given the annotation `Type`. This is a special type which
// indicates a literal type argument, such as `Int` or `Ptr[T]`.
with_capacity(alloc, T: Type, count) {

    // The `:=` operator indicates a variable declaration. Variables must be declared at the point of their
    // first use. Subsequently, a new value may be assigned to the variable using the `=` operator.
    //
    // This statement also introduces the use of a type variable (`T`) as a type argument to another function
    // call.
    width := _type_width_aligned(T)

    // This statement demonstrates the use of a type annotation on a variable being declared. In this case,
    // since `alloc->malloc` returns a generic type, the annotation effectively casts the return value to the
    // expected type of the variable `p`. In this case, the annotation is NOT needed, because the type of `p`
    // can be inferred by its point of use in the `v = p` assignment of the type constructor on the next
    // line.
    p: Ptr[T] := alloc->malloc(alloc, count * width)
    Array ( v = p, capacity = count, size = 0 )
}

with_capacity(T: Type, count: Int) {
    with_capacity(Alloc.context.managed, T, count)
}

// The type annotations can be left off, and the previous declaration at the top of the file will be used.
with_capacity_undefined(alloc, T, count) {
    result := with_capacity(alloc, T, count)
    result.size = count
    result
}

with_capacity_undefined(T, count) {
    with_capacity_undefined(Alloc.context.managed, T, count)
}


reserve(self: Ptr[Array(T]), alloc, count: Int) {
    // This is an if statement. Round braces are optional around the condition.
    if self->capacity < count {
        // The `: Ptr[T]` annotation is required here, so the compiler knows what concrete type to assign to `p`
        p: Ptr[T] := alloc->realloc(alloc, self->v, count * _type_width_aligned(T))

        // _tl_fatal_ is a TL intrinsic that prints a message to stderr and exits with status 1.
        if p == null { _tl_fatal_(c"oom") }
        self->v        = p
        self->capacity = count
    }
}

reserve(self: Ptr[Array(T]), count: Int) {
    reserve(self, Alloc.context.managed, count)
}


push(self, alloc, x) {
    if _is_full(self) { _grow(self, alloc, self->capacity + 1) }

    // Assigning to an array element through a pointer is identical to C.
    self->v.(self->size) = x

    // It's a bit wordy compared to C, but there are no ++ operators or += operators.
    self->size = self->size + 1
}

push(self, x) {
    push(self, Alloc.context.managed, x)
}

// -- Iterator interface --

// The iterator struct. This is used to provide a public iterator interface over an Array using the `for`
// statement.
Iter(T): {
    v:     Ptr[T],
    size:  Int,
    index: Int,
}

// The required iterator functions. They are called automatically by the compiler-generated code which
// implements the `for` statement. See the bottom of this file for another iterator module, Array.Indexed.
// See also parser.c `a_for_statement` for documentation of how a for loop desugars into calls to these
// functions.
iter_init   (arr: Ptr[Array(T])) -> Iter(T)
iter_ptr    (iter: Ptr[Iter(T])) -> Ptr[T]
iter_value  (iter: Ptr[Iter(T])) -> T
iter_cond   (iter: Ptr[Iter(T])) -> Bool
iter_update (iter: Ptr[Iter(T])) -> Void
iter_deinit (iter: Ptr[Iter(T])) -> Void

iter_init  (arr)  { Iter ( v = arr->v, size = arr->size, index = 0 ) }
iter_ptr   (iter) { iter->v.(iter->index).&                           }
iter_value (iter) { iter->v.(iter->index)                             }
iter_cond  (iter) { iter->index < iter->size                         }
iter_update(iter) { iter->index = iter->index + 1                    }
iter_deinit(iter) {                                                  }


// -- Private functions --

// Conventionally, a preceding `_` indicates a function intended to be private. However, all toplevel
// identifiers may be accessed from any scope. For example, `Array._grow` can be used to access the
// function, even though it is indicated to be private.

// Forward declarations are NOT required, but they can be useful for documentation.

_grow               (self: Ptr[Array(T]), alloc: Ptr[Allocator], at_least: Int) -> Void
_is_full            (self: Ptr[Array(T]))                                       -> Bool
_type_width_aligned (T: Type)                                                   -> Int

// --


// The type annotation above carries forward to the function definition.
_is_full(self) { self->capacity == self->size }

_grow(self, alloc, at_least) {
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
    reserve(self, alloc, count)
}

// `T` is annotated as a type argument, which are handled carefully to interface with C's sizeof and alignof
// builtins. If you examine the TL `builtin` module you'll see sizeof and alignof are implemented as
// wrappers around TL intrinsic functions.
_type_width_aligned(T: Type) { Alloc.align(sizeof(T), alignof(T)) }


// Start a new module. A single file may contain multiple modules, but the same module cannot be implemented
// using multiple #module sections. That is, once we start a new module, we cannot re-open a previously
// defined module.
#module Array.Indexed

// -- Indexed iterator interface --

IterValuePtr[T]: {
    ptr:   Ptr[T],
    index: Int,
}

IterValue(T): {
    value: T,
    index: Int,
}

Iter(T): {
    v:     Ptr[T],
    size:  Int,
    index: Int,
    iv:    IterValue(T),
    ivp:   IterValuePtr[T],
}


iter_init   (arr: Ptr[Array.Array(T])) -> Iter(T)
iter_ptr    (iter: Ptr[Iter(T]))       -> IterValuePtr[T]
iter_value  (iter: Ptr[Iter(T]))       -> IterValue(T)
iter_cond   (iter: Ptr[Iter(T]))       -> Bool
iter_update (iter: Ptr[Iter(T]))       -> Void
iter_deinit (iter: Ptr[Iter(T]))       -> Void

iter_init(arr) { Iter (v = arr->v, size = arr->size, index = 0,
                       iv  = IterValue (value = void, index = 0),
                       ivp = IterValuePtr[ptr = null, index = 0])
}

iter_ptr   (iter) { iter->ivp.ptr  = iter->v.(iter->index).& iter->ivp.index = iter->index iter->ivp }
iter_value (iter) { iter->iv.value = iter->v.(iter->index) iter->iv.index = iter->index iter->iv     }
iter_cond  (iter) { iter->index < iter->size                                                        }
iter_update(iter) { iter->index = iter->index + 1                                                   }
iter_deinit(iter) {                                                                                 }
