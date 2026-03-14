// The Array module.
//
// For a tutorial introduction to the TL language, see the file Array-tutorial.tl.
#module Array
#import <Alloc.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <string.tl>

Allocator = Alloc.Allocator

// The Array struct.
Array[T]: {
    v:        Ptr[T],
    size:     CSize,
    capacity: CSize,
}

// -- Synopsis --

// All allocating functions take a Ptr[Allocator] argument as their first or second argument. Variants exist
// with no allocator argument: these use the default managed allocator.

// Construct an empty array with elements of type a.
empty[T]() -> Array[T]

// Construct an array with count elements of type a, each initialised to val.
init[T](alloc: Ptr[Allocator], val: T, count: CSize) -> Array[T]
init[T](val: T, count: CSize)                        -> Array[T]

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity[T](alloc: Ptr[Allocator], count: CSize) -> Array[T]
with_capacity[T](count: CSize)                        -> Array[T]

// Allocate an empty array with elements of type a and an initial capacity of count, and set the size to the
// capacity, leaving the contents undefined.
with_capacity_undefined[T](alloc: Ptr[Allocator], count: CSize) -> Array[T]
with_capacity_undefined[T](count: CSize)                        -> Array[T]


// Push an item to the back of the array, allocating to expand capacity if necessary.
push[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], x: T) -> Void
push[T](self: Ptr[Array[T]], x: T)                        -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], count: CSize) -> Void
reserve[T](self: Ptr[Array[T]], count: CSize)                        -> Void

// Deallocate array buffer.
free[T](self: Ptr[Array[T]], alloc: Ptr[Allocator]) -> Void
free[T](self: Ptr[Array[T]])                        -> Void

// Element access (bounds-checked).
get[T]  (arr: Array[T], index: CSize) -> T
front[T](arr: Array[T])               -> T
back[T] (arr: Array[T])               -> T

// Removal / mutation.
pop[T]        (self: Ptr[Array[T]])                                              -> T
clear[T]      (self: Ptr[Array[T]])                                              -> Void
insert[T]     (self: Ptr[Array[T]], alloc: Ptr[Allocator], index: CSize, val: T) -> Void
insert[T]     (self: Ptr[Array[T]], index: CSize, val: T)                        -> Void
remove[T]     (self: Ptr[Array[T]], index: CSize)                                -> T
swap_remove[T](self: Ptr[Array[T]], index: CSize)                                -> T

// Sizing.
resize[T]       (self: Ptr[Array[T]], alloc: Ptr[Allocator], count: CSize, val: T) -> Void
resize[T]       (self: Ptr[Array[T]], count: CSize, val: T)                        -> Void
shrink_to_fit[T](self: Ptr[Array[T]], alloc: Ptr[Allocator])                       -> Void
shrink_to_fit[T](self: Ptr[Array[T]])                                              -> Void
truncate[T]     (self: Ptr[Array[T]], count: CSize)                                -> Void

// Bulk operations.
append[T](self: Ptr[Array[T]], alloc: Ptr[Allocator], other: Array[T])    -> Void
append[T](self: Ptr[Array[T]], other: Array[T])                           -> Void
clone[T] (alloc: Ptr[Allocator], arr: Array[T])                           -> Array[T]
clone[T] (arr: Array[T])                                                  -> Array[T]
slice[T] (alloc: Ptr[Allocator], arr: Array[T], start: CSize, end: CSize) -> Array[T]
slice[T] (arr: Array[T], start: CSize, end: CSize)                        -> Array[T]

// Search / query.
is_empty[T](arr: Array[T])         -> Bool
contains[T](arr: Array[T], val: T) -> Bool
index_of[T](arr: Array[T], val: T) -> Option[CSize]

// Sorting.
sort[T]  (self: Ptr[Array[T]], tmp_alloc: Ptr[Allocator], cmp: (T, T) -> Int)                  -> Void
sort[T]  (self: Ptr[Array[T]], cmp: (T, T) -> Int)                                             -> Void
sorted[T](alloc: Ptr[Allocator], tmp_alloc: Ptr[Allocator], arr: Array[T], cmp: (T, T) -> Int) -> Array[T]
sorted[T](arr: Array[T], cmp: (T, T) -> Int)                                                   -> Array[T]

// Reordering.
reverse[T](self: Ptr[Array[T]])                     -> Void
swap[T]   (self: Ptr[Array[T]], i: CSize, j: CSize) -> Void

// Construction.
from_ptr[T](alloc: Ptr[Allocator], ptr: Ptr[T], len: CSize) -> Array[T]
from_ptr[T](ptr: Ptr[T], len: CSize)                        -> Array[T]

// Functional operations.
map[T, U]   (alloc: Ptr[Allocator], arr: Array[T], f: (T) -> U)    -> Array[U]
map[T, U]   (arr: Array[T], f: (T) -> U)                           -> Array[U]
filter[T]   (alloc: Ptr[Allocator], arr: Array[T], f: (T) -> Bool) -> Array[T]
filter[T]   (arr: Array[T], f: (T) -> Bool)                        -> Array[T]
reduce[T, U](arr: Array[T], init: U, f: (U, T) -> U)               -> U
foreach[T]  (arr: Array[T], f: (T) -> Void)                        -> Void
any_of[T]   (arr: Array[T], f: (T) -> Bool)                        -> Bool
all[T]      (arr: Array[T], f: (T) -> Bool)                        -> Bool
find[T]     (arr: Array[T], f: (T) -> Bool)                        -> Option[CSize]


// -- Implementation --

empty[T]() -> Array[T] {
    Array ( v: Ptr[T] = null, capacity = 0zu, size = 0zu)
}


init[T](alloc, val: T, count) {
    arr := with_capacity[T](alloc, count)
    arr.size             = count
    for p.& in arr { p.* = val }
    arr
}

init[T](val: T, count: CSize) {
    init(Alloc.context.default, val, count)
}

free(self, alloc) {
    alloc->free(alloc, self->v)
    self->v = null
}

free(self) {
    free(self, Alloc.context.default)
}


with_capacity[T](alloc, count) {
    width     := _type_width_aligned[T]()
    p: Ptr[T] := alloc->malloc(alloc, count * width)
    Array ( v = p, capacity = count, size = 0zu)
}

with_capacity[T](count: CSize) {
    with_capacity[T](Alloc.context.default, count)
}

with_capacity_undefined[T](alloc, count) {
    result := with_capacity[T](alloc, count)
    result.size = count
    result
}

with_capacity_undefined[T](count) {
    with_capacity_undefined[T](Alloc.context.default, count)
}

reserve[T](self, alloc, count: CSize) {
    if self->capacity < count {
        p := alloc->realloc(alloc, self->v, count * _type_width_aligned[T]())
        if p == null { _tl_fatal_("oom") }
        self->v        = p
        self->capacity = count
    }
}

reserve[T](self, count: CSize) {
    reserve(self, Alloc.context.default, count)
}

push(self, alloc, x) {
    if _is_full(self) { _grow(self, alloc, self->capacity + 1zu) }
    self->v.[self->size] = x
    self->size           = self->size + 1zu
}

push(self, x) {
    push(self, Alloc.context.default, x)
}


// -- Element access --

get[T](arr: Array[T], index: CSize) -> T {
    if index >= arr.size { _tl_fatal_("Array.get: index out of bounds") }
    arr.v.[index]
}

front[T](arr: Array[T]) -> T {
    if arr.size == 0zu { _tl_fatal_("Array.front: empty array") }
    arr.v.[0zu]
}

back[T](arr: Array[T]) -> T {
    if arr.size == 0zu { _tl_fatal_("Array.back: empty array") }
    arr.v.[arr.size - 1zu]
}


// -- Removal / mutation --

pop[T](self: Ptr[Array[T]]) -> T {
    if self->size == 0zu { _tl_fatal_("Array.pop: empty array") }
    self->size = self->size - 1zu
    self->v.[self->size]
}

clear[T](self: Ptr[Array[T]]) -> Void {
    self->size = 0zu
}

insert[T](self: Ptr[Array[T]], alloc, index: CSize, val: T) {
    if index > self->size { _tl_fatal_("Array.insert: index out of bounds") }
    if _is_full(self) { _grow(self, alloc, self->capacity + 1zu) }
    if index < self->size {
        width       := _type_width_aligned[T]()
        dst: Ptr[T] := self->v.[index + 1zu].&
        src: Ptr[T] := self->v.[index].&
        c_memmove(dst, src, (self->size - index) * width)
    }
    self->v.[index] = val
    self->size      = self->size + 1zu
}

insert[T](self: Ptr[Array[T]], index: CSize, val: T) {
    insert(self, Alloc.context.default, index, val)
}

remove[T](self: Ptr[Array[T]], index: CSize) -> T {
    if index >= self->size { _tl_fatal_("Array.remove: index out of bounds") }
    val := self->v.[index]
    self->size = self->size - 1zu
    if index < self->size {
        width       := _type_width_aligned[T]()
        dst: Ptr[T] := self->v.[index].&
        src: Ptr[T] := self->v.[index + 1zu].&
        c_memmove(dst, src, (self->size - index) * width)
    }
    val
}

swap_remove[T](self: Ptr[Array[T]], index: CSize) -> T {
    if index >= self->size { _tl_fatal_("Array.swap_remove: index out of bounds") }
    val := self->v.[index]
    self->size = self->size - 1zu
    if index < self->size {
        self->v.[index] = self->v.[self->size]
    }
    val
}


// -- Sizing --

resize[T](self: Ptr[Array[T]], alloc, count: CSize, val: T) {
    if count > self->size {
        _grow(self, alloc, count)
        i := self->size
        while i < count, i += 1zu {
            self->v.[i] = val
        }
    }
    self->size = count
}

resize[T](self: Ptr[Array[T]], count: CSize, val: T) {
    resize(self, Alloc.context.default, count, val)
}

shrink_to_fit[T](self: Ptr[Array[T]], alloc) {
    if self->size < self->capacity {
        if self->size == 0zu {
            alloc->free(alloc, self->v)
            self->v        = null
            self->capacity = 0zu
        } else {
            width := _type_width_aligned[T]()
            p     := alloc->realloc(alloc, self->v, self->size * width)
            if p != null {
                self->v        = p
                self->capacity = self->size
            }
        }
    }
}

shrink_to_fit[T](self: Ptr[Array[T]]) {
    shrink_to_fit(self, Alloc.context.default)
}

truncate[T](self: Ptr[Array[T]], count: CSize) {
    if count < self->size {
        self->size = count
    }
}


// -- Reordering --

swap[T](self: Ptr[Array[T]], i: CSize, j: CSize) {
    if i >= self->size { _tl_fatal_("Array.swap: index out of bounds") }
    if j >= self->size { _tl_fatal_("Array.swap: index out of bounds") }
    tmp := self->v.[i]
    self->v.[i] = self->v.[j]
    self->v.[j] = tmp
}

reverse[T](self: Ptr[Array[T]]) {
    if self->size == 0zu { return void }
    i: CSize := 0zu
    j        := self->size - 1zu
    while i < j, i += 1zu {
        swap(self, i, j)
        j = j - 1zu
    }
}


// -- Bulk operations --

append[T](self: Ptr[Array[T]], alloc, other: Array[T]) {
    if other.size == 0zu { return void }
    _grow(self, alloc, self->size + other.size)
    width       := _type_width_aligned[T]()
    dst: Ptr[T] := self->v.[self->size].&
    c_memcpy(dst, other.v, other.size * width)
    self->size = self->size + other.size
}

append[T](self: Ptr[Array[T]], other: Array[T]) {
    append(self, Alloc.context.default, other)
}

clone[T](alloc, arr: Array[T]) -> Array[T] {
    if arr.size == 0zu { return empty[T]() }
    result := with_capacity[T](alloc, arr.size)
    result.size = arr.size
    width := _type_width_aligned[T]()
    c_memcpy(result.v, arr.v, arr.size * width)
    result
}

clone[T](arr: Array[T]) -> Array[T] {
    clone(Alloc.context.default, arr)
}

slice[T](alloc, arr: Array[T], start: CSize, end: CSize) -> Array[T] {
    if end > arr.size || start > end { _tl_fatal_("Array.slice: index out of bounds") }
    len := end - start
    if len == 0zu { return empty[T]() }
    result := with_capacity[T](alloc, len)
    result.size = len
    width       := _type_width_aligned[T]()
    src: Ptr[T] := arr.v.[start].&
    c_memcpy(result.v, src, len * width)
    result
}

slice[T](arr: Array[T], start: CSize, end: CSize) -> Array[T] {
    slice(Alloc.context.default, arr, start, end)
}


// -- Search / query --

is_empty[T](arr: Array[T]) -> Bool {
    arr.size == 0zu
}

contains[T](arr: Array[T], val: T) -> Bool {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if arr.v.[i] == val { return true }
    }
    false
}

index_of[T](arr: Array[T], val: T) -> Option[CSize] {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if arr.v.[i] == val { return Some(i) }
    }
    None
}

// -- Construction --

from_ptr[T](alloc, ptr: Ptr[T], len: CSize) -> Array[T] {
    if len == 0zu { return empty[T]() }
    result := with_capacity[T](alloc, len)
    result.size = len
    width := _type_width_aligned[T]()
    c_memcpy(result.v, ptr, len * width)
    result
}

from_ptr[T](ptr: Ptr[T], len: CSize) -> Array[T] {
    from_ptr(Alloc.context.default, ptr, len)
}


// -- Functional operations --

map[T, U](alloc, arr: Array[T], f: (T) -> U) -> Array[U] {
    result := with_capacity[U](alloc, arr.size)
    result.size = arr.size
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        result.v.[i] = f(arr.v.[i])
    }
    result
}

map[T, U](arr: Array[T], f: (T) -> U) -> Array[U] {
    map(Alloc.context.default, arr, f)
}

filter[T](alloc, arr: Array[T], f: (T) -> Bool) -> Array[T] {
    result   := empty[T]()
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if f(arr.v.[i]) { push(result.&, alloc, arr.v.[i]) }
    }
    result
}

filter[T](arr: Array[T], f: (T) -> Bool) -> Array[T] {
    filter(Alloc.context.default, arr, f)
}

reduce[T, U](arr: Array[T], init: U, f: (U, T) -> U) -> U {
    acc      := init
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        acc = f(acc, arr.v.[i])
    }
    acc
}

foreach[T](arr: Array[T], f: (T) -> Void) -> Void {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        f(arr.v.[i])
    }
}

any_of[T](arr: Array[T], f: (T) -> Bool) -> Bool {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if f(arr.v.[i]) { return true }
    }
    false
}

all[T](arr: Array[T], f: (T) -> Bool) -> Bool {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if !f(arr.v.[i]) { return false }
    }
    true
}

find[T](arr: Array[T], f: (T) -> Bool) -> Option[CSize] {
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if f(arr.v.[i]) { return Some(i) }
    }
    None
}


// -- Sorting --

_merge[T](v: Ptr[T], tmp: Ptr[T], lo: CSize, mid: CSize, hi: CSize, cmp: (T, T) -> Int, width: CSize) {
    c_memcpy(tmp.[lo].&, v.[lo].&, (hi - lo) * width)
    i := lo
    j := mid
    k := lo
    while i < mid && j < hi {
        if cmp(tmp.[i], tmp.[j]) <= 0 {
            v.[k] = tmp.[i]
            i     = i + 1zu
        } else {
            v.[k] = tmp.[j]
            j     = j + 1zu
        }
        k = k + 1zu
    }
    while i < mid, i += 1zu {
        v.[k] = tmp.[i]
        k     = k + 1zu
    }
    while j < hi, j += 1zu {
        v.[k] = tmp.[j]
        k     = k + 1zu
    }
}

sort[T](self: Ptr[Array[T]], tmp_alloc, cmp: (T, T) -> Int) {
    n := self->size
    if n <= 1zu { return void }
    width       := _type_width_aligned[T]()
    tmp: Ptr[T] := tmp_alloc->malloc(tmp_alloc, n * width)
    if tmp == null { _tl_fatal_("Array.sort: oom") }
    w: CSize := 1zu
    while w < n {
        i: CSize := 0zu
        while i < n {
            mid := i + w
            if mid > n { mid = n }
            hi := i + w * 2zu
            if hi > n { hi = n }
            if mid < hi {
                _merge(self->v, tmp, i, mid, hi, cmp, width)
            }
            i = i + w * 2zu
        }
        w = w * 2zu
    }
    tmp_alloc->free(tmp_alloc, tmp)
}

sort[T](self: Ptr[Array[T]], cmp: (T, T) -> Int) {
    sort(self, Alloc.context.transient, cmp)
}

sorted[T](alloc, tmp_alloc, arr: Array[T], cmp: (T, T) -> Int) -> Array[T] {
    result := clone(alloc, arr)
    sort(result.&, tmp_alloc, cmp)
    result
}

sorted[T](arr: Array[T], cmp: (T, T) -> Int) -> Array[T] {
    sorted(Alloc.context.default, Alloc.context.transient, arr, cmp)
}


// -- Iterator interface --

Iter[T]: {
    v:     Ptr[T],
    size:  CSize,
    index: CSize,
}

iter_init[T]  (arr: Ptr[Array[T]]) -> Iter[T]
iter_ptr[T]   (iter: Ptr[Iter[T]]) -> Ptr[T]
iter_value[T] (iter: Ptr[Iter[T]]) -> T
iter_cond[T]  (iter: Ptr[Iter[T]]) -> Bool
iter_update[T](iter: Ptr[Iter[T]]) -> Void
iter_deinit[T](iter: Ptr[Iter[T]]) -> Void

iter_init  (arr)  { Iter ( v = arr->v, size = arr->size, index = 0zu) }
iter_ptr   (iter) { iter->v.[iter->index].&                           }
iter_value (iter) { iter->v.[iter->index]                             }
iter_cond  (iter) { iter->index < iter->size                          }
iter_update(iter) { iter->index = iter->index + 1zu                   }
iter_deinit(iter) {                                                   }


// -- Private functions --

_grow[T]              (self: Ptr[Array[T]], alloc: Ptr[Allocator], at_least: CSize) -> Void
_is_full[T]           (self: Ptr[Array[T]])                                         -> Bool
_type_width_aligned[T]()                                                            -> CSize

// --


_is_full(self) { self->capacity == self->size }

_grow(self, alloc, at_least) {
    if self->capacity >= at_least { return }

    count := self->capacity
    if count < 1zu { count = 1zu }

    while count < at_least { count = count * 2zu }

    reserve(self, alloc, count)
}

_type_width_aligned[T]() { Alloc.align(sizeof[T](), alignof[T]()) }


// -- Array.Indexed --

#module Array.Indexed

// -- Indexed iterator interface --

IterValuePtr[T]: {
    ptr:   Ptr[T],
    index: CSize,
}

IterValue[T]: {
    value: T,
    index: CSize,
}

Iter[T]: {
    v:     Ptr[T],
    size:  CSize,
    index: CSize,
    iv:    IterValue[T],
    ivp:   IterValuePtr[T],
}


iter_init[T]  (arr: Ptr[Array.Array[T]]) -> Iter[T]
iter_ptr[T]   (iter: Ptr[Iter[T]])       -> IterValuePtr[T]
iter_value[T] (iter: Ptr[Iter[T]])       -> IterValue[T]
iter_cond[T]  (iter: Ptr[Iter[T]])       -> Bool
iter_update[T](iter: Ptr[Iter[T]])       -> Void
iter_deinit[T](iter: Ptr[Iter[T]])       -> Void

iter_init(arr) { Iter (v = arr->v,
                       size  = arr->size,
                       index = 0zu,
                       iv    = IterValue (value = void, index = 0zu),
                       ivp   = IterValuePtr(ptr = null, index = 0zu)) }

iter_ptr   (iter) { iter->ivp.ptr  = iter->v.[iter->index].& iter->ivp.index = iter->index iter->ivp }
iter_value (iter) { iter->iv.value = iter->v.[iter->index] iter->iv.index = iter->index iter->iv     }
iter_cond  (iter) { iter->index < iter->size                                                         }
iter_update(iter) { iter->index = iter->index + 1zu                                                  }
iter_deinit(iter) {                                                                                  }
