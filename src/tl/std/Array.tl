// The Array module.
//
// For a tutorial introduction to the TL language, see the file Array-tutorial.tl.
#module Array
#import <Alloc.tl>
#import <stdlib.tl>
#import <stdio.tl> // for fatal()

// The Array struct.
T(a) : {
    v:        Ptr(a),
    size:     Int,
    capacity: Int,
}

TPtr = Ptr(T(a))

// -- Synopsis --

// Construct an empty array with elements of type a.
empty(a: Type) -> T(a)

// Construct an array with count elements of type a, each initialised to val.
init(val: a, count: Int) -> T(a)

// Allocate an empty array with elements of type a and an initial capacity of count.
with_capacity(a: Type, count: Int) -> T(a)

// Allocate an empty array with elements of type a and an initial capacity of count, and set the size to the
// capacity, leaving the contents undefined.
with_capacity_undefined(a: Type, count: Int) -> T(a)


// Push an item to the back of the array, allocating to expand capacity if necessary.
push(self: Ptr(T(a)), x: a) -> Void

// Ensure the array's capacity is large enough for count items, allocating if necessary.
reserve(self: Ptr(T(a)), count: Int) -> Void

// Deallocate array buffer.
free(self: TPtr) -> Void




// -- Implementation --

empty(a: Type) -> T(a) {
    T ( v : Ptr(a) = null, capacity = 0, size = 0 )
}

init(val: a, count: Int) {
    arr := with_capacity(a, count)
    arr.size = count
    for p.& in arr { p.* = val }
    arr
}

free(self) {
    c_free(self->v)
    self->v = null
}

with_capacity(a: Type, count: Int) {
    width      := _type_width_aligned(a)
    p : Ptr(a) := c_malloc(count * width)
    T ( v = p, capacity = count, size = 0 )
}

with_capacity_undefined(a, count) {
    result := with_capacity(a, count)
    result.size = count
    result
}


reserve(self: Ptr(T(a)), count: Int) {
    if self->capacity < count {
        p : Ptr(a) := c_realloc(self->v, count * _type_width_aligned(a))
        if p == null { _tl_fatal_("oom") }
        self->v = p
        self->capacity = count
    }
}


push(self, x) {
    if _is_full(self) { _grow(self, self->capacity + 1) }
    self->v[self->size] = x
    self->size = self->size + 1
}

// -- Iterator interface --

Iter(a) : {
    v:        Ptr(a),
    size:     Int,
    index:    Int,
}

iter_init    (arr:  Ptr(T(a)))    -> Iter(a)
iter_ptr     (iter: Ptr(Iter(a))) -> Ptr(a)
iter_value   (iter: Ptr(Iter(a))) -> a
iter_cond    (iter: Ptr(Iter(a))) -> Bool
iter_update  (iter: Ptr(Iter(a))) -> Void
iter_deinit  (iter: Ptr(Iter(a))) -> Void

iter_init(arr)    { Iter ( v = arr->v, size = arr->size, index = 0 ) }
iter_ptr(iter)    { iter->v[iter->index].&                           }
iter_value(iter)  { iter->v[iter->index]                             }
iter_cond(iter)   { iter->index < iter->size                         }
iter_update(iter) { iter->index = iter->index + 1                    }
iter_deinit(iter) {                                                  }



// -- Private functions --

_grow               (self: TPtr, at_least: Int) -> Void
_is_full            (self: TPtr)                -> Bool
_type_width_aligned (a: Type)                   -> Int

// --


_is_full(self) { self->capacity == self->size }

_grow(self, at_least) {
    if self->capacity >= at_least { return }

    count := self->capacity
    if count < 1 { count = 1 }

    while count < at_least { count = count * 2 }

    reserve(self, count)
}

_type_width_aligned(a: Type) { Alloc.align(sizeof(a), alignof(a)) }


// -- IndexedArray --

#module IndexedArray

// -- Indexed iterator interface --

IterValuePtr(a) : {
    ptr:   Ptr(a),
    index: Int,
}

IterValue(a) : {
    value: a,
    index: Int,
}

Iter(a) : {
    v:        Ptr(a),
    size:     Int,
    index:    Int,
    iv:       IterValue(a),
    ivp:      IterValuePtr(a),
}


iter_init    (arr:  Ptr(Array.T(a))) -> Iter(a)
iter_ptr     (iter: Ptr(Iter(a)))    -> IterValuePtr(a)
iter_value   (iter: Ptr(Iter(a)))    -> IterValue(a)
iter_cond    (iter: Ptr(Iter(a)))    -> Bool
iter_update  (iter: Ptr(Iter(a)))    -> Void
iter_deinit  (iter: Ptr(Iter(a)))    -> Void

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
