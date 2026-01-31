#module String
#import <Alloc.tl>
#import <Unsafe.tl>
#import <string.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <fatal.tl>

// Small string optimization: strings of 14 bytes or fewer are stored inline (no heap allocation).
// The String type is a 16-byte union of _Big (len + pointer) and _Small (15-byte buffer + tag/len byte).
// The 15-byte buffer holds up to 14 bytes of data + 1 byte for a null terminator (written lazily by cstr).
// The tag_len byte (byte 15) packs a 4-bit tag in the upper nibble and 4-bit length in the lower nibble.
// Tag value 0x01 indicates a small string; big strings have tag_len determined by the pointer value,
// which in practice never has its upper nibble equal to 1.

_STR_SMALL_TAG: Byte  := 0x01
_STR_MAX_SMALL: CSize := 14

_Big: {
    len: CSize,
    buf: Ptr(CChar),
}

_Small: {
    buf:     CArray(CChar, 15),
    tag_len: Byte,
}

String: { | big: _Big | small: _Small }

// -- Synopsis --

// Construction.
empty     ()                                               -> String
from_cstr (alloc: Ptr(Allocator), s: CString)              -> String
from_cstr (s: CString)                                     -> String
from_bytes(alloc: Ptr(Allocator), s: Ptr(CChar), n: CSize) -> String
from_bytes(s: Ptr(CChar), n: CSize)                        -> String
copy      (alloc: Ptr(Allocator), s: String)               -> String
copy      (s: String)                                      -> String
from_int  (alloc: Ptr(Allocator), n: Int)                  -> String
from_int  (n: Int)                                         -> String
from_float(alloc: Ptr(Allocator), f: Float)                -> String
from_float(f: Float)                                       -> String

// Queries.
len       (s: String)               -> CSize
is_empty  (s: String)               -> Bool
_is_small (s: String)               -> Bool
byte_at   (s: String, index: CSize) -> Byte
cstr      (s: Ptr(String))          -> CString

// Comparison.
eq           (a: String, b: String)             -> Bool
cmp          (a: String, b: String)             -> Int
starts_with  (s: String, prefix: String)        -> Bool
ends_with    (s: String, suffix: String)        -> Bool
contains     (haystack: String, needle: String) -> Bool
contains_byte(s: String, b: Byte)               -> Bool

// Concatenation.
cat   (alloc: Ptr(Allocator), a: String, b: String)                       -> String
cat   (a: String, b: String)                                              -> String
cat_3 (alloc: Ptr(Allocator), a: String, b: String, c: String)            -> String
cat_3 (a: String, b: String, c: String)                                   -> String
cat_4 (alloc: Ptr(Allocator), a: String, b: String, c: String, d: String) -> String
cat_4 (a: String, b: String, c: String, d: String)                        -> String
push  (self: Ptr(String), alloc: Ptr(Allocator), other: String)           -> Void
push  (self: Ptr(String), other: String)                                  -> Void

// Slicing.
slice (alloc: Ptr(Allocator), s: String, start: CSize, end: CSize) -> String
slice (s: String, start: CSize, end: CSize)                        -> String

// Search.
index_of_byte(s: String, b: Byte) -> Int

// Mutation.
replace_byte(alloc: Ptr(Allocator), s: String, find: Byte, replace: Byte) -> String
replace_byte(s: String, find: Byte, replace: Byte)                        -> String

// Memory.
free(self: Ptr(String), alloc: Ptr(Allocator)) -> Void
free(self: Ptr(String))                        -> Void


// -- Implementation --

// -- Private helpers --

_is_small(s: String) -> Bool {
    (s.small.tag_len & 0x0F) == _STR_SMALL_TAG
}

_small_len(s: String) -> CSize {
    (s.small.tag_len & 0xF0) >> 4
}

_buf(s: Ptr(String)) -> Ptr(CChar) {
    if _is_small(s.*) {
        // FIXME: BUG WORKAROUND: Accessing a CArray field in a struct (e.g. s->small.buf) first copies the
        // enclosing struct into a temporary, then decays the CArray to a pointer into that temporary.
        // The returned pointer is dangling because the temporary is destroyed at the end of the
        // expression. This is a compiler bug -- CArray struct field access should decay to a pointer
        // into the original struct, not a copy.
        //
        // Workaround: since the buffer is at offset 0 of both _Small and the String union, we cast
        // the String pointer directly to Ptr(CChar) via Ptr(any).
        p: Ptr(any)   := s
        r: Ptr(CChar) := p
        return r
    }
    s->big.buf
}

_make_tag_len(n: CSize) -> Byte {
    ((n & 0x0F) << 4) | _STR_SMALL_TAG
}

_init_small(src: Ptr(CChar), n: CSize) -> String {
    s := String(small = _Small(buf = void, tag_len = _make_tag_len(n)))
    if n > 0 {
        // FIXME: BUG WORKAROUND: Cannot use s.small.buf here -- see _buf() for explanation.
        // The buffer is at offset 0 of the String union, so we cast s.& directly.
        p:   Ptr(any)   := s.&
        dst: Ptr(CChar) := p
        c_memcpy(dst, src, n)
    }
    s
}

_init_big(alloc: Ptr(Allocator), src: Ptr(CChar), n: CSize) -> String {
    buf: Ptr(CChar) := alloc->malloc(alloc, n + 1)
    if buf == null { _tl_fatal_("String: out of memory") }
    if n > 0 { c_memcpy(buf, src, n) }
    String(big = _Big(len = n, buf = buf))
}


// -- Construction --

empty() -> String {
    String(small = _Small(buf = void, tag_len = _make_tag_len(0)))
}

from_bytes(alloc, s: Ptr(CChar), n: CSize) -> String {
    if n <= _STR_MAX_SMALL { return _init_small(s, n) }
    _init_big(alloc, s, n)
}

from_bytes(s: Ptr(CChar), n: CSize) -> String {
    from_bytes(Alloc.context.managed, s, n)
}

from_cstr(alloc, s: CString) -> String {
    n := c_strlen(s)
    from_bytes(alloc, s, n)
}

from_cstr(s: CString) -> String {
    from_cstr(Alloc.context.managed, s)
}

copy(alloc, s: String) -> String {
    n := len(s)
    p := _buf(s.&)
    from_bytes(alloc, p, n)
}

copy(s: String) -> String {
    copy(Alloc.context.managed, s)
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

from_int(alloc, n: Int) -> String {
    buf: CArray(CChar, 32) := void
    ptr: Ptr(CChar)        := buf
    written                := c_tl_string_snprintf_int(ptr, 32, n)
    from_bytes(alloc, ptr, written)
}

from_int(n: Int) -> String {
    from_int(Alloc.context.managed, n)
}

from_float(alloc, f: Float) -> String {
    buf: CArray(CChar, 32) := void
    ptr: Ptr(CChar)        := buf
    written                := c_tl_string_snprintf_float(ptr, 32, f)
    from_bytes(alloc, ptr, written)
}

from_float(f: Float) -> String {
    from_float(Alloc.context.managed, f)
}


// -- Queries --

len(s: String) -> CSize {
    if _is_small(s) { return _small_len(s) }
    s.big.len
}

is_empty(s: String) -> Bool {
    len(s) == 0
}

byte_at(s: String, index: CSize) -> Byte {
    n := len(s)
    if index >= n { _tl_fatal_("String.byte_at: index out of bounds") }
    p := _buf(s.&)
    p[index]
}

cstr(s: Ptr(String)) -> CString {
    n := len(s.*)
    p := _buf(s)
    p[n] = 0
    cs: CString := p
    cs
}


// -- Comparison --

eq(a: String, b: String) -> Bool {
    la := len(a)
    lb := len(b)
    if la != lb { return 0 }
    if la == 0 { return 1 }
    pa := _buf(a.&)
    pb := _buf(b.&)
    c_memcmp(pa, pb, la) == 0
}

cmp(a: String, b: String) -> Int {
    la      := len(a)
    lb      := len(b)
    pa      := _buf(a.&)
    pb      := _buf(b.&)
    min_len := if la < lb { la } else { lb }
    if min_len > 0 {
        r: CInt := c_memcmp(pa, pb, min_len)
        if r != 0 { return r }
    }
    if la < lb { return - 1 }
    if la > lb { return 1 }
    0
}

starts_with(s: String, prefix: String) -> Bool {
    sn := len(s)
    pn := len(prefix)
    if pn > sn { return 0 }
    if pn == 0 { return 1 }
    sp := _buf(s.&)
    pp := _buf(prefix.&)
    c_memcmp(sp, pp, pn) == 0
}

ends_with(s: String, suffix: String) -> Bool {
    sn := len(s)
    xn := len(suffix)
    if xn > sn { return 0 }
    if xn == 0 { return 1 }
    sp               := _buf(s.&)
    tail: Ptr(CChar) := Unsafe.pointer_add(sp, sn - xn)
    xp               := _buf(suffix.&)
    c_memcmp(tail, xp, xn) == 0
}

contains(haystack: String, needle: String) -> Bool {
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

contains_byte(s: String, b: Byte) -> Bool {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        if sp[i] == b { return 1 }
    }
    0
}


// -- Concatenation --

_cat_impl(alloc: Ptr(Allocator), bufs: Ptr(Ptr(CChar)), lens: Ptr(CSize), count: CSize) -> String {
    total: CSize := 0
    i:     CSize := 0
    while i < count, i += 1 { total = total + lens[i] }

    if total <= _STR_MAX_SMALL {
        s := String(small = _Small(buf = void, tag_len = _make_tag_len(total)))
        // FIXME: BUG WORKAROUND: Cannot use s.small.buf here -- see _buf() for explanation.
        p:   Ptr(any)   := s.&
        dst: Ptr(CChar) := p
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
    if buf == null { _tl_fatal_("String: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < count, j += 1 {
        if lens[j] > 0 {
            d: Ptr(CChar) := Unsafe.pointer_add(buf, off)
            c_memcpy(d, bufs[j], lens[j])
        }
        off = off + lens[j]
    }
    String(big = _Big(len = total, buf = buf))
}

cat(alloc, a: String, b: String) -> String {
    bufs: CArray(Ptr(CChar), 2) := void
    lens: CArray(CSize, 2)      := void
    bufs[0] = _buf(a.&) lens[0] = len(a)
    bufs[1] = _buf(b.&) lens[1] = len(b)
    bp: Ptr(Ptr(CChar)) := bufs
    lp: Ptr(CSize)      := lens
    _cat_impl(alloc, bp, lp, 2)
}

cat(a: String, b: String) -> String {
    cat(Alloc.context.managed, a, b)
}

cat_3(alloc, a: String, b: String, c: String) -> String {
    bufs: CArray(Ptr(CChar), 3) := void
    lens: CArray(CSize, 3)      := void
    bufs[0] = _buf(a.&) lens[0] = len(a)
    bufs[1] = _buf(b.&) lens[1] = len(b)
    bufs[2] = _buf(c.&) lens[2] = len(c)
    bp: Ptr(Ptr(CChar)) := bufs
    lp: Ptr(CSize)      := lens
    _cat_impl(alloc, bp, lp, 3)
}

cat_3(a: String, b: String, c: String) -> String {
    cat_3(Alloc.context.managed, a, b, c)
}

cat_4(alloc, a: String, b: String, c: String, d: String) -> String {
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

cat_4(a: String, b: String, c: String, d: String) -> String {
    cat_4(Alloc.context.managed, a, b, c, d)
}

push(self: Ptr(String), alloc, other: String) {
    result := cat(alloc, self.*, other)
    free(self, alloc)
    self.* = result
}

push(self: Ptr(String), other: String) {
    push(self, Alloc.context.managed, other)
}


// -- Slicing --

slice(alloc, s: String, start: CSize, end: CSize) -> String {
    n := len(s)
    if start > end || end > n { _tl_fatal_("String.slice: index out of bounds") }
    slen := end - start
    if slen == 0 { return empty() }
    sp              := _buf(s.&)
    src: Ptr(CChar) := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, slen)
}

slice(s: String, start: CSize, end: CSize) -> String {
    slice(Alloc.context.managed, s, start, end)
}


// -- Search --

index_of_byte(s: String, b: Byte) -> Int {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        if sp[i] == b { return i }
    }
    -1
}


// -- Mutation --

replace_byte(alloc, s: String, find: Byte, replace: Byte) -> String {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0
    while i < n, i += 1 {
        if rp[i] == find { rp[i] = replace }
    }
    result
}

replace_byte(s: String, find: Byte, replace: Byte) -> String {
    replace_byte(Alloc.context.managed, s, find, replace)
}


// -- Memory --

free(self: Ptr(String), alloc) {
    if !_is_small(self.*) {
        alloc->free(alloc, self->big.buf)
    }
    self.* = empty()
}

free(self: Ptr(String)) {
    free(self, Alloc.context.managed)
}
