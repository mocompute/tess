#module Str
#import <Alloc.tl>
#import <Unsafe.tl>
#import <string.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <fatal.tl>

// Small string optimization: strings of 14 bytes or fewer are stored inline (no heap allocation).
// The Str type is a 16-byte union of _Big (len + pointer) and _Small (15-byte buffer + tag/len byte).
// The 15-byte buffer holds up to 14 bytes of data + 1 byte for a null terminator (written lazily by cstr).
// The tag_len byte (byte 15) packs a 4-bit tag in the upper nibble and 4-bit length in the lower nibble.
// Tag value 0x01 indicates a small string.
// In DEBUG mode, tag value 0x02 marks a string literal (cannot be freed).

_STR_SMALL_TAG: Byte  := 0x01
_STR_MAX_SMALL: CSize := 14

#ifdef DEBUG
_STR_LITERAL_TAG: Byte := 0x02
#endif

// The following are in prelude.tl:
// _Big: {
//     len: CSize,
//     buf: Ptr(CChar),
// }
// _Small: {
//     buf:     CArray(CChar, 15),
//     tag_len: CUnsignedChar,
// }
// Str: { | big: _Big
//        | small: _Small }
//
// from_literal is directly invoked by compiler to generate string literals
// from_literal (s: CString) -> Str


// -- Synopsis --

// Construction.
empty     ()                                               -> Str
from_cstr (alloc: Ptr(Allocator), s: CString)              -> Str
from_bytes(alloc: Ptr(Allocator), s: Ptr(CChar), n: CSize) -> Str
from_bytes(s: Ptr(CChar), n: CSize)                        -> Str
copy      (alloc: Ptr(Allocator), s: Str)                  -> Str
copy      (s: Str)                                         -> Str
from_int  (alloc: Ptr(Allocator), n: Int)                  -> Str
from_int  (n: Int)                                         -> Str
from_float(alloc: Ptr(Allocator), f: Float)                -> Str
from_float(f: Float)                                       -> Str

// Queries.
len       (s: Str)               -> CSize
is_empty  (s: Str)               -> Bool
_is_small (s: Str)               -> Bool
byte_at   (s: Str, index: CSize) -> Byte
cstr      (s: Ptr(Str))          -> CString

// Comparison.
eq           (a: Str, b: Str)             -> Bool
cmp          (a: Str, b: Str)             -> Int
starts_with  (s: Str, prefix: Str)        -> Bool
ends_with    (s: Str, suffix: Str)        -> Bool
contains     (haystack: Str, needle: Str) -> Bool
contains_byte(s: Str, b: Byte)            -> Bool

// Concatenation.
cat   (alloc: Ptr(Allocator), a: Str, b: Str)                 -> Str
cat   (a: Str, b: Str)                                        -> Str
cat_3 (alloc: Ptr(Allocator), a: Str, b: Str, c: Str)         -> Str
cat_3 (a: Str, b: Str, c: Str)                                -> Str
cat_4 (alloc: Ptr(Allocator), a: Str, b: Str, c: Str, d: Str) -> Str
cat_4 (a: Str, b: Str, c: Str, d: Str)                        -> Str
push  (self: Ptr(Str), alloc: Ptr(Allocator), other: Str)     -> Void
push  (self: Ptr(Str), other: Str)                            -> Void

// Slicing.
slice (alloc: Ptr(Allocator), s: Str, start: CSize, end: CSize) -> Str
slice (s: Str, start: CSize, end: CSize)                        -> Str

// Search.
index_of_byte(s: Str, b: Byte) -> Int

// Mutation.
replace_byte(alloc: Ptr(Allocator), s: Str, find: Byte, replace: Byte) -> Str
replace_byte(s: Str, find: Byte, replace: Byte)                        -> Str

// Memory.
free(self: Ptr(Str), alloc: Ptr(Allocator)) -> Void
free(self: Ptr(Str))                        -> Void


// -- Implementation --

// -- Private helpers --

_is_small(s: Str) -> Bool {
    (s.small.tag_len & _STR_SMALL_TAG) == _STR_SMALL_TAG
}

_small_len(s: Str) -> CSize {
    (s.small.tag_len & 0xF0) >> 4
}

_buf(s: Ptr(Str)) -> Ptr(CChar) {
    return if _is_small(s.*) { s->small.buf } else { s->big.buf }
}

_make_tag_len(n: CSize) -> Byte {
    ((n & 0x0F) << 4) | _STR_SMALL_TAG
}


#ifdef DEBUG

// -- Debug: literal string tracking --
// Small literals are tagged in tag_len (bit 1).
// Big literals are tracked in a C-level pointer set.

#ifc
static struct {
    void  **keys;
    size_t  cap;
    size_t  len;
} tl_str_literal_set;

static void tl_str_literal_set_add(void *p) {
    if (!tl_str_literal_set.keys) {
        tl_str_literal_set.cap  = 256;
        tl_str_literal_set.keys = (void **)calloc(256, sizeof(void *));
    }
    if (tl_str_literal_set.len * 2 >= tl_str_literal_set.cap) {
        size_t  old_cap  = tl_str_literal_set.cap;
        void  **old_keys = tl_str_literal_set.keys;
        size_t  new_cap  = old_cap * 2;
        void  **new_keys = (void **)calloc(new_cap, sizeof(void *));
        for (size_t i = 0; i < old_cap; i++) {
            if (old_keys[i]) {
                size_t h = (size_t)old_keys[i] * 2654435761u;
                h &= new_cap - 1;
                while (new_keys[h]) h = (h + 1) & (new_cap - 1);
                new_keys[h] = old_keys[i];
            }
        }
        free(old_keys);
        tl_str_literal_set.keys = new_keys;
        tl_str_literal_set.cap  = new_cap;
    }
    size_t h = (size_t)p * 2654435761u;
    h &= tl_str_literal_set.cap - 1;
    while (tl_str_literal_set.keys[h]) {
        if (tl_str_literal_set.keys[h] == p) return;
        h = (h + 1) & (tl_str_literal_set.cap - 1);
    }
    tl_str_literal_set.keys[h] = p;
    tl_str_literal_set.len++;
}

static int tl_str_literal_set_has(void *p) {
    if (!tl_str_literal_set.keys || !p) return 0;
    size_t h = (size_t)p * 2654435761u;
    h &= tl_str_literal_set.cap - 1;
    while (tl_str_literal_set.keys[h]) {
        if (tl_str_literal_set.keys[h] == p) return 1;
        h = (h + 1) & (tl_str_literal_set.cap - 1);
    }
    return 0;
}
#endc

c_tl_str_literal_set_add(p: Ptr(CChar)) -> Void
c_tl_str_literal_set_has(p: Ptr(CChar)) -> Bool

_mark_literal(s: Ptr(Str)) {
    if _is_small(s.*) {
        s->small.tag_len = s->small.tag_len | _STR_LITERAL_TAG
    } else {
        c_tl_str_literal_set_add(s->big.buf)
    }
}

_is_literal(s: Str) -> Bool {
    return if _is_small(s) {
        (s.small.tag_len & _STR_LITERAL_TAG) != 0
    } else {
        c_tl_str_literal_set_has(s.big.buf)
    }
}

#endif


_init_small(src: Ptr(CChar), n: CSize) -> Str {
    s := Str(small = _Small(buf = void, tag_len = _make_tag_len(n)))
    if n > 0 {
        dst: Ptr(CChar) := s.small.buf
        c_memcpy(dst, src, n)
    }
    s
}

_init_big(alloc: Ptr(Allocator), src: Ptr(CChar), n: CSize) -> Str {
    buf: Ptr(CChar) := alloc->malloc(alloc, n + 1)
    if buf == null { _tl_fatal_(c"Str: out of memory") }
    if n > 0 { c_memcpy(buf, src, n) }
    Str(big = _Big(len = n, buf = buf))
}


// -- Construction --

empty() -> Str {
    Str(small = _Small(buf = void, tag_len = _make_tag_len(0)))
}

from_bytes(alloc, s: Ptr(CChar), n: CSize) -> Str {
    return
    if n <= _STR_MAX_SMALL {
        _init_small(s, n)
    }
    else {
        _init_big(alloc, s, n)
    }
}

from_bytes(s: Ptr(CChar), n: CSize) -> Str {
    from_bytes(Alloc.context.default, s, n)
}

from_cstr(alloc, s: CString) -> Str {
    n := c_strlen(s)
    from_bytes(alloc, s, n)
}

from_cstr(s: CString) -> Str {
    from_cstr(Alloc.context.default, s)
}

#ifdef DEBUG
from_literal(s: CString) -> Str {
    r := from_cstr(Alloc.context.string_literals, s)
    _mark_literal(r.&)
    r
}
#endif

#ifndef DEBUG
from_literal(s: CString) -> Str {
    from_cstr(Alloc.context.string_literals, s)
}
#endif

copy(alloc, s: Str) -> Str {
    n := len(s)
    p := _buf(s.&)
    from_bytes(alloc, p, n)
}

copy(s: Str) -> Str {
    copy(Alloc.context.default, s)
}

#ifc
static long long tl_string_snprintf_int(char *buf, size_t sz, long long n) {
    return snprintf(buf, sz, "%lld", n);
}
static long long tl_string_snprintf_float(char *buf, size_t sz, double f) {
    return snprintf(buf, sz, "%g", f);
}
#endc

c_tl_string_snprintf_int  (buf: Ptr(CChar), sz: CSize, n: Int)   -> Int
c_tl_string_snprintf_float(buf: Ptr(CChar), sz: CSize, f: Float) -> Int

from_int(alloc, n: Int) -> Str {
    buf: CArray(CChar, 32) := void
    ptr: Ptr(CChar)        := buf
    written                := c_tl_string_snprintf_int(ptr, 32, n)
    from_bytes(alloc, ptr, written)
}

from_int(n: Int) -> Str {
    from_int(Alloc.context.default, n)
}

from_float(alloc, f: Float) -> Str {
    buf: CArray(CChar, 32) := void
    ptr: Ptr(CChar)        := buf
    written                := c_tl_string_snprintf_float(ptr, 32, f)
    from_bytes(alloc, ptr, written)
}

from_float(f: Float) -> Str {
    from_float(Alloc.context.default, f)
}


// -- Queries --

len(s: Str) -> CSize {
    return if _is_small(s) { _small_len(s) } else { s.big.len }
}

is_empty(s: Str) -> Bool {
    len(s) == 0
}

byte_at(s: Str, index: CSize) -> Byte {
    n := len(s)
    if index >= n { _tl_fatal_(c"Str.byte_at: index out of bounds") }
    p := _buf(s.&)
    p[index]
}

cstr(s: Ptr(Str)) -> CString {
    n := len(s.*)
    p := _buf(s)
    p[n] = 0
    cs: CString := p
    cs
}


// -- Comparison --

eq(a: Str, b: Str) -> Bool {
    0 == cmp(a, b)
}

cmp(a: Str, b: Str) -> Int {
    la      := len(a)
    lb      := len(b)
    pa      := _buf(a.&)
    pb      := _buf(b.&)
    min_len := if la < lb { la } else { lb }
    if min_len > 0 {
        r := c_memcmp(pa, pb, min_len)
        if r != 0 { return r }
    }
    return if la < lb { - 1 } else if la > lb { 1 } else { 0 }
}

starts_with(s: Str, prefix: Str) -> Bool {
    sn := len(s)
    pn := len(prefix)
    if pn > sn { return 0 }
    if pn == 0 { return 1 }
    sp := _buf(s.&)
    pp := _buf(prefix.&)
    c_memcmp(sp, pp, pn) == 0
}

ends_with(s: Str, suffix: Str) -> Bool {
    sn := len(s)
    xn := len(suffix)
    if xn > sn { return 0 }
    if xn == 0 { return 1 }
    sp               := _buf(s.&)
    tail: Ptr(CChar) := Unsafe.pointer_add(sp, sn - xn)
    xp               := _buf(suffix.&)
    c_memcmp(tail, xp, xn) == 0
}

contains(haystack: Str, needle: Str) -> Bool {
    hn := len(haystack)
    nn := len(needle)
    if nn == 0 { return 1 }
    if nn > hn { return 0 }
    hp       := _buf(haystack.&)
    np       := _buf(needle.&)
    i: CSize := 0
    while i <= hn - nn, i += 1 {
        candidate: Ptr(CChar) := Unsafe.pointer_add(hp, i)
        if c_memcmp(candidate, np, nn) == 0 { return 1 }
    }
    0
}

contains_byte(s: Str, b: Byte) -> Bool {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        if sp[i] == b { return 1 }
    }
    0
}


// -- Concatenation --

_cat_impl(alloc: Ptr(Allocator), bufs: Ptr(Ptr(CChar)), lens: Ptr(CSize), count: CSize) -> Str {
    total: CSize := 0
    i:     CSize := 0
    while i < count, i += 1 { total = total + lens[i] }

    if total <= _STR_MAX_SMALL {
        s               := Str(small = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr(CChar) := s.small.buf
        off: CSize      := 0
        j:   CSize      := 0
        while j < count, j += 1 {
            if lens[j] > 0 {
                d: Ptr(CChar) := Unsafe.pointer_add(dst, off)
                c_memcpy(d, bufs[j], lens[j])
            }
            off = off + lens[j]
        }
        return s
    }

    buf: Ptr(CChar) := alloc->malloc(alloc, total + 1)
    if buf == null { _tl_fatal_(c"Str: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < count, j += 1 {
        if lens[j] > 0 {
            d: Ptr(CChar) := Unsafe.pointer_add(buf, off)
            c_memcpy(d, bufs[j], lens[j])
        }
        off = off + lens[j]
    }
    Str(big = _Big(len = total, buf = buf))
}

cat(alloc, a: Str, b: Str) -> Str {
    bufs: CArray(Ptr(CChar), 2) := void
    lens: CArray(CSize, 2)      := void
    bufs[0] = _buf(a.&) lens[0] = len(a)
    bufs[1] = _buf(b.&) lens[1] = len(b)
    bp: Ptr(Ptr(CChar)) := bufs
    lp: Ptr(CSize)      := lens
    _cat_impl(alloc, bp, lp, 2)
}

cat(a: Str, b: Str) -> Str {
    cat(Alloc.context.default, a, b)
}

cat_3(alloc, a: Str, b: Str, c: Str) -> Str {
    bufs: CArray(Ptr(CChar), 3) := void
    lens: CArray(CSize, 3)      := void
    bufs[0] = _buf(a.&) lens[0] = len(a)
    bufs[1] = _buf(b.&) lens[1] = len(b)
    bufs[2] = _buf(c.&) lens[2] = len(c)
    bp: Ptr(Ptr(CChar)) := bufs
    lp: Ptr(CSize)      := lens
    _cat_impl(alloc, bp, lp, 3)
}

cat_3(a: Str, b: Str, c: Str) -> Str {
    cat_3(Alloc.context.default, a, b, c)
}

cat_4(alloc, a: Str, b: Str, c: Str, d: Str) -> Str {
    bufs: CArray(Ptr(CChar), 4) := void
    lens: CArray(CSize, 4)      := void
    bufs[0] = _buf(a.&) lens[0] = len(a)
    bufs[1] = _buf(b.&) lens[1] = len(b)
    bufs[2] = _buf(c.&) lens[2] = len(c)
    bufs[3] = _buf(d.&) lens[3] = len(d)
    bp: Ptr(Ptr(CChar)) := bufs
    lp: Ptr(CSize)      := lens
    _cat_impl(alloc, bp, lp, 4)
}

cat_4(a: Str, b: Str, c: Str, d: Str) -> Str {
    cat_4(Alloc.context.default, a, b, c, d)
}

#ifdef DEBUG
push(self: Ptr(Str), alloc, other: Str) {
    if _is_literal(self.*) {
        _tl_fatal_(c"Str.push: cannot push to a string literal")
    }
    result := cat(alloc, self.*, other)
    free(self, alloc)
    self.* = result
}
#endif

#ifndef DEBUG
push(self: Ptr(Str), alloc, other: Str) {
    result := cat(alloc, self.*, other)
    free(self, alloc)
    self.* = result
}
#endif

push(self: Ptr(Str), other: Str) {
    push(self, Alloc.context.default, other)
}


// -- Slicing --

slice(alloc, s: Str, start: CSize, end: CSize) -> Str {
    n := len(s)
    if start > end || end > n { _tl_fatal_(c"Str.slice: index out of bounds") }
    slen := end - start
    if slen == 0 { return empty() }
    sp              := _buf(s.&)
    src: Ptr(CChar) := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, slen)
}

slice(s: Str, start: CSize, end: CSize) -> Str {
    slice(Alloc.context.default, s, start, end)
}


// -- Search --

index_of_byte(s: Str, b: Byte) -> Int {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        if sp[i] == b { return i }
    }
    -1
}


// -- Mutation --

replace_byte(alloc, s: Str, find: Byte, replace: Byte) -> Str {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0
    while i < n, i += 1 {
        if rp[i] == find { rp[i] = replace }
    }
    result
}

replace_byte(s: Str, find: Byte, replace: Byte) -> Str {
    replace_byte(Alloc.context.default, s, find, replace)
}


// -- Memory --

#ifdef DEBUG
free(self: Ptr(Str), alloc) {
    if _is_literal(self.*) {
        _tl_fatal_(c"Str.free: cannot free a string literal")
    }
    if !_is_small(self.*) {
        alloc->free(alloc, self->big.buf)
    }
    self.* = empty()
}
#endif

#ifndef DEBUG
free(self: Ptr(Str), alloc) {
    if !_is_small(self.*) {
        alloc->free(alloc, self->big.buf)
    }
    self.* = empty()
}
#endif

free(self: Ptr(Str)) {
    free(self, Alloc.context.default)
}
