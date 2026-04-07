// HashMap — Robin Hood open-addressing with backward-shift deletion.
//
// Pure Tess implementation.
#module HashMap
#import <Alloc.tl>
#import <Array.tl>
#import <Unsafe.tl>
#import <cstdlib.tl>
#import <cstdio.tl>
#import <cstring.tl>

// -- Aliases --

Allocator = Alloc.Allocator

// -- Combined trait --

HashEq[K]: Hash[K], Eq[K] { }

// -- Types --

_Entry[K, V]: {
    key:   K,
    value: V,
    meta:  CSize,                       // bit 0: occupied, bits 1-8: hash tag, bits 9+: probe distance
}

HashMap[K, V]: {
    _entries:  Ptr[_Entry[K, V]],
    _capacity: CSize,
    _size:     CSize,
    _alloc:    Ptr[Allocator],
}

HashMapOpts: {
    capacity: CSize,
}

Entry[K, V]: { key: K, value: V }

// -- Synopsis --

[K, V](): {
    create[K, V]()                                         -> Ptr[HashMap[K, V]]
    create[K, V](alloc: Ptr[Allocator])                    -> Ptr[HashMap[K, V]]
    create[K, V](alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]]
}

(self: Ptr[HashMap[K: HashEq, V]]): {
    destroy() -> Void

    set     (key: K, value: V) -> Void
    get     (key: K)           -> Option[Ptr[V]]
    get_copy(key: K)           -> Option[V]
    contains(key: K)           -> Bool
    remove  (key: K)           -> Bool

    size    () -> CSize
    is_empty() -> Bool
    clear   () -> Void

    keys  ()                      -> Array[K]
    values()                      -> Array[V]
    values(alloc: Ptr[Allocator]) -> Array[V]
    keys  (alloc: Ptr[Allocator]) -> Array[K]
}


// -- Implementation --

_is_occupied(meta)               { (meta & 1) != 0              }
_get_tag    (meta) -> CSize      { (meta >> 1) & 0xFF           }
_get_dist   (meta) -> CSize      { meta >> 9                    }
_make_meta  (tag, dist) -> CSize { 1 | (tag << 1) | (dist << 9) }
_hash_tag   (hash) -> CSize      { (hash >> 56) & 0xFF          }

_entry_width[K, V]() -> CSize { Alloc.align(sizeof[_Entry[K, V]](), alignof[_Entry[K, V]]()) }

_alloc_entries[K, V](alloc, capacity) {
    p: Ptr[_Entry[K, V]] := alloc.clear(capacity, _entry_width[K, V]())
    if p == null { _tl_fatal_(c"HashMap: oom") }
    p
}


// -- Create / Destroy --

[K, V](): {
    create() -> Ptr[HashMap[K, V]] {
        opts := HashMapOpts(capacity = 0)
        create[K, V](Alloc.context.default, opts)
    }

    create(alloc: Ptr[Allocator]) -> Ptr[HashMap[K, V]] {
        opts := HashMapOpts(capacity = 0)
        create[K, V](alloc, opts)
    }

    create(alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]] {
        cap := if opts.capacity == 0 { 16 } else { Alloc.next_power_of_two(opts.capacity) }

        m: Ptr[HashMap[K, V]] := alloc.alloc(sizeof[HashMap[K, V]]())
        if m == null { _tl_fatal_(c"HashMap: oom") }

        m->_entries  = _alloc_entries[K, V](alloc, cap)
        m->_capacity = cap
        m->_size     = 0
        m->_alloc    = alloc
        m
    }
}

(self: Ptr[HashMap[K: HashEq, V]]): {
    destroy() {
        alloc := self->_alloc
        alloc.dealloc(self->_entries)
        alloc.dealloc(self)
    }
}


// -- Core: set, get, contains --

// Insert into a raw entries table. Returns 1 for new insertion, 0 for overwrite.
_insert_entry[K: Eq, V](entries, capacity, key: K, value: V, hash: CSize) -> CSize {
    mask   := capacity - 1
    bucket := hash & mask

    cur_key         := key
    cur_value       := value
    cur_tag         := _hash_tag(hash)
    cur_dist: CSize := 0

    while true {
        e      := entries.[bucket].&
        e_meta := e->meta

        if !_is_occupied(e_meta) {
            e->key   = cur_key
            e->value = cur_value
            e->meta  = _make_meta(cur_tag, cur_dist)
            return 1
        }

        if _get_tag(e_meta) == cur_tag && e->key == cur_key {
            e->value = cur_value
            return 0
        }

        // Robin Hood: steal from rich
        if _get_dist(e_meta) < cur_dist {
            tmp_key   := e->key
            tmp_value := e->value
            tmp_tag   := _get_tag(e_meta)
            tmp_dist  := _get_dist(e_meta)

            e->key   = cur_key
            e->value = cur_value
            e->meta  = _make_meta(cur_tag, cur_dist)

            cur_key   = tmp_key
            cur_value = tmp_value
            cur_tag   = tmp_tag
            cur_dist  = tmp_dist
        }

        cur_dist += 1
        bucket = (bucket + 1) & mask
    }

    0
}


(self: Ptr[HashMap[K: HashEq, V]]): {

    set(key, value) {
        hash  := key.hash()
        delta := _insert_entry[K, V](self->_entries, self->_capacity, key, value, hash)
        self->_size += delta

        // Grow after insertion so overwrites (delta=0) don't trigger unnecessary growth.
        if self->_size * 20 >= self->_capacity * 17 {
            _grow[K, V](self)
        }
    }

    get(key) {
        if self->_size == 0 { return None }

        hash        := key.hash()
        mask        := self->_capacity - 1
        bucket      := hash & mask
        tag         := _hash_tag(hash)
        dist: CSize := 0

        while true {
            e      := self->_entries.[bucket].&
            e_meta := e->meta

            if !_is_occupied(e_meta) { return None }
            if _get_dist(e_meta) < dist { return None }
            if _get_tag(e_meta) == tag && e->key == key {
                return Some(e->value.&)
            }

            dist += 1
            bucket = (bucket + 1) & mask
        }

        None
    }

    get_copy(key) {
        p := try get(self, key)
        Some(p.*)
    }

    contains(key) {
        if _: Some := get(self, key) {true} else {false}
    }

    // -- Remove (backward-shift deletion) --

    remove(key) {
        if self->_size == 0 { return false }

        hash        := key.hash()
        mask        := self->_capacity - 1
        bucket      := hash & mask
        tag         := _hash_tag(hash)
        dist: CSize := 0

        while true {
            e      := self->_entries.[bucket].&
            e_meta := e->meta

            if !_is_occupied(e_meta) { return false }
            if _get_dist(e_meta) < dist { return false }

            if _get_tag(e_meta) == tag && e->key == key {
                e->meta = 0
                self->_size -= 1

                // Backward-shift subsequent entries
                prev := bucket
                bucket = (bucket + 1) & mask
                while true {
                    e = self->_entries.[bucket].&
                    if !_is_occupied(e->meta) || _get_dist(e->meta) == 0 { break }

                    self->_entries.[prev] = self->_entries.[bucket]
                    self->_entries.[prev].& -> meta -= (1 << 9)
                    e->meta = 0

                    prev   = bucket
                    bucket = (bucket + 1) & mask
                }

                return true
            }

            dist += 1
            bucket = (bucket + 1) & mask
        }

        false
    }

    size    () { self->_size      }
    is_empty() { self->_size == 0 }

    clear() {
        c_memset(self->_entries, 0, self->_capacity * _entry_width[K, V]())
        self->_size = 0
    }

    keys(alloc) {
        result   := Array.with_capacity[K](alloc, self->_size)
        i: CSize := 0
        while i < self->_capacity, i += 1 {
            e := self->_entries.[i].&
            if _is_occupied(e->meta) {
                Array.push(result.&, alloc, e->key)
            }
        }
        result
    }

    keys() {
        keys(self, Alloc.context.default)
    }

    values(alloc) {
        result   := Array.with_capacity[V](alloc, self->_size)
        i: CSize := 0
        while i < self->_capacity, i += 1 {
            e := self->_entries.[i].&
            if _is_occupied(e->meta) {
                Array.push(result.&, alloc, e->value)
            }
        }
        result
    }

    values() {
        values(self, Alloc.context.default)
    }


    _grow() -> Void {
        old_entries  := self->_entries
        old_capacity := self->_capacity
        new_capacity := old_capacity * 2
        alloc        := self->_alloc

        new_entries := _alloc_entries[K, V](alloc, new_capacity)

        i: CSize := 0
        while i < old_capacity, i += 1 {
            e := old_entries.[i].&
            if _is_occupied(e->meta) {
                _insert_entry[K, V](new_entries, new_capacity, e->key, e->value, e->key.hash())
            }
        }

        alloc.dealloc(old_entries)
        self->_entries  = new_entries
        self->_capacity = new_capacity
    }
}


// -- Iterator interface --


IterEntry[K, V]: {
    key:   K,
    value: V,
}

Iter[K, V]: {
    data:     Ptr[_Entry[K, V]],
    capacity: CSize,
    index:    CSize,
}

iter_init (map) { Iter(data = map->_entries, capacity = map->_capacity, index = 0) }

(iter: Ptr[Iter[K, V]]): {
    // mutable iterator is over values only, not keys
    iter_ptr () -> Ptr[V] { iter->data.[iter->index].value.& }

    iter_value () -> IterEntry[K, V] {
        entry_p := iter->data.[iter->index].&
        IterEntry(key = entry_p->key, value = entry_p->value)
    }

    iter_cond () -> Bool {
        // Skip unoccupied slots in the open-addressing table.
        while iter->index < iter->capacity, iter->index += 1 {
            if _is_occupied(iter->data.[iter->index].meta) { return true }
        }
        false
    }

    iter_update() -> Void { iter->index += 1 }
    iter_deinit() -> Void {                  }
}
