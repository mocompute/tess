// Array — Dynamic growable array.
#module Array
#import <Alloc.tl>
#import <cstdlib.tl>
#import <cstdio.tl>
#import <cstring.tl>

Allocator = Alloc.Allocator

// Primary type.
Array[T]: {
    data:     Ptr[T],
    size:     CSize,
    capacity: CSize,
}

// -- Synopsis --

// Constructors: type argument and no receiver
[T](): {
    // Construct an empty array.
    empty() -> Array[T]

    // Construct an array of size len and copy len bytes from ptr into it.
    from_ptr(ptr: Ptr[T], len: CSize) -> Array[T]

    // Construct an array with count elements, each initialised to val.
    init(val: T, count: CSize) -> Array[T]

    // Allocate an array with size and capacity of count, contents undefined.
    init_undefined(count: CSize) -> Array[T]

    // Allocate an empty array with an initial capacity of count.
    with_capacity(count: CSize) -> Array[T]

    // -- With allocators --

    from_ptr      (alloc: Ptr[Allocator], ptr: Ptr[T], len: CSize) -> Array[T]
    init          (alloc: Ptr[Allocator], val: T, count: CSize)    -> Array[T]
    with_capacity (alloc: Ptr[Allocator], count: CSize)            -> Array[T]
    init_undefined(alloc: Ptr[Allocator], count: CSize)            -> Array[T]


}

// Mutating operations
(self: Ptr[Array[T]]): {
    // Push an item to the back, allocating if necessary.
    push(x: T) -> Void

    // Ensure capacity is large enough for count items, allocating if necessary.
    reserve(count: CSize) -> Void

    // Deallocate array buffer.
    free() -> Void

    // -- Removal / mutation --

    pop        ()                     -> T
    clear      ()                     -> Void
    insert     (index: CSize, val: T) -> Void
    remove     (index: CSize)         -> T
    swap_remove(index: CSize)         -> T

    // -- Sizing --

    resize       (count: CSize, val: T) -> Void
    shrink_to_fit()                     -> Void
    truncate     (count: CSize)         -> Void

    // -- Bulk operations --

    append(other: Array[T]) -> Void

    // -- Sorting --

    sort(cmp: (T, T) -> Int) -> Void

    // -- With allocators --

    push         (alloc: Ptr[Allocator], x: T)                   -> Void
    reserve      (alloc: Ptr[Allocator], count: CSize)           -> Void
    free         (alloc: Ptr[Allocator])                         -> Void
    insert       (alloc: Ptr[Allocator], index: CSize, val: T)   -> Void
    resize       (alloc: Ptr[Allocator], count: CSize, val: T)   -> Void
    shrink_to_fit(alloc: Ptr[Allocator])                         -> Void
    append       (alloc: Ptr[Allocator], other: Array[T])        -> Void
    sort         (tmp_alloc: Ptr[Allocator], cmp: (T, T) -> Int) -> Void
}

// Immutable operations
(arr: Ptr[Const[Array[T]]]): {

    // -- Element access (bounds-checked) --

    get  (index: CSize) -> T
    front()             -> T
    back ()             -> T

    // -- Bulk operations --

    clone()                         -> Array[T]
    copy (start: CSize, end: CSize) -> Array[T]
    slice(start: CSize, end: CSize) -> Slice[T]

    // -- Search / query --

    is_empty()       -> Bool
    contains(val: T) -> Bool
    index_of(val: T) -> Option[CSize]

    // -- Sorting --

    sorted(cmp: (T, T) -> Int) -> Array[T]

    // -- Reordering --

    reverse()                   -> Void
    swap   (i: CSize, j: CSize) -> Void

    // -- Functional operations --

    map[U]   (f: (T) -> U)             -> Array[U]
    filter   (f: (T) -> Bool)          -> Array[T]
    reduce[U](init: U, f: (U, T) -> U) -> U
    foreach  (f: (T) -> Void)          -> Void
    any_of   (f: (T) -> Bool)          -> Bool
    all      (f: (T) -> Bool)          -> Bool
    find     (f: (T) -> Bool)          -> Option[CSize]

    // -- With allocators --

    clone (alloc: Ptr[Allocator])                                                -> Array[T]
    copy  (alloc: Ptr[Allocator], start: CSize, end: CSize)                      -> Array[T]
    filter(alloc: Ptr[Allocator], f: (T) -> Bool)                                -> Array[T]
    map[U](alloc: Ptr[Allocator], f: (T) -> U)                                   -> Array[U]
    sorted(alloc: Ptr[Allocator], tmp_alloc: Ptr[Allocator], cmp: (T, T) -> Int) -> Array[T]
}


// -- Private functions --

_grow[T]              (self: Ptr[Array[T]], alloc: Ptr[Allocator], at_least: CSize)                                -> Void
_is_full[T]           (self: Ptr[Const[Array[T]]])                                                                 -> Bool
_type_width_aligned[T]()                                                                                           -> CSize
_merge[T]             (v: Ptr[T], tmp: Ptr[T], lo: CSize, mid: CSize, hi: CSize, cmp: (T, T) -> Int, width: CSize) -> Void

// -- Implementation --

_is_full(self) { self->capacity == self->size }

_grow(self, alloc, at_least) {
    if self->capacity >= at_least { return }

    count := self->capacity
    if count < 1 { count = 1 }

    while count < at_least { count *= 2 }

    reserve(self, alloc, count)
}

_type_width_aligned[T]() { Alloc.align(sizeof[T](), alignof[T]()) }

_merge[T](v, tmp, lo, mid, hi, cmp: (T, T) -> Int, width) {
    c_memcpy(tmp.[lo].&, v.[lo].&, (hi - lo) * width)
    i := lo
    j := mid
    k := lo
    while i < mid && j < hi {
        if cmp(tmp.[i], tmp.[j]) <= 0 {
            v.[k] = tmp.[i]
            i += 1
        } else {
            v.[k] = tmp.[j]
            j += 1
        }
        k += 1
    }
    while i < mid, i += 1 {
        v.[k] = tmp.[i]
        k += 1
    }
    while j < hi, j += 1 {
        v.[k] = tmp.[j]
        k += 1
    }
}


// -- Constructors --

[T]():      {
    empty() {
        Array (data: Ptr[T] = null, capacity = 0, size = 0)
    }

    from_ptr(alloc, ptr, len) {
        if len == 0 { return empty[T]() }
        result := with_capacity[T](alloc, len)
        result.size = len
        width := _type_width_aligned[T]()
        c_memcpy(result.data, ptr, len * width)
        result
    }

    from_ptr(ptr, len) {
        from_ptr(Alloc.context.default, ptr, len)
    }

    init(alloc, val: T, count) {
        arr := with_capacity[T](alloc, count)
        arr.size = count
        for p.& in arr { p.* = val }
        arr
    }

    init(val: T, count) {
        init(Alloc.context.default, val, count)
    }

    init_undefined(alloc, count) {
        result := with_capacity[T](alloc, count)
        result.size = count
        result
    }

    init_undefined(count) {
        init_undefined[T](Alloc.context.default, count)
    }


    with_capacity(alloc, count) {
        width     := _type_width_aligned[T]()
        p: Ptr[T] := alloc->malloc(alloc, count * width)
        Array ( data = p, capacity = count, size = 0)
    }

    with_capacity(count) {
        // Type argument [T] required because no argument carries the type.
        with_capacity[T](Alloc.context.default, count)
    }
}

// -- Mutating operations --

(self: Ptr[Array[T]]): {
    push(x) {
        self.push(Alloc.context.default, x)
    }

    reserve(count) {
        self.reserve(Alloc.context.default, count)
    }

    free() {
        self.free(Alloc.context.default)
    }

    pop() -> T {
        if self->size == 0 { _tl_fatal_(c"Array.pop: empty array") }
        self->size -= 1
        self->data.[self->size]
    }

    clear() {
        self->size = 0
    }

    insert(index, val: T) {
        self.insert(Alloc.context.default, index, val)
    }

    remove(index) -> T {
        if index >= self->size { _tl_fatal_(c"Array.remove: index out of bounds") }
        val := self->data.[index]
        self->size -= 1
        if index < self->size {
            width       := _type_width_aligned[T]()
            dst: Ptr[T] := self->data.[index].&
            src: Ptr[T] := self->data.[index + 1].&
            c_memmove(dst, src, (self->size - index) * width)
        }
        val
    }

    swap_remove(index) -> T {
        if index >= self->size { _tl_fatal_(c"Array.swap_remove: index out of bounds") }
        val := self->data.[index]
        self->size -= 1
        if index < self->size {
            self->data.[index] = self->data.[self->size]
        }
        val
    }

    resize(count, val: T) {
        self.resize(Alloc.context.default, count, val)
    }

    shrink_to_fit() {
        self.shrink_to_fit[T](Alloc.context.default)
    }

    truncate(count) {
        if count < self->size {
            self->size = count
        }
    }

    append(other) {
        self.append(Alloc.context.default, other)
    }

    sort(cmp: (T, T) -> Int) {
        self.sort(Alloc.context.transient, cmp)
    }

    // -- With allocators --

    push(alloc, x) {
        if _is_full(self) { _grow(self, alloc, self->capacity + 1) }
        self->data.[self->size] = x
        self->size += 1
    }

    reserve(alloc, count) {
        if self->capacity < count {
            p := alloc->realloc(alloc, self->data, count * _type_width_aligned[T]())
            if p == null { _tl_fatal_(c"oom") }
            self->data     = p
            self->capacity = count
        }
    }

    free(alloc) {
        alloc->free(alloc, self->data)
        self->data = null
    }

    insert(alloc, index, val: T) {
        if index > self->size { _tl_fatal_(c"Array.insert: index out of bounds") }
        if _is_full(self) { self._grow(alloc, self->capacity + 1) }
        if index < self->size {
            width       := _type_width_aligned[T]()
            dst: Ptr[T] := self->data.[index + 1].&
            src: Ptr[T] := self->data.[index].&
            c_memmove(dst, src, (self->size - index) * width)
        }
        self->data.[index] = val
        self->size += 1
    }

    resize(alloc, count, val: T) {
        if count > self->size {
            self._grow(alloc, count)
            i := self->size
            while i < count, i += 1 {
                self->data.[i] = val
            }
        }
        self->size = count
    }

    shrink_to_fit(alloc) {
        if self->size < self->capacity {
            if self->size == 0 {
                alloc->free(alloc, self->data)
                self->data     = null
                self->capacity = 0
            } else {
                width := _type_width_aligned[T]()
                p     := alloc->realloc(alloc, self->data, self->size * width)
                if p != null {
                    self->data     = p
                    self->capacity = self->size
                }
            }
        }
    }

    append(alloc, other) {
        if other.size == 0 { return void }
        self._grow(alloc, self->size + other.size)
        width       := _type_width_aligned[T]()
        dst: Ptr[T] := self->data.[self->size].&
        c_memcpy(dst, other.data, other.size * width)
        self->size += other.size
    }

    sort(tmp_alloc, cmp: (T, T) -> Int) {
        n := self->size
        if n <= 1 { return void }
        width       := _type_width_aligned[T]()
        tmp: Ptr[T] := tmp_alloc->malloc(tmp_alloc, n * width)
        if tmp == null { _tl_fatal_(c"Array.sort: oom") }
        w: CSize := 1
        while w < n {
            i: CSize := 0
            while i < n {
                mid := i + w
                if mid > n { mid = n }
                hi := i + w * 2
                if hi > n { hi = n }
                if mid < hi {
                    _merge(self->data, tmp, i, mid, hi, cmp, width)
                }
                i += w * 2
            }
            w *= 2
        }
        tmp_alloc->free(tmp_alloc, tmp)
    }
}

// -- Immutable operations --

(arr: Ptr[Const[Array[T]]]): {
    get(index: CSize) -> T {
        if index >= arr->size { _tl_fatal_(c"Array.get: index out of bounds") }
        arr->data.[index]
    }

    front() -> T {
        if arr->size == 0 { _tl_fatal_(c"Array.front: empty array") }
        arr->data.[0]
    }

    back() -> T {
        if arr->size == 0 { _tl_fatal_(c"Array.back: empty array") }
        arr->data.[arr->size - 1]
    }

    clone() {
        arr.clone(Alloc.context.default)
    }

    copy(start, end) {
        arr.copy(Alloc.context.default, start, end)
    }

    slice(start, end) {
        if end > arr->size || start > end { _tl_fatal_(c"Array.slice: index out of bounds") }
        if start == end { return Slice[T](data: Ptr[T] = null, size = 0) }
        p: Ptr[T] := arr->data.[start].&
        Slice(data = p, size = end - start)
    }

    is_empty() {
        arr->size == 0
    }

    contains(val: T) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            if arr->data.[i] == val { return true }
        }
        false
    }

    index_of(val: T) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            if arr->data.[i] == val { return Some(i) }
        }
        None
    }

    sorted(cmp: (T, T) -> Int) {
        arr.sorted(Alloc.context.default, Alloc.context.transient, cmp)
    }

    reverse() {
        if arr->size == 0 { return void }
        i: CSize := 0
        j        := arr->size - 1
        while i < j, i += 1 {
            arr.swap(i, j)
            j -= 1
        }
    }

    swap(i, j) {
        if i >= arr->size { _tl_fatal_(c"Array.swap: index out of bounds") }
        if j >= arr->size { _tl_fatal_(c"Array.swap: index out of bounds") }
        tmp := arr->data.[i]
        arr->data.[i] = arr->data.[j]
        arr->data.[j] = tmp
    }

    map[T, U](f: (T) -> U) {
        arr.map(Alloc.context.default, f)
    }

    filter(f: (T) -> Bool) {
        arr.filter(Alloc.context.default, f)
    }

    reduce[T, U](init: U, f: (U, T) -> U) {
        acc      := init
        i: CSize := 0
        while i < arr->size, i += 1 {
            acc = f(acc, arr->data.[i])
        }
        acc
    }

    foreach(f: (T) -> Void) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            f(arr->data.[i])
        }
    }

    any_of(f: (T) -> Bool) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            if f(arr->data.[i]) { return true }
        }
        false
    }

    all(f: (T) -> Bool) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            if !f(arr->data.[i]) { return false }
        }
        true
    }

    find(f: (T) -> Bool) {
        i: CSize := 0
        while i < arr->size, i += 1 {
            if f(arr->data.[i]) { return Some(i) }
        }
        None
    }

    // -- With allocators --

    clone(alloc) {
        if arr->size == 0 { return empty[T]() }
        result := with_capacity[T](alloc, arr->size)
        result.size = arr->size
        width := _type_width_aligned[T]()
        c_memcpy(result.data, arr->data, arr->size * width)
        result
    }

    copy(alloc, start, end) {
        if end > arr->size || start > end { _tl_fatal_(c"Array.copy: index out of bounds") }
        len := end - start
        if len == 0 { return empty[T]() }
        result := with_capacity[T](alloc, len)
        result.size = len
        width       := _type_width_aligned[T]()
        src: Ptr[T] := arr->data.[start].&
        c_memcpy(result.data, src, len * width)
        result
    }

    filter(alloc, f: (T) -> Bool) {
        result   := empty[T]()
        i: CSize := 0
        while i < arr->size, i += 1 {
            if f(arr->data.[i]) { push(result.&, alloc, arr->data.[i]) }
        }
        result
    }

    map[T, U](alloc, f: (T) -> U) {
        result := with_capacity[U](alloc, arr->size)
        result.size = arr->size
        i: CSize := 0
        while i < arr->size, i += 1 {
            result.data.[i] = f(arr->data.[i])
        }
        result
    }

    sorted(alloc, tmp_alloc, cmp: (T, T) -> Int) {
        result := arr.clone(alloc)
        sort(result.&, tmp_alloc, cmp)
        result
    }

}

// -- Iterator interface --

Iter[T]: {
    data:  Ptr[T],
    size:  CSize,
    index: CSize,
}

iter_init[T](arr: Ptr[Array[T]]) -> Iter[T]

iter_init(arr) { Iter(data = arr->data, size = arr->size, index = 0) }

(iter: Ptr[Iter[T]]):       {
    iter_ptr   () -> Ptr[T] { iter->data.[iter->index].& }
    iter_value () -> T      { iter->data.[iter->index]   }
    iter_cond  () -> Bool   { iter->index < iter->size   }
    iter_update() -> Void   { iter->index += 1           }
    iter_deinit() -> Void   {                            }
}


// -- Array.Indexed --

#module Array.Indexed

IterValuePtr[T]: {
    ptr:   Ptr[T],
    index: CSize,
}

IterValue[T]: {
    value: T,
    index: CSize,
}

Iter[T]: {
    data:  Ptr[T],
    size:  CSize,
    index: CSize,
    iv:    IterValue[T],
    ivp:   IterValuePtr[T],
}

iter_init[T](arr: Ptr[Array[T]]) { Iter (data = arr->data,
                                         size  = arr->size,
                                         index = 0,
                                         iv    = IterValue (value = void, index = 0),
                                         ivp   = IterValuePtr(ptr = null, index = 0)) }

(iter: Ptr[Iter[T]]):                {
    iter_ptr   () -> IterValuePtr[T] { iter->ivp.ptr = iter->data.[iter->index].& iter->ivp.index = iter->index iter->ivp }
    iter_value () -> IterValue[T]    { iter->iv.value = iter->data.[iter->index] iter->iv.index = iter->index iter->iv    }
    iter_cond  () -> Bool            { iter->index < iter->size                                                           }
    iter_update() -> Void            { iter->index += 1                                                                   }
    iter_deinit() -> Void            {                                                                                    }
}
