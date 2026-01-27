// The Array module.
//
// For a tutorial introduction to the TL language, see the file Array-tutorial.tl.
#module Array
#import <Alloc.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <fatal.tl>

// The Array struct.
Array(T) : {
    v:        Ptr(T),
    size:     Int,
    capacity: Int,
}

// -- Synopsis --

// All allocating functions take a Ptr(Allocator) argument as their first or second argument. Variants exist
// with no allocator argument: these use the default managed allocator.

// Construct an empty array with elements of type a.
empty(T: Type) -> Array(T)

// Construct an array with count elements of type a, each initialised to val.
init(alloc: Ptr(Allocator), val: T, count: Int) -> Array(T)
init(val: T, count: Int) -> Array(T)

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(alloc: Ptr(Allocator), T: Type, count: Int) -> Array(T)
with_capacity(T: Type, count: Int) -> Array(T)

// Allocate an empty array with elements of type a and an initial capacity of count, and set the size to the
// capacity, leaving the contents undefined.
with_capacity_undefined(alloc: Ptr(Allocator), T: Type, count: Int) -> Array(T)
with_capacity_undefined(T: Type, count: Int) -> Array(T)


// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: Ptr(Array(T)), alloc: Ptr(Allocator), x: T) -> Void
push(self: Ptr(Array(T)), x: T) -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: Ptr(Array(T)), alloc: Ptr(Allocator), count: Int) -> Void
reserve(self: Ptr(Array(T)), count: Int) -> Void

// Deallocate array buffer.
free(self: Ptr(Array), alloc: Ptr(Allocator)) -> Void
free(self: Ptr(Array)) -> Void



// -- Implementation --

empty(T: Type) -> Array(T) {
    Array ( v : Ptr(T) = null, capacity = 0, size = 0 )
}


init(alloc, val: T, count) {
    arr := with_capacity(alloc, T, count)
    arr.size = count
    for p.& in arr { p.* = val }
    arr
}

init(val: T, count: Int) {
    init(Alloc.context.managed, val, count)
}

free(self, alloc) {
    alloc->free(alloc, self->v)
    self->v = null
}

free(self) {
    free(self, Alloc.context.managed)
}


with_capacity(alloc, T: Type, count) {
    width      := _type_width_aligned(T)
    p : Ptr(T) := alloc->malloc(alloc, count * width)
    Array ( v = p, capacity = count, size = 0 )
}

with_capacity(T: Type, count: Int) {
    with_capacity(Alloc.context.managed, T, count)
}

with_capacity_undefined(alloc, T: Type, count) {
    result := with_capacity(alloc, T, count)
    result.size = count
    result
}

with_capacity_undefined(T: Type, count) {
    with_capacity_undefined(Alloc.context.managed, T, count)
}

reserve(self: Ptr(Array(T)), alloc, count: Int) {
    if self->capacity < count {
        p := alloc->realloc(alloc, self->v, count * _type_width_aligned(T))
        if p == null { _tl_fatal_("oom") }
        self->v = p
        self->capacity = count
    }
}

reserve(self: Ptr(Array(T)), count: Int) {
    reserve(self, Alloc.context.managed, count)
}

push(self, alloc, x) {
    if _is_full(self) { _grow(self, alloc, self->capacity + 1) }
    self->v[self->size] = x
    self->size = self->size + 1
}

push(self, x) {
    push(self, Alloc.context.managed, x)
}

// -- Iterator interface --

Iter(T) : {
    v:        Ptr(T),
    size:     Int,
    index:    Int,
}

iter_init    (arr:  Ptr(Array(T)))-> Iter(T)
iter_ptr     (iter: Ptr(Iter(T))) -> Ptr(T)
iter_value   (iter: Ptr(Iter(T))) -> T
iter_cond    (iter: Ptr(Iter(T))) -> Bool
iter_update  (iter: Ptr(Iter(T))) -> Void
iter_deinit  (iter: Ptr(Iter(T))) -> Void

iter_init(arr)    { Iter ( v = arr->v, size = arr->size, index = 0 ) }
iter_ptr(iter)    { iter->v[iter->index].&                           }
iter_value(iter)  { iter->v[iter->index]                             }
iter_cond(iter)   { iter->index < iter->size                         }
iter_update(iter) { iter->index = iter->index + 1                    }
iter_deinit(iter) {                                                  }



// -- Private functions --

_grow               (self: Ptr(T), alloc: Ptr(Allocator), at_least: Int) -> Void
_is_full            (self: Ptr(T))                                       -> Bool
_type_width_aligned (T: Type)                                            -> Int

// --


_is_full(self) { self->capacity == self->size }

_grow(self, alloc, at_least) {
    if self->capacity >= at_least { return }

    count := self->capacity
    if count < 1 { count = 1 }

    while count < at_least { count = count * 2 }

    reserve(self, alloc, count)
}

_type_width_aligned(T: Type) { Alloc.align(sizeof(T), alignof(T)) }


// -- IndexedArray --

#module IndexedArray

// -- Indexed iterator interface --

IterValuePtr(T) : {
    ptr:   Ptr(T),
    index: Int,
}

IterValue(T) : {
    value: T,
    index: Int,
}

Iter(T) : {
    v:        Ptr(T),
    size:     Int,
    index:    Int,
    iv:       IterValue(T),
    ivp:      IterValuePtr(T),
}


iter_init    (arr:  Ptr(Array.Array(T))) -> Iter(T)
iter_ptr     (iter: Ptr(Iter(T)))    -> IterValuePtr(T)
iter_value   (iter: Ptr(Iter(T)))    -> IterValue(T)
iter_cond    (iter: Ptr(Iter(T)))    -> Bool
iter_update  (iter: Ptr(Iter(T)))    -> Void
iter_deinit  (iter: Ptr(Iter(T)))    -> Void

iter_init(arr)    { Iter (v     = arr->v,
                          size  = arr->size,
                          index = 0,
                          iv    = IterValue   (value = void, index = 0),
                          ivp   = IterValuePtr(ptr   = null, index = 0)) }

iter_ptr(iter)    { iter->ivp.ptr  = iter->v[iter->index].& iter->ivp.index = iter->index iter->ivp     }
iter_value(iter)  { iter->iv.value = iter->v[iter->index]   iter->iv.index  = iter->index iter->iv      }
iter_cond(iter)   { iter->index < iter->size                                                            }
iter_update(iter) { iter->index = iter->index + 1                                                       }
iter_deinit(iter) {                                                                                     }
