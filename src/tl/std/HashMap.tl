// HashMap — Robin Hood open-addressing with backward-shift deletion.
//
// Pure Tess implementation.
#module HashMap
#import <Alloc.tl>
#import <Array.tl>
#import <Unsafe.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <string.tl>

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
    _alloc:    Ptr[Alloc.Allocator],
}

HashMapOpts: {
    capacity: CSize,
}

Entry[K, V]: { key: K, value: V }

// -- Synopsis --

create[K, V] ()                                         -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator])                    -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]]
destroy[K, V](self: Ptr[HashMap[K, V]])                 -> Void

set[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
get[K: HashEq, V]     (self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
get_copy[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Option[V]
contains[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K)           -> Bool
remove[K: HashEq, V]  (self: Ptr[HashMap[K, V]], key: K)           -> Bool

size[K, V]    (self: Ptr[HashMap[K, V]]) -> CSize
is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool
clear[K, V]   (self: Ptr[HashMap[K, V]]) -> Void

keys[K, V]  (self: Ptr[HashMap[K, V]])                        -> Array[K]
keys[K, V]  (self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[K]
values[K, V](self: Ptr[HashMap[K, V]])                        -> Array[V]
values[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[V]


// -- Private helpers --

_is_occupied (meta: CSize)             -> Bool
_get_tag     (meta: CSize)             -> CSize
_get_dist    (meta: CSize)             -> CSize
_make_meta   (tag: CSize, dist: CSize) -> CSize
_hash_tag    (hash: CSize)             -> CSize

_entry_width[K, V]     () -> CSize
_grow[K: HashEq, V]    (self: Ptr[HashMap[K, V]]) -> Void
_alloc_entries[K, V]   (alloc: Ptr[Allocator], capacity: CSize) -> Ptr[_Entry[K, V]]
_insert_entry[K: Eq, V](entries: Ptr[_Entry[K, V]], capacity: CSize,
                        key: K, value: V, hash: CSize) -> CSize


// -- Implementation --

_is_occupied(meta)      { (meta & 1) != 0              }
_get_tag    (meta)      { (meta >> 1) & 0xFF           }
_get_dist   (meta)      { meta >> 9                    }
_make_meta  (tag, dist) { 1 | (tag << 1) | (dist << 9) }
_hash_tag   (hash)      { (hash >> 56) & 0xFF          }

_entry_width[K, V]() -> CSize { Alloc.align(sizeof[_Entry[K, V]](), alignof[_Entry[K, V]]()) }

_alloc_entries[K, V](alloc, capacity) {
    p: Ptr[_Entry[K, V]] := alloc->calloc(alloc, capacity, _entry_width[K, V]())
    if p == null { _tl_fatal_("HashMap: oom") }
    p
}


// -- Create / Destroy --

create[K, V](alloc: Ptr[Allocator], opts: HashMapOpts) -> Ptr[HashMap[K, V]] {
    cap := if opts.capacity == 0 { 16 } else { Alloc.next_power_of_two(opts.capacity) }

    m: Ptr[HashMap[K, V]] := alloc->malloc(alloc, sizeof[HashMap[K, V]]())
    if m == null { _tl_fatal_("HashMap: oom") }

    m->_entries  = _alloc_entries[K, V](alloc, cap)
    m->_capacity = cap
    m->_size     = 0
    m->_alloc    = alloc
    m
}

create[K, V]() -> Ptr[HashMap[K, V]] {
    opts := HashMapOpts(capacity = 0)
    create[K, V](Alloc.context.default, opts)
}

create[K, V](alloc: Ptr[Allocator]) -> Ptr[HashMap[K, V]] {
    opts := HashMapOpts(capacity = 0)
    create[K, V](alloc, opts)
}

destroy[K, V](self: Ptr[HashMap[K, V]]) -> Void {
    alloc := self->_alloc
    alloc->free(alloc, self->_entries)
    alloc->free(alloc, self)
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

_grow[K: HashEq, V](self: Ptr[HashMap[K, V]]) -> Void {
    old_entries  := self->_entries
    old_capacity := self->_capacity
    new_capacity := old_capacity * 2
    alloc        := self->_alloc

    new_entries := _alloc_entries[K, V](alloc, new_capacity)

    i: CSize := 0
    while i < old_capacity, i += 1 {
        e := old_entries.[i].&
        if _is_occupied(e->meta) {
            _insert_entry[K, V](new_entries, new_capacity, e->key, e->value, hash(e->key))
        }
    }

    alloc->free(alloc, old_entries)
    self->_entries  = new_entries
    self->_capacity = new_capacity
}

set[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K, value: V) -> Void {
    hash  := hash(key)
    delta := _insert_entry[K, V](self->_entries, self->_capacity, key, value, hash)
    self->_size += delta

    // Grow after insertion so overwrites (delta=0) don't trigger unnecessary growth.
    if self->_size * 20 >= self->_capacity * 17 {
        _grow[K, V](self)
    }
}

get[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K) -> Ptr[V] {
    if self->_size == 0 { return null }

    hash        := hash(key)
    mask        := self->_capacity - 1
    bucket      := hash & mask
    tag         := _hash_tag(hash)
    dist: CSize := 0

    while true {
        e      := self->_entries.[bucket].&
        e_meta := e->meta

        if !_is_occupied(e_meta) { return null }
        if _get_dist(e_meta) < dist { return null }
        if _get_tag(e_meta) == tag && e->key == key {
            return e->value.&
        }

        dist += 1
        bucket = (bucket + 1) & mask
    }

    null
}

get_copy[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K) -> Option[V] {
    p := get(self, key)
    if p == null { return None }
    Some(p.*)
}

contains[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K) -> Bool {
    get(self, key) != null
}


// -- Remove (backward-shift deletion) --

remove[K: HashEq, V](self: Ptr[HashMap[K, V]], key: K) -> Bool {
    if self->_size == 0 { return false }

    hash        := hash(key)
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


// -- Info --

size[K, V](self: Ptr[HashMap[K, V]]) -> CSize { self->_size }

is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool { self->_size == 0 }

clear[K, V](self: Ptr[HashMap[K, V]]) -> Void {
    c_memset(self->_entries, 0, self->_capacity * _entry_width[K, V]())
    self->_size = 0
}


// -- Bulk --

keys[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[K] {
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

keys[K, V](self: Ptr[HashMap[K, V]]) -> Array[K] {
    keys(self, Alloc.context.default)
}

values[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[V] {
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

values[K, V](self: Ptr[HashMap[K, V]]) -> Array[V] {
    values(self, Alloc.context.default)
}
