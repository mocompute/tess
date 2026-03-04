// HashMap — Robin Hood open-addressing with backward-shift deletion.
//
// Pure Tess implementation; only two small #ifc helpers for byte-level operations.
#module HashMap
#import <Alloc.tl>
#import <Array.tl>
#import <Unsafe.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <fatal.tl>
#import <string.tl>

// -- Aliases --

Allocator = Alloc.Allocator

// -- Types --

_Entry[K, V]: {
    key:    K,
    value:  V,
    hash:   UInt,
    dist:   UInt,
    status: UInt,
}

_OCCUPIED := 1u

HashMap[K, V]: {
    _entries:  Ptr[_Entry[K, V]],
    _capacity: UInt,
    _size:     UInt,
    _alloc:    Ptr[Alloc.Allocator],
    _hash:     (Ptr[K]) -> UInt,
    _eq:       (Ptr[K], Ptr[K]) -> Bool,
}

HashMapOpts[K]: {
    hash:     (Ptr[K]) -> UInt,
    eq:       (Ptr[K], Ptr[K]) -> Bool,
    capacity: UInt,
}

Entry[K, V]: { key: K, value: V }

// -- Synopsis --

create[K, V] ()                                            -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator])                       -> Ptr[HashMap[K, V]]
create[K, V] (alloc: Ptr[Allocator], opts: HashMapOpts[K]) -> Ptr[HashMap[K, V]]
destroy[K, V](self: Ptr[HashMap[K, V]])                    -> Void

set[K, V]     (self: Ptr[HashMap[K, V]], key: K, value: V) -> Void
get[K, V]     (self: Ptr[HashMap[K, V]], key: K)           -> Ptr[V]
get_copy[K, V](self: Ptr[HashMap[K, V]], key: K)           -> Option[V]
contains[K, V](self: Ptr[HashMap[K, V]], key: K)           -> Bool
remove[K, V]  (self: Ptr[HashMap[K, V]], key: K)           -> Bool

size[K, V]    (self: Ptr[HashMap[K, V]]) -> UInt
is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool
clear[K, V]   (self: Ptr[HashMap[K, V]]) -> Void

keys[K, V]  (self: Ptr[HashMap[K, V]])                        -> Array[K]
keys[K, V]  (self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[K]
values[K, V](self: Ptr[HashMap[K, V]])                        -> Array[V]
values[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[V]


// -- C helpers --

#ifc
#include <string.h>

unsigned long long tl_hashmap_byte_at(void *p, size_t i) {
    return ((unsigned char*)p)[i];
}

int tl_hashmap_bytes_equal(void *a, void *b, size_t n) {
    return memcmp(a, b, n) == 0;
}
#endc

c_tl_hashmap_byte_at     (p: Ptr[any], i: CSize)              -> UInt
c_tl_hashmap_bytes_equal (a: Ptr[any], b: Ptr[any], n: CSize) -> CInt


// -- Private helpers --

_default_hash[K]    (key: Ptr[K]) -> UInt
_default_eq[K]      (a: Ptr[K], b: Ptr[K]) -> Bool
_entry_width[K, V]  () -> UInt
_grow[K, V]         (self: Ptr[HashMap[K, V]]) -> Void
_alloc_entries[K, V](alloc: Ptr[Allocator], capacity: UInt) -> Ptr[_Entry[K, V]]
_insert_entry[K, V] (entries: Ptr[_Entry[K, V]], capacity: UInt, eq: (Ptr[K], Ptr[K]) -> Bool,
                    key: K, value: V, hash: UInt) -> UInt


// -- Implementation --

// FNV-1a 64-bit hash over the raw bytes of a key.
_default_hash[K](key: Ptr[K]) -> UInt {
    h:     UInt  := 0xcbf29ce484222325
    prime: UInt  := 0x100000001b3
    n:     CSize := sizeof[K]()
    i:     CSize := 0
    while i < n, i += 1 {
        b := c_tl_hashmap_byte_at(key, i)
        h = h ^ b
        h = h * prime
    }
    h
}

// Byte-level equality of two keys.
_default_eq[K](a: Ptr[K], b: Ptr[K]) -> Bool {
    !!c_tl_hashmap_bytes_equal(a, b, sizeof[K]())
}

_entry_width[K, V]() -> UInt { Alloc.align(sizeof[_Entry[K, V]](), alignof[_Entry[K, V]]()) }

_alloc_entries[K, V](alloc, capacity) {
    p: Ptr[_Entry[K, V]] := alloc->calloc(alloc, capacity, _entry_width[K, V]())
    if p == null { _tl_fatal_(c"HashMap: oom") }
    p
}


// -- Create / Destroy --

create[K, V](alloc: Ptr[Allocator], opts: HashMapOpts[K]) -> Ptr[HashMap[K, V]] {
    cap := if opts.capacity == 0u { 16u } else { Alloc.next_power_of_two(opts.capacity) }

    hash_fn := opts.hash
    eq_fn   := opts.eq
    if hash_fn == null { hash_fn = _default_hash[K]/1 }
    if eq_fn == null { eq_fn     = _default_eq[K]/2 }

    m: Ptr[HashMap[K, V]] := alloc->malloc(alloc, sizeof[HashMap[K, V]]())
    if m == null { _tl_fatal_(c"HashMap: oom") }

    m->_entries  = _alloc_entries[K, V](alloc, cap)
    m->_capacity = cap
    m->_size     = 0u
    m->_alloc    = alloc
    m->_hash     = hash_fn
    m->_eq       = eq_fn
    m
}

create[K, V]() -> Ptr[HashMap[K, V]] {
    opts := HashMapOpts[K](hash = null, eq = null, capacity = 0u)
    create[K, V](Alloc.context.default, opts)
}

create[K, V](alloc: Ptr[Allocator]) -> Ptr[HashMap[K, V]] {
    opts := HashMapOpts[K](hash = null, eq = null, capacity = 0u)
    create[K, V](alloc, opts)
}

destroy[K, V](self: Ptr[HashMap[K, V]]) -> Void {
    alloc := self->_alloc
    alloc->free(alloc, self->_entries)
    alloc->free(alloc, self)
}


// -- Core: set, get, contains --

// Insert into a raw entries table. Returns 1 for new insertion, 0 for overwrite.
_insert_entry[K, V](entries, capacity, eq, key: K, value: V, hash: UInt) -> UInt {
    mask   := capacity - 1u
    bucket := hash & mask

    cur_key        := key
    cur_value      := value
    cur_hash       := hash
    cur_dist: UInt := 0u

    while true {
        e := entries.[bucket].&

        if e->status != _OCCUPIED {
            e->key    = cur_key
            e->value  = cur_value
            e->hash   = cur_hash
            e->dist   = cur_dist
            e->status = _OCCUPIED
            return 1u
        }

        if e->hash == cur_hash && eq(e->key.&, cur_key.&) {
            e->value = cur_value
            return 0u
        }

        // Robin Hood: steal from rich
        if e->dist < cur_dist {
            tmp_key   := e->key
            tmp_value := e->value
            tmp_hash  := e->hash
            tmp_dist  := e->dist

            e->key   = cur_key
            e->value = cur_value
            e->hash  = cur_hash
            e->dist  = cur_dist

            cur_key   = tmp_key
            cur_value = tmp_value
            cur_hash  = tmp_hash
            cur_dist  = tmp_dist
        }

        cur_dist += 1u
        bucket = (bucket + 1u) & mask
    }

    0u
}

_grow[K, V](self: Ptr[HashMap[K, V]]) -> Void {
    old_entries  := self->_entries
    old_capacity := self->_capacity
    new_capacity := old_capacity * 2
    alloc        := self->_alloc

    new_entries := _alloc_entries[K, V](alloc, new_capacity)

    i: UInt := 0u
    while i < old_capacity, i += 1 {
        e := old_entries.[i].&
        if e->status == _OCCUPIED {
            _insert_entry[K, V](new_entries, new_capacity, self->_eq, e->key, e->value, e->hash)
        }
    }

    alloc->free(alloc, old_entries)
    self->_entries  = new_entries
    self->_capacity = new_capacity
}

set[K, V](self: Ptr[HashMap[K, V]], key: K, value: V) -> Void {
    hash  := self->_hash(key.&)
    delta := _insert_entry[K, V](self->_entries, self->_capacity, self->_eq, key, value, hash)
    self->_size += delta

    // Grow after insertion so overwrites (delta=0) don't trigger unnecessary growth.
    if self->_size * 20u >= self->_capacity * 17u {
        _grow[K, V](self)
    }
}

get[K, V](self: Ptr[HashMap[K, V]], key: K) -> Ptr[V] {
    if self->_size == 0u { return null }

    hash       := self->_hash(key.&)
    mask       := self->_capacity - 1
    bucket     := hash & mask
    dist: UInt := 0u

    while true {
        e := self->_entries.[bucket].&

        if e->status != _OCCUPIED { return null }
        if e->dist < dist { return null }
        if e->hash == hash && self->_eq(e->key.&, key.&) {
            return e->value.&
        }

        dist += 1u
        bucket = (bucket + 1u) & mask
    }

    null
}

get_copy[K, V](self: Ptr[HashMap[K, V]], key: K) -> Option[V] {
    p := get(self, key)
    if p == null { return None }
    Some(p.*)
}

contains[K, V](self: Ptr[HashMap[K, V]], key: K) -> Bool {
    get(self, key) != null
}


// -- Remove (backward-shift deletion) --

remove[K, V](self: Ptr[HashMap[K, V]], key: K) -> Bool {
    if self->_size == 0u { return false }

    hash       := self->_hash(key.&)
    mask       := self->_capacity - 1u
    bucket     := hash & mask
    dist: UInt := 0u

    while true {
        e := self->_entries.[bucket].&

        if e->status != _OCCUPIED { return false }
        if e->dist < dist { return false }

        if e->hash == hash && self->_eq(e->key.&, key.&) {
            e->status = 0u
            self->_size -= 1u

            // Backward-shift subsequent entries
            prev := bucket
            bucket = (bucket + 1u) & mask
            while true {
                e = self->_entries.[bucket].&
                if e->status != _OCCUPIED || e->dist == 0u { break }

                self->_entries.[prev] = self->_entries.[bucket]
                self->_entries.[prev].& -> dist -= 1u
                e->status = 0u

                prev   = bucket
                bucket = (bucket + 1u) & mask
            }

            return true
        }

        dist += 1u
        bucket = (bucket + 1u) & mask
    }

    false
}


// -- Info --

size[K, V](self: Ptr[HashMap[K, V]]) -> UInt { self->_size }

is_empty[K, V](self: Ptr[HashMap[K, V]]) -> Bool { self->_size == 0u }

clear[K, V](self: Ptr[HashMap[K, V]]) -> Void {
    c_memset(self->_entries, 0, self->_capacity * _entry_width[K, V]())
    self->_size = 0u
}


// -- Bulk --

keys[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[K] {
    result  := Array.with_capacity[K](alloc, self->_size)
    i: UInt := 0u
    while i < self->_capacity, i += 1 {
        e := self->_entries.[i].&
        if e->status == _OCCUPIED {
            Array.push(result.&, alloc, e->key)
        }
    }
    result
}

keys[K, V](self: Ptr[HashMap[K, V]]) -> Array[K] {
    keys(self, Alloc.context.default)
}

values[K, V](self: Ptr[HashMap[K, V]], alloc: Ptr[Allocator]) -> Array[V] {
    result  := Array.with_capacity[V](alloc, self->_size)
    i: UInt := 0u
    while i < self->_capacity, i += 1 {
        e := self->_entries.[i].&
        if e->status == _OCCUPIED {
            Array.push(result.&, alloc, e->value)
        }
    }
    result
}

values[K, V](self: Ptr[HashMap[K, V]]) -> Array[V] {
    values(self, Alloc.context.default)
}
