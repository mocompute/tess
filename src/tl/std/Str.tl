#module Str
#import <Alloc.tl>
#import <Array.tl>
#import <Unsafe.tl>
#import <string.tl>
#import <stdlib.tl>
#import <stdio.tl>

Allocator = Alloc.Allocator

// Small string optimization: strings of 14 bytes or fewer are stored inline (no heap allocation).
// The Str type is a 16-byte union of _Big (len + pointer) and _Small (15-byte buffer + tag/len byte).
// The 15-byte buffer holds up to 14 bytes of data + 1 byte for a null terminator (written lazily by cstr).
// The tag_len byte (byte 15) packs a 4-bit tag in the upper nibble and 4-bit length in the lower nibble.
// Tag value 0x01 indicates a small string.

_STR_SMALL_TAG: Byte  := 0x01u
_STR_MAX_SMALL: CSize := 14zu

_Big: {
    len: CSize,
    buf: Ptr[CChar],
}

_Small: {
    buf:     CArray[CChar, 15],
    tag_len: CUnsignedChar,
}

Str: { | big: _Big
       | small: _Small }


// -- Synopsis --

// Construction.
empty     ()                                                      -> Str
from_cstr (alloc: Ptr[Allocator], s: Ptr[Const[CChar]])           -> Str
from_bytes(alloc: Ptr[Allocator], s: Ptr[Const[CChar]], n: CSize) -> Str
from_bytes(s: Ptr[Const[CChar]], n: CSize)                        -> Str
copy      (alloc: Ptr[Allocator], s: Str)                         -> Str
copy      (s: Str)                                                -> Str
from_int  (alloc: Ptr[Allocator], n: Int)                         -> Str
from_int  (n: Int)                                                -> Str
from_float(alloc: Ptr[Allocator], f: Float)                       -> Str
from_float(f: Float)                                              -> Str
from_byte (b: Byte)                                               -> Str
from_bool (b: Bool)                                               -> Str

// Queries.
len       (s: Str)               -> CSize
is_empty  (s: Str)               -> Bool
_is_small (s: Str)               -> Bool
byte_at   (s: Str, index: CSize) -> Option[Byte]
char_at   (s: Str, index: CSize) -> Option[CChar]
cstr      (s: Ptr[Str])          -> CString

// Hashing.
hash         (s: Str)                     -> CSize

// Comparison.
eq           (a: Str, b: Str)             -> Bool
cmp          (a: Str, b: Str)             -> Int
starts_with  (s: Str, prefix: Str)        -> Bool
ends_with    (s: Str, suffix: Str)        -> Bool
contains     (haystack: Str, needle: Str) -> Bool
contains_char(s: Str, b: CChar)           -> Bool
eq_cstr      (s: Str, cs: CString)        -> Bool

// Concatenation.
cat   (alloc: Ptr[Allocator], a: Str, b: Str)                 -> Str
cat   (a: Str, b: Str)                                        -> Str
cat_3 (alloc: Ptr[Allocator], a: Str, b: Str, c: Str)         -> Str
cat_3 (a: Str, b: Str, c: Str)                                -> Str
cat_4 (alloc: Ptr[Allocator], a: Str, b: Str, c: Str, d: Str) -> Str
cat_4 (a: Str, b: Str, c: Str, d: Str)                        -> Str
push  (self: Ptr[Str], alloc: Ptr[Allocator], other: Str)     -> Void
push  (self: Ptr[Str], other: Str)                            -> Void

// Slicing.
slice (alloc: Ptr[Allocator], s: Str, start: CSize, end: CSize) -> Str
slice (s: Str, start: CSize, end: CSize)                        -> Str

// Search.
index_of_char     (s: Str, b: CChar)               -> Option[CSize]
index_of_char_from(s: Str, b: CChar, start: CSize) -> Option[CSize]
last_index_of_char(s: Str, b: CChar)               -> Option[CSize]
index_of          (s: Str, needle: Str)            -> Option[CSize]
last_index_of     (s: Str, needle: Str)            -> Option[CSize]
count_byte        (s: Str, b: CChar)               -> CSize
count             (s: Str, needle: Str)            -> CSize

// Mutation.
replace_byte(alloc: Ptr[Allocator], s: Str, find: CChar, replace: CChar) -> Str
replace_byte(s: Str, find: CChar, replace: CChar)                        -> Str

// Transformation.
to_upper    (alloc: Ptr[Allocator], s: Str)                       -> Str
to_upper    (s: Str)                                              -> Str
to_lower    (alloc: Ptr[Allocator], s: Str)                       -> Str
to_lower    (s: Str)                                              -> Str
trim_left   (alloc: Ptr[Allocator], s: Str)                       -> Str
trim_left   (s: Str)                                              -> Str
trim_right  (alloc: Ptr[Allocator], s: Str)                       -> Str
trim_right  (s: Str)                                              -> Str
trim        (alloc: Ptr[Allocator], s: Str)                       -> Str
trim        (s: Str)                                              -> Str
repeat      (alloc: Ptr[Allocator], s: Str, count: CSize)         -> Str
repeat      (s: Str, count: CSize)                                -> Str
replace     (alloc: Ptr[Allocator], s: Str, old: Str, new_s: Str) -> Str
replace     (s: Str, old: Str, new_s: Str)                        -> Str
strip_prefix(alloc: Ptr[Allocator], s: Str, prefix: Str)          -> Str
strip_prefix(s: Str, prefix: Str)                                 -> Str
strip_suffix(alloc: Ptr[Allocator], s: Str, suffix: Str)          -> Str
strip_suffix(s: Str, suffix: Str)                                 -> Str

// Split and join.
split(alloc: Ptr[Allocator], s: Str, delimiter: Str)          -> Array.Array[Str]
split(s: Str, delimiter: Str)                                 -> Array.Array[Str]
join (alloc: Ptr[Allocator], sep: Str, arr: Array.Array[Str]) -> Str
join (sep: Str, arr: Array.Array[Str])                        -> Str

// Memory.
free(self: Ptr[Str], alloc: Ptr[Allocator]) -> Void
free(self: Ptr[Str])                        -> Void


// -- Implementation --

// -- Private helpers --

_is_small(s: Str) -> Bool {
    (s.small.tag_len & _STR_SMALL_TAG) == _STR_SMALL_TAG
}

_small_len(s: Str) -> CSize {
    tl: CSize := s.small.tag_len, (tl & 0xF0zu) >> 4zu

}

_buf(s: Ptr[Str]) -> Ptr[CChar] {
    return if _is_small(s.*) { s->small.buf } else { s->big.buf }
}

_make_tag_len(n: CSize) -> Byte {
    tag:    CSize := _STR_SMALL_TAG
    r:      CSize := ((n & 0x0Fzu) << 4zu) | tag
    result: Byte  := r
    result
}

_is_whitespace(b: CChar) -> Bool {
    b == 32 || b == 9 || b == 10 || b == 13 || b == 11 || b == 12
}


_init_small(src: Ptr[Const[CChar]], n: CSize) -> Str {
    s := Str(small = _Small(buf = void, tag_len = _make_tag_len(n)))
    if n > 0zu {
        dst: Ptr[CChar] := s.small.buf
        c_memcpy(dst, src, n)
    }
    s
}

_init_big(alloc: Ptr[Allocator], src: Ptr[Const[CChar]], n: CSize) -> Str {
    buf: Ptr[CChar] := alloc->malloc(alloc, n + 1zu)
    if buf == null { _tl_fatal_("Str: out of memory") }
    if n > 0zu { c_memcpy(buf, src, n) }
    Str(big = _Big(len = n, buf = buf))
}


// -- Construction --

empty() -> Str {
    Str(small = _Small(buf = void, tag_len = _make_tag_len(0zu)))
}

from_bytes(alloc, s: Ptr[Const[CChar]], n: CSize) -> Str {
    if n <= _STR_MAX_SMALL {
        _init_small(s, n)
    }
    else {
        _init_big(alloc, s, n)
    }
}

from_bytes(s: Ptr[Const[CChar]], n: CSize) -> Str {
    from_bytes(Alloc.context.default, s, n)
}

from_cstr(alloc, s: Ptr[Const[CChar]]) -> Str {
    n := c_strlen(s)
    from_bytes   (alloc, s, n)
}

from_cstr(s: Ptr[Const[CChar]]) -> Str {
    from_cstr(Alloc.context.default, s)
}


copy(alloc, s: Str) -> Str {
    n := len  (s)
    p := _buf (s.&)
    from_bytes(alloc, p, n)
}

copy(s: Str) -> Str {
    copy(Alloc.context.default, s)
}

#ifc
#include <stdio.h>
static size_t tl_string_snprintf_int(char *buf, size_t sz, long long n) {
    int r = snprintf(buf, sz, "%lld", n);
    return r < 0 ? 0 : (size_t)r;
}
static size_t tl_string_snprintf_float(char *buf, size_t sz, double f) {
    int r = snprintf(buf, sz, "%g", f);
    return r < 0 ? 0 : (size_t)r;
}
#endc

c_tl_string_snprintf_int  (buf: Ptr[CChar], sz: CSize, n: Int)   -> CSize
c_tl_string_snprintf_float(buf: Ptr[CChar], sz: CSize, f: Float) -> CSize

from_int(alloc, n: Int) -> Str {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_string_snprintf_int(ptr, 32zu, n)
    from_bytes(alloc, ptr, written)
}

from_int(n: Int) -> Str {
    from_int(Alloc.context.default, n)
}

from_float(alloc, f: Float) -> Str {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_string_snprintf_float(ptr, 32zu, f)
    from_bytes(alloc, ptr, written)
}

from_float(f: Float) -> Str {
    from_float(Alloc.context.default, f)
}

from_byte(b: Byte) -> Str {
    c: CChar := b
    _init_small(c.&, 1zu)
}

from_bool(b: Bool) -> Str {
    if b { return from_cstr("true") }
    from_cstr("false")
}


// -- Queries --

len(s: Str) -> CSize {
    return if _is_small(s) { _small_len(s) } else { s.big.len }
}

is_empty(s: Str) -> Bool {
    len(s) == 0zu
}

byte_at(s: Str, index: CSize) -> Option[Byte] {
    n := len(s)
    if index >= n { return None }
    p       := _buf(s.&)
    b: Byte := p.[index]                // cast CChar -> Byte
    Some(b)
}

char_at(s: Str, index: CSize) -> Option[CChar] {
    n := len(s)
    if index >= n { return None }
    p := _buf(s.&)
    Some(p.[index])
}

cstr(s: Ptr[Str]) -> CString {
    n := len(s.*)
    p := _buf(s)
    if p.[n] != '\0' { p.[n] = '\0' }
    p
}


// -- Hashing --

hash(s: Str) -> CSize {
    n  := len(s)
    p  := _buf(s.&)
    h: CSize := 0xcbf29ce484222325zu
    i: CSize := 0zu
    while i < n, i += 1zu {
        b: CSize := p.[i]
        h = h ^ b
        h = h * 0x100000001b3zu
    }
    h
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
    if min_len > 0zu {
        r := c_memcmp(pa, pb, min_len)
        if r != 0 { return r }
    }
    if la < lb { - 1 } else if la > lb { 1 } else { 0 }
}

starts_with(s: Str, prefix: Str) -> Bool {
    sn := len(s)
    pn := len(prefix)
    if pn > sn { return false }
    if pn == 0zu { return true }
    sp := _buf(s.&)
    pp := _buf(prefix.&)
    c_memcmp(sp, pp, pn) == 0
}

ends_with(s: Str, suffix: Str) -> Bool {
    sn := len(s)
    xn := len(suffix)
    if xn > sn { return false }
    if xn == 0zu { return true }
    sp               := _buf(s.&)
    tail: Ptr[CChar] := Unsafe.pointer_add(sp, sn - xn)
    xp               := _buf(suffix.&)
    c_memcmp(tail, xp, xn) == 0
}

contains(haystack: Str, needle: Str) -> Bool {
    hn := len(haystack)
    nn := len(needle)
    if nn == 0zu { return true }
    if nn > hn { return false }
    hp       := _buf(haystack.&)
    np       := _buf(needle.&)
    i: CSize := 0zu
    while i <= hn - nn, i += 1zu {
        candidate: Ptr[CChar] := Unsafe.pointer_add(hp, i)
        if c_memcmp(candidate, np, nn) == 0 { return true }
    }
    false
}

contains_char(s: Str, b: CChar) -> Bool {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0zu
    while i < n, i += 1zu {
        ch: CChar := sp.[i]
        if ch == b { return true }
    }
    false
}

eq_cstr(s: Str, cs: CString) -> Bool {
    sn := len(s)
    cn := c_strlen(cs)
    if sn != cn { return false }
    if sn == 0zu { return true }
    sp := _buf(s.&)
    c_memcmp(sp, cs, sn) == 0
}


// -- Concatenation --

_cat_impl(alloc: Ptr[Allocator], bufs: Ptr[Ptr[CChar]], lens: Ptr[CSize], count: CSize) -> Str {
    total: CSize := 0zu
    i:     CSize := 0zu
    while i < count, i += 1zu { total = total + lens.[i] }

    if total <= _STR_MAX_SMALL {
        s               := Str(small = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := s.small.buf
        off: CSize      := 0zu
        j:   CSize      := 0zu
        while j < count, j += 1zu {
            if lens.[j] > 0zu {
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, bufs.[j], lens.[j])
            }
            off = off + lens.[j]
        }
        return s
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1zu)
    if buf == null { _tl_fatal_("Str: out of memory") }
    off: CSize := 0zu
    j:   CSize := 0zu
    while j < count, j += 1zu {
        if lens.[j] > 0zu {
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, bufs.[j], lens.[j])
        }
        off = off + lens.[j]
    }
    Str(big = _Big(len = total, buf = buf))
}

cat(alloc, a: Str, b: Str) -> Str {
    bufs: CArray[Ptr[CChar], 2] := void
    lens: CArray[CSize, 2]      := void
    bufs.[0zu] = _buf(a.&) lens.[0zu] = len(a)
    bufs.[1zu] = _buf(b.&) lens.[1zu] = len(b)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 2zu)
}

cat(a: Str, b: Str) -> Str {
    cat(Alloc.context.default, a, b)
}

cat_3(alloc, a: Str, b: Str, c: Str) -> Str {
    bufs: CArray[Ptr[CChar], 3] := void
    lens: CArray[CSize, 3]      := void
    bufs.[0zu] = _buf(a.&) lens.[0zu] = len(a)
    bufs.[1zu] = _buf(b.&) lens.[1zu] = len(b)
    bufs.[2zu] = _buf(c.&) lens.[2zu] = len(c)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 3zu)
}

cat_3(a: Str, b: Str, c: Str) -> Str {
    cat_3(Alloc.context.default, a, b, c)
}

cat_4(alloc, a: Str, b: Str, c: Str, d: Str) -> Str {
    bufs: CArray[Ptr[CChar], 4] := void
    lens: CArray[CSize, 4]      := void
    bufs.[0zu] = _buf(a.&) lens.[0zu] = len(a)
    bufs.[1zu] = _buf(b.&) lens.[1zu] = len(b)
    bufs.[2zu] = _buf(c.&) lens.[2zu] = len(c)
    bufs.[3zu] = _buf(d.&) lens.[3zu] = len(d)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 4zu)
}

cat_4(a: Str, b: Str, c: Str, d: Str) -> Str {
    cat_4(Alloc.context.default, a, b, c, d)
}


push(self: Ptr[Str], alloc, other: Str) {
    result := cat(alloc, self.*, other)
    free(self, alloc)
    self.* = result
}


push(self: Ptr[Str], other: Str) {
    push(self, Alloc.context.default, other)
}


// -- Slicing --

slice(alloc, s: Str, start: CSize, end: CSize) -> Str {
    n := len(s)
    if start > end || end > n { _tl_fatal_("Str.slice: index out of bounds") }
    slen := end - start
    if slen == 0zu { return empty() }
    sp              := _buf(s.&)
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, slen)
}

slice(s: Str, start: CSize, end: CSize) -> Str {
    slice(Alloc.context.default, s, start, end)
}


// -- Search --

index_of_char(s: Str, b: CChar) -> Option[CSize] {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0zu
    while i < n, i += 1zu {
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

index_of_char_from(s: Str, b: CChar, start: CSize) -> Option[CSize] {
    n := len(s)
    if start >= n { return None }
    sp       := _buf(s.&)
    i: CSize := start
    while i < n, i += 1zu {
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

last_index_of_char(s: Str, b: CChar) -> Option[CSize] {
    n := len(s)
    if n == 0zu { return None }
    sp       := _buf(s.&)
    i: CSize := n
    while i > 0zu {
        i = i - 1zu
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

index_of(s: Str, needle: Str) -> Option[CSize] {
    sn := len(s)
    nn := len(needle)
    if nn == 0zu { return Some(0zu) }
    if nn > sn { return None }
    sp       := _buf(s.&)
    np       := _buf(needle.&)
    i: CSize := 0zu
    while i <= sn - nn, i += 1zu {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
    }
    None
}

last_index_of(s: Str, needle: Str) -> Option[CSize] {
    sn := len(s)
    nn := len(needle)
    if nn == 0zu { return Some(sn) }
    if nn > sn { return None }
    sp       := _buf(s.&)
    np       := _buf(needle.&)
    i: CSize := sn - nn + 1zu
    while i > 0zu {
        i = i - 1zu
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
    }
    None
}

count_byte(s: Str, b: CChar) -> CSize {
    n        := len(s)
    sp       := _buf(s.&)
    c: CSize := 0zu
    i: CSize := 0zu
    while i < n, i += 1zu {
        ch: CChar := sp.[i]
        if ch == b { c = c + 1zu}
    }
    c
}

count(s: Str, needle: Str) -> CSize {
    sn := len(s)
    nn := len(needle)
    if nn == 0zu { _tl_fatal_("Str.count: empty needle") }
    if nn > sn { return 0zu }
    sp       := _buf(s.&)
    np       := _buf(needle.&)
    c: CSize := 0zu
    i: CSize := 0zu
    while i <= sn - nn {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 {
            c = c + 1zu
            i = i + nn
        } else {
            i = i + 1zu
        }
    }
    c
}


// -- Mutation --

replace_byte(alloc, s: Str, find: CChar, replace: CChar) -> Str {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0zu
    while i < n, i += 1zu {
        ch: CChar := rp.[i]
        if ch == find {
            rp.[i] = replace
        }
    }
    result
}

replace_byte(s: Str, find: CChar, replace: CChar) -> Str {
    replace_byte(Alloc.context.default, s, find, replace)
}


// -- Transformation --

to_upper(alloc, s: Str) -> Str {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0zu
    while i < n, i += 1zu {
        if rp.[i] >= 'a' && rp.[i] <= 'z' { rp.[i] = rp.[i] - ' ' }
    }
    result
}

to_upper(s: Str) -> Str {
    to_upper(Alloc.context.default, s)
}

to_lower(alloc, s: Str) -> Str {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0zu
    while i < n, i += 1zu {
        if rp.[i] >= 'A' && rp.[i] <= 'Z' { rp.[i] = rp.[i] + ' ' }
    }
    result
}

to_lower(s: Str) -> Str {
    to_lower(Alloc.context.default, s)
}

trim_left(alloc, s: Str) -> Str {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0zu
    while i < n {
        if !_is_whitespace(sp.[i]) { break }
        i = i + 1zu
    }
    if i == n { return empty() }
    src: Ptr[CChar] := Unsafe.pointer_add(sp, i)
    from_bytes(alloc, src, n - i)
}

trim_left(s: Str) -> Str {
    trim_left(Alloc.context.default, s)
}

trim_right(alloc, s: Str) -> Str {
    n := len(s)
    if n == 0zu { return empty() }
    sp       := _buf(s.&)
    i: CSize := n
    while i > 0zu {
        if !_is_whitespace(sp.[i - 1zu]) { break }
        i = i - 1zu
    }
    if i == 0zu { return empty() }
    from_bytes(alloc, sp, i)
}

trim_right(s: Str) -> Str {
    trim_right(Alloc.context.default, s)
}

trim(alloc, s: Str) -> Str {
    n := len(s)
    if n == 0zu { return empty() }
    sp           := _buf(s.&)
    start: CSize := 0zu
    while start < n {
        if !_is_whitespace(sp.[start]) { break }
        start = start + 1zu
    }
    if start == n { return empty() }
    end: CSize := n
    while end > start {
        if !_is_whitespace(sp.[end - 1zu]) { break }
        end = end - 1zu
    }
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, end - start)
}

trim(s: Str) -> Str {
    trim(Alloc.context.default, s)
}

repeat(alloc, s: Str, count: CSize) -> Str {
    if count == 0zu { return empty() }
    sn := len(s)
    if sn == 0zu { return empty() }
    total: CSize := sn * count
    sp           := _buf(s.&)

    if total <= _STR_MAX_SMALL {
        result          := Str(small = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := result.small.buf
        off: CSize      := 0zu
        i:   CSize      := 0zu
        while i < count, i += 1zu {
            d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
            c_memcpy(d, sp, sn)
            off = off + sn
        }
        return result
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1zu)
    if buf == null { _tl_fatal_("Str.repeat: out of memory") }
    off: CSize := 0zu
    i:   CSize := 0zu
    while i < count, i += 1zu {
        d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
        c_memcpy(d, sp, sn)
        off = off + sn
    }
    Str(big = _Big(len = total, buf = buf))
}

repeat(s: Str, count: CSize) -> Str {
    repeat(Alloc.context.default, s, count)
}

replace(alloc, s: Str, old: Str, new_s: Str) -> Str {
    sn := len(s)
    on := len(old)
    if on == 0zu { _tl_fatal_("Str.replace: empty search string") }
    if on > sn { return copy(alloc, s) }
    nn := len(new_s)
    sp := _buf(s.&)
    op := _buf(old.&)

    // Count occurrences
    occ: CSize := 0zu
    i:   CSize := 0zu
    while i <= sn - on, i += 1zu {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, op, on) == 0 {
            occ = occ + 1zu
            i   = i + on - 1zu
        }
    }

    if occ == 0zu { return copy(alloc, s) }

    total: CSize := sn - (occ * on) + (occ * nn)
    if total == 0zu { return empty() }
    np := _buf (new_s.&)

    // Build result into heap buffer, then decide SSO
    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1zu)
    if buf == null { _tl_fatal_("Str.replace: out of memory") }
    off: CSize := 0zu
    j:   CSize := 0zu
    while j < sn {
        if j <= sn - on {
            candidate: Ptr[CChar] := Unsafe.pointer_add(sp, j)
            if c_memcmp(candidate, op, on) == 0 {
                if nn > 0zu {
                    d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
                    c_memcpy(d, np, nn)
                }
                off = off + nn
                j   = j + on
            } else {
                buf.[off] = sp.[j]
                off       = off + 1zu
                j         = j + 1zu
            }
        } else {
            buf.[off] = sp.[j]
            off       = off + 1zu
            j         = j + 1zu
        }
    }

    if total <= _STR_MAX_SMALL {
        result := _init_small(buf, total)
        alloc->free(alloc, buf)
        return result
    }
    Str(big = _Big(len = total, buf = buf))
}

replace(s: Str, old: Str, new_s: Str) -> Str {
    replace(Alloc.context.default, s, old, new_s)
}

strip_prefix(alloc, s: Str, prefix: Str) -> Str {
    if !starts_with(s, prefix) { return copy(alloc, s) }
    pn        := len(prefix)
    sn        := len(s)
    remaining := sn - pn
    if remaining == 0zu { return empty() }
    sp              := _buf(s.&)
    src: Ptr[CChar] := Unsafe.pointer_add(sp, pn)
    from_bytes(alloc, src, remaining)
}

strip_prefix(s: Str, prefix: Str) -> Str {
    strip_prefix(Alloc.context.default, s, prefix)
}

strip_suffix(alloc, s: Str, suffix: Str) -> Str {
    if !ends_with(s, suffix) { return copy(alloc, s) }
    xn        := len(suffix)
    sn        := len(s)
    remaining := sn - xn
    if remaining == 0zu { return empty() }
    sp := _buf(s.&)
    from_bytes(alloc, sp, remaining)
}

strip_suffix(s: Str, suffix: Str) -> Str {
    strip_suffix(Alloc.context.default, s, suffix)
}


// -- Split and join --

split(alloc, s: Str, delimiter: Str) -> Array.Array[Str] {
    dn := len(delimiter)
    if dn == 0zu { _tl_fatal_("Str.split: empty delimiter") }
    sn     := len(s)
    result := Array.empty[Str]()

    if sn == 0zu {
        e := empty()
        Array.push(result.&, alloc, e)
        return result
    }

    if dn > sn {
        c := copy(alloc, s)
        Array.push(result.&, alloc, c)
        return result
    }

    sp           := _buf(s.&)
    dp           := _buf(delimiter.&)
    start: CSize := 0zu
    i:     CSize := 0zu
    while i <= sn - dn, i += 1zu {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, dp, dn) == 0 {
            src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
            part            := from_bytes(alloc, src, i - start)
            Array.push(result.&, alloc, part)
            start = i + dn
            i     = i + dn - 1zu
        }
    }

    // Push remaining segment
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    part            := from_bytes        (alloc, src, sn - start)
    Array.push                           (result.&, alloc, part)

    result
}

split(s: Str, delimiter: Str) -> Array.Array[Str] {
    split(Alloc.context.default, s, delimiter)
}

join(alloc, sep: Str, arr: Array.Array[Str]) -> Str {
    if arr.size == 0zu { return empty()                          }
    if arr.size == 1zu { return copy(alloc, Array.get(arr, 0zu)) }

    sn := len (sep)
    sp := _buf(sep.&)

    // Calculate total length
    total: CSize := 0zu
    i:     CSize := 0zu
    while i < arr.size, i += 1zu {
        total = total + len(Array.get(arr, i))
    }
    n: CSize := arr.size - 1zu
    total = total + (sn * n)

    if total == 0zu { return empty() }

    if total <= _STR_MAX_SMALL {
        result          := Str(small = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := result.small.buf
        off: CSize      := 0zu
        j:   CSize      := 0zu
        while j < arr.size, j += 1zu {
            elem := Array.get(arr, j)
            en   := len(elem)
            if en > 0zu {
                ep            := _buf(elem.&)
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, ep, en)
            }
            off = off + en
            if j < arr.size - 1zu && sn > 0zu {
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, sp, sn)
                off = off + sn
            }
        }
        return result
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1zu)
    if buf == null { _tl_fatal_("Str.join: out of memory") }
    off: CSize := 0zu
    j:   CSize := 0zu
    while j < arr.size, j += 1zu {
        elem := Array.get(arr, j)
        en   := len(elem)
        if en > 0zu {
            ep            := _buf(elem.&)
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, ep, en)
        }
        off = off + en
        if j < arr.size - 1zu && sn > 0zu {
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, sp, sn)
            off = off + sn
        }
    }
    Str(big = _Big(len = total, buf = buf))
}

join(sep: Str, arr: Array.Array[Str]) -> Str {
    join(Alloc.context.default, sep, arr)
}


// -- Memory --


free(self: Ptr[Str], alloc) {
    if !_is_small(self.*) {
        alloc->free(alloc, self->big.buf)
    }
    self.* = empty()
}


free(self: Ptr[Str]) {
    free(self, Alloc.context.default)
}


// -- Iterator --

Iter: {
    buf:   Ptr[CChar],
    size:  CSize,
    index: CSize,
}

iter_init  (s: Ptr[Str])     -> Iter
iter_ptr   (iter: Ptr[Iter]) -> Ptr[CChar]
iter_value (iter: Ptr[Iter]) -> CChar
iter_cond  (iter: Ptr[Iter]) -> Bool
iter_update(iter: Ptr[Iter]) -> Void
iter_deinit(iter: Ptr[Iter]) -> Void

iter_init  (s)    { Iter(buf = _buf(s), size = len(s.*), index = 0zu) }
iter_value (iter) { iter->buf.[iter->index]                           }
iter_ptr   (iter) { iter->buf.[iter->index].&                         }
iter_cond  (iter) { iter->index < iter->size                          }
iter_update(iter) { iter->index = iter->index + 1zu                   }
iter_deinit(iter) {                                                   }
