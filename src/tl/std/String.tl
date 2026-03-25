// String — Small-string-optimized, immutable string type.
//
// Strings of 14 bytes or fewer are stored inline (no heap allocation).
// Larger strings use a heap-allocated buffer via an explicit allocator.
#module String
#import <Alloc.tl>
#import <Array.tl>
#import <Unsafe.tl>
#import <Hash.tl>
#import <cstring.tl>
#import <cstdlib.tl>
#import <cstdio.tl>

Allocator = Alloc.Allocator

// -- Types --
// Small string optimization: strings of 14 bytes or fewer are stored inline (no heap allocation).
// The String type is a 16-byte union of _Big (len + pointer) and _Small (15-byte buffer + tag/len byte).
// The 15-byte buffer holds up to 14 bytes of data + 1 byte for a null terminator (written lazily by cstr).
// The tag_len byte (byte 15) packs a 4-bit tag in the upper nibble and 4-bit length in the lower nibble.
// Tag value 0x01 indicates a small string.

_STR_SMALL_TAG: Byte  := 0x01
_STR_MAX_SMALL: CSize := 14

_Big: {
    len: CSize,
    buf: Ptr[CChar],
}

_Small: {
    buf:     CArray[CChar, 15],
    tag_len: CUnsignedChar,
}

String: { | big: _Big
          | ss: _Small }


// -- Synopsis --

// Construction.
empty      ()                                                      -> String
from_cstr  (alloc: Ptr[Allocator], s: Ptr[Const[CChar]])           -> String
from_cstr  (s: Ptr[Const[CChar]])                                  -> String
from_bytes (alloc: Ptr[Allocator], s: Ptr[Const[CChar]], n: CSize) -> String
from_bytes (s: Ptr[Const[CChar]], n: CSize)                        -> String
copy       (alloc: Ptr[Allocator], s: String)                      -> String
copy       (s: String)                                             -> String
from_int   (alloc: Ptr[Allocator], n: Int)                         -> String
from_int   (n: Int)                                                -> String
from_float (alloc: Ptr[Allocator], f: Float)                       -> String
from_float (f: Float)                                              -> String
from_byte  (b: Byte)                                               -> String
from_bool  (b: Bool)                                               -> String

// Queries.
len      (s: String)               -> CSize
is_empty (s: String)               -> Bool
byte_at  (s: String, index: CSize) -> Option[Byte]
char_at  (s: String, index: CSize) -> Option[CChar]
cstr     (s: Ptr[String])          -> CString

// Hashing.
hash (s: String) -> CSize

// Comparison.
eq           (a: String, b: String)             -> Bool
cmp          (a: String, b: String)             -> CInt
starts_with  (s: String, prefix: String)        -> Bool
ends_with    (s: String, suffix: String)        -> Bool
contains     (haystack: String, needle: String) -> Bool
contains_char(s: String, b: CChar)              -> Bool
eq_cstr      (s: String, cs: CString)           -> Bool

// Concatenation.
cat   (alloc: Ptr[Allocator], a: String, b: String)                       -> String
cat   (a: String, b: String)                                              -> String
cat_3 (alloc: Ptr[Allocator], a: String, b: String, c: String)            -> String
cat_3 (a: String, b: String, c: String)                                   -> String
cat_4 (alloc: Ptr[Allocator], a: String, b: String, c: String, d: String) -> String
cat_4 (a: String, b: String, c: String, d: String)                        -> String
push  (self: Ptr[String], alloc: Ptr[Allocator], other: String)           -> Void
push  (self: Ptr[String], other: String)                                  -> Void

// Slicing.
slice (alloc: Ptr[Allocator], s: String, start: CSize, end: CSize) -> String
slice (s: String, start: CSize, end: CSize)                        -> String

// Search.
index_of_char     (s: String, b: CChar)               -> Option[CSize]
index_of_char_from(s: String, b: CChar, start: CSize) -> Option[CSize]
last_index_of_char(s: String, b: CChar)               -> Option[CSize]
index_of          (s: String, needle: String)         -> Option[CSize]
last_index_of     (s: String, needle: String)         -> Option[CSize]
count_byte        (s: String, b: CChar)               -> CSize
count             (s: String, needle: String)         -> CSize

// Mutation.
replace_byte(alloc: Ptr[Allocator], s: String, find: CChar, replace: CChar) -> String
replace_byte(s: String, find: CChar, replace: CChar)                        -> String

// Transformation.
to_upper    (alloc: Ptr[Allocator], s: String)                             -> String
to_upper    (s: String)                                                    -> String
to_lower    (alloc: Ptr[Allocator], s: String)                             -> String
to_lower    (s: String)                                                    -> String
trim_left   (alloc: Ptr[Allocator], s: String)                             -> String
trim_left   (s: String)                                                    -> String
trim_right  (alloc: Ptr[Allocator], s: String)                             -> String
trim_right  (s: String)                                                    -> String
trim        (alloc: Ptr[Allocator], s: String)                             -> String
trim        (s: String)                                                    -> String
repeat      (alloc: Ptr[Allocator], s: String, count: CSize)               -> String
repeat      (s: String, count: CSize)                                      -> String
replace     (alloc: Ptr[Allocator], s: String, old: String, new_s: String) -> String
replace     (s: String, old: String, new_s: String)                        -> String
strip_prefix(alloc: Ptr[Allocator], s: String, prefix: String)             -> String
strip_prefix(s: String, prefix: String)                                    -> String
strip_suffix(alloc: Ptr[Allocator], s: String, suffix: String)             -> String
strip_suffix(s: String, suffix: String)                                    -> String

// Split and join.
split(alloc: Ptr[Allocator], s: String, delimiter: String)          -> Array.Array[String]
split(s: String, delimiter: String)                                 -> Array.Array[String]
join (alloc: Ptr[Allocator], sep: String, arr: Array.Array[String]) -> String
join (sep: String, arr: Array.Array[String])                        -> String

// Memory.
free(self: Ptr[String], alloc: Ptr[Allocator]) -> Void
free(self: Ptr[String])                        -> Void


// -- Private helpers --

_is_small     (s: String)                                               -> Bool
_small_len    (s: String)                                               -> CSize
_buf          (s: Ptr[String])                                          -> Ptr[CChar]
_make_tag_len (n: CSize)                                                -> Byte
_is_whitespace(b: CChar)                                                -> Bool
_init_small   (src: Ptr[Const[CChar]], n: CSize)                        -> String
_init_big     (alloc: Ptr[Allocator], src: Ptr[Const[CChar]], n: CSize) -> String

// -- Implementation --

_is_small(s) {
    (s.ss.tag_len & _STR_SMALL_TAG) == _STR_SMALL_TAG
}

_small_len(s) {
    tl: CSize := s.ss.tag_len, (tl & 0xF0) >> 4

}

_buf(s) {
    return if _is_small(s.*) { s->ss.buf } else { s->big.buf }
}

_make_tag_len(n) {
    tag:    CSize := _STR_SMALL_TAG
    r:      CSize := ((n & 0x0F) << 4) | tag
    result: Byte  := r
    result
}

_is_whitespace(b) {
    b == 32 || b == 9 || b == 10 || b == 13 || b == 11 || b == 12
}


_init_small(src, n) {
    s := String(ss = _Small(buf = void, tag_len = _make_tag_len(n)))
    if n > 0 {
        dst: Ptr[CChar] := s.ss.buf
        c_memcpy(dst, src, n)
    }
    s
}

_init_big(alloc, src, n) {
    buf: Ptr[CChar] := alloc->malloc(alloc, n + 1)
    if buf == null { _tl_fatal_(c"String: out of memory") }
    if n > 0 { c_memcpy(buf, src, n) }
    String(big = _Big(len = n, buf = buf))
}


// -- Construction --

empty() {
    String(ss = _Small(buf = void, tag_len = _make_tag_len(0)))
}

from_bytes(alloc, s, n) {
    if n <= _STR_MAX_SMALL {
        _init_small(s, n)
    }
    else {
        _init_big(alloc, s, n)
    }
}

from_bytes(s, n) {
    from_bytes(Alloc.context.default, s, n)
}

from_cstr(alloc, s) {
    n := c_strlen(s)
    from_bytes (alloc, s, n)
}

from_cstr(s: Ptr[Const[CChar]]) {
    from_cstr(Alloc.context.default, s)
}

// Note: from_literal/1 is now a pseudo-function that is inlined at the transpiler phase. It still needs to
// have a type though.
from_literal(s: Ptr[Const[CChar]]) -> String


copy(alloc, s) {
    n := len (s)
    p := _buf (s.&)
    from_bytes(alloc, p, n)
}

copy(s) {
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

from_int(alloc, n: Int) {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_string_snprintf_int(ptr, 32, n)
    from_bytes(alloc, ptr, written)
}

from_int(n: Int) {
    from_int(Alloc.context.default, n)
}

from_float(alloc, f: Float) {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_string_snprintf_float(ptr, 32, f)
    from_bytes(alloc, ptr, written)
}

from_float(f: Float) {
    from_float(Alloc.context.default, f)
}

from_byte(b: Byte) {
    c: CChar := b
    _init_small(c.&, 1)
}

from_bool(b: Bool) {
    if b { return from_cstr(c"true") }
    from_cstr(c"false")
}


// -- Queries --

len(s) -> CSize {
    return if _is_small(s) { _small_len(s) } else { s.big.len }
}

is_empty(s) -> Bool {
    len(s) == 0
}

byte_at(s, index) {
    n := len(s)
    if index >= n { return None }
    p       := _buf(s.&)
    b: Byte := p.[index]                // cast CChar -> Byte
    Some(b)
}

char_at(s, index) {
    n := len(s)
    if index >= n { return None }
    p := _buf(s.&)
    Some(p.[index])
}

cstr(s) {
    n := len(s.*)
    p := _buf(s)
    if p.[n] != '\0' { p.[n] = '\0' }
    p
}


// -- Hashing --

hash(s) {
    Hash.fnv1a(_buf(s.&), len(s))
}


// -- Comparison --

eq(a, b) {
    0 == cmp(a, b)
}

cmp(a, b) {
    la      := len(a)
    lb      := len(b)
    pa      := _buf(a.&)
    pb      := _buf(b.&)
    min_len := if la < lb { la } else { lb }
    if min_len > 0 {
        r := c_memcmp(pa, pb, min_len)
        if r != 0 { return r }
    }
    if la < lb { - 1 } else if la > lb { 1 } else { 0 }
}

starts_with(s, prefix) {
    sn := len(s)
    pn := len(prefix)
    if pn > sn { return false }
    if pn == 0 { return true }
    sp := _buf(s.&)
    pp := _buf(prefix.&)
    c_memcmp(sp, pp, pn) == 0
}

ends_with(s, suffix) {
    sn := len(s)
    xn := len(suffix)
    if xn > sn { return false }
    if xn == 0 { return true }
    sp               := _buf(s.&)
    tail: Ptr[CChar] := Unsafe.pointer_add(sp, sn - xn)
    xp               := _buf(suffix.&)
    c_memcmp(tail, xp, xn) == 0
}

contains(haystack, needle) {
    s: Some := index_of(haystack, needle) else { false }
    true
}

contains_char(s, b: CChar) {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        ch: CChar := sp.[i]
        if ch == b { return true }
    }
    false
}

eq_cstr(s, cs: CString) {
    sn := len(s)
    cn := c_strlen(cs)
    if sn != cn { return false }
    if sn == 0 { return true }
    sp := _buf(s.&)
    c_memcmp(sp, cs, sn) == 0
}


// -- Concatenation --

_cat_impl(alloc: Ptr[Allocator], bufs: Ptr[Ptr[CChar]], lens: Ptr[CSize], count: CSize) {
    total: CSize := 0
    i:     CSize := 0
    while i < count, i += 1 { total += lens.[i] }

    if total <= _STR_MAX_SMALL {
        s               := String(ss = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := s.ss.buf
        off: CSize      := 0
        j:   CSize      := 0
        while j < count, j += 1 {
            if lens.[j] > 0 {
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, bufs.[j], lens.[j])
            }
            off += lens.[j]
        }
        return s
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1)
    if buf == null { _tl_fatal_(c"String: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < count, j += 1 {
        if lens.[j] > 0 {
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, bufs.[j], lens.[j])
        }
        off += lens.[j]
    }
    String(big = _Big(len = total, buf = buf))
}

cat(alloc, a, b) {
    bufs: CArray[Ptr[CChar], 2] := void
    lens: CArray[CSize, 2]      := void
    bufs.[0] = _buf(a.&) lens.[0] = len(a)
    bufs.[1] = _buf(b.&) lens.[1] = len(b)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 2)
}

cat(a, b) {
    cat(Alloc.context.default, a, b)
}

cat_3(alloc, a, b, c) {
    bufs: CArray[Ptr[CChar], 3] := void
    lens: CArray[CSize, 3]      := void
    bufs.[0] = _buf(a.&) lens.[0] = len(a)
    bufs.[1] = _buf(b.&) lens.[1] = len(b)
    bufs.[2] = _buf(c.&) lens.[2] = len(c)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 3)
}

cat_3(a, b, c) {
    cat_3(Alloc.context.default, a, b, c)
}

cat_4(alloc, a, b, c, d) {
    bufs: CArray[Ptr[CChar], 4] := void
    lens: CArray[CSize, 4]      := void
    bufs.[0] = _buf(a.&) lens.[0] = len(a)
    bufs.[1] = _buf(b.&) lens.[1] = len(b)
    bufs.[2] = _buf(c.&) lens.[2] = len(c)
    bufs.[3] = _buf(d.&) lens.[3] = len(d)
    bp: Ptr[Ptr[CChar]] := bufs
    lp: Ptr[CSize]      := lens
    _cat_impl(alloc, bp, lp, 4)
}

cat_4(a, b, c, d) {
    cat_4(Alloc.context.default, a, b, c, d)
}


push(self, alloc, other) {
    result := cat(alloc, self.*, other)
    free(self, alloc)
    self.* = result
}


push(self, other) {
    push(self, Alloc.context.default, other)
}


// -- Slicing --

slice(alloc, s, start, end) {
    n := len(s)
    if start > end || end > n { _tl_fatal_(c"String.slice: index out of bounds") }
    slen := end - start
    if slen == 0 { return empty() }
    sp              := _buf(s.&)
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, slen)
}

slice(s, start, end) {
    slice(Alloc.context.default, s, start, end)
}


// -- Search --

index_of_char(s, b) {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n, i += 1 {
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

index_of_char_from(s, b, start) {
    n := len(s)
    if start >= n { return None }
    sp       := _buf(s.&)
    i: CSize := start
    while i < n, i += 1 {
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

last_index_of_char(s, b) {
    n := len(s)
    if n == 0 { return None }
    sp       := _buf(s.&)
    i: CSize := n
    while i > 0 {
        i -= 1
        ch: CChar := sp.[i]
        if ch == b { return Some(i) }
    }
    None
}

index_of(s, needle) {
    sn       := len(s)
    nn       := len(needle)
    i: CSize := 0
    if nn == 0 { return Some(i) }
    if nn > sn { return None }
    sp := _buf(s.&)
    np := _buf(needle.&)
    while i <= sn - nn, i += 1 {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
    }
    None
}

last_index_of(s, needle) {
    sn := len(s)
    nn := len(needle)
    if nn == 0 { return Some(sn) }
    if nn > sn { return None }
    sp       := _buf(s.&)
    np       := _buf(needle.&)
    i: CSize := sn - nn + 1
    while i > 0 {
        i -= 1
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
    }
    None
}

count_byte(s, b) {
    n        := len(s)
    sp       := _buf(s.&)
    c: CSize := 0
    i: CSize := 0
    while i < n, i += 1 {
        ch: CChar := sp.[i]
        if ch == b { c += 1 }
    }
    c
}

count(s, needle) {
    sn := len(s)
    nn := len(needle)
    if nn == 0 { _tl_fatal_(c"String.count: empty needle") }
    if nn > sn { return 0 }
    sp       := _buf(s.&)
    np       := _buf(needle.&)
    c: CSize := 0
    i: CSize := 0
    while i <= sn - nn {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, np, nn) == 0 {
            c += 1
            i += nn
        } else {
            i += 1
        }
    }
    c
}


// -- Mutation --

replace_byte(alloc, s, find, replace) {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0
    while i < n, i += 1 {
        ch: CChar := rp.[i]
        if ch == find {
            rp.[i] = replace
        }
    }
    result
}

replace_byte(s, find, replace) {
    replace_byte(Alloc.context.default, s, find, replace)
}


// -- Transformation --

to_upper(alloc, s) {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0
    while i < n, i += 1 {
        if rp.[i] >= 'a' && rp.[i] <= 'z' { rp.[i] = rp.[i] - ' ' }
    }
    result
}

to_upper(s) {
    to_upper(Alloc.context.default, s)
}

to_lower(alloc, s) {
    result   := copy(alloc, s)
    n        := len(result)
    rp       := _buf(result.&)
    i: CSize := 0
    while i < n, i += 1 {
        if rp.[i] >= 'A' && rp.[i] <= 'Z' { rp.[i] = rp.[i] + ' ' }
    }
    result
}

to_lower(s) {
    to_lower(Alloc.context.default, s)
}

trim_left(alloc, s) {
    n        := len(s)
    sp       := _buf(s.&)
    i: CSize := 0
    while i < n {
        if !_is_whitespace(sp.[i]) { break }
        i += 1
    }
    if i == n { return empty() }
    src: Ptr[CChar] := Unsafe.pointer_add(sp, i)
    from_bytes(alloc, src, n - i)
}

trim_left(s) {
    trim_left(Alloc.context.default, s)
}

trim_right(alloc, s) {
    n := len(s)
    if n == 0 { return empty() }
    sp       := _buf(s.&)
    i: CSize := n
    while i > 0 {
        if !_is_whitespace(sp.[i - 1]) { break }
        i -= 1
    }
    if i == 0 { return empty() }
    from_bytes(alloc, sp, i)
}

trim_right(s) {
    trim_right(Alloc.context.default, s)
}

trim(alloc, s) {
    n := len(s)
    if n == 0 { return empty() }
    sp           := _buf(s.&)
    start: CSize := 0
    while start < n {
        if !_is_whitespace(sp.[start]) { break }
        start += 1
    }
    if start == n { return empty() }
    end: CSize := n
    while end > start {
        if !_is_whitespace(sp.[end - 1]) { break }
        end -= 1
    }
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    from_bytes(alloc, src, end - start)
}

trim(s) {
    trim(Alloc.context.default, s)
}

repeat(alloc, s, count) {
    if count == 0 { return empty() }
    sn := len(s)
    if sn == 0 { return empty() }
    total: CSize := sn * count
    sp           := _buf(s.&)

    if total <= _STR_MAX_SMALL {
        result          := String(ss = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := result.ss.buf
        off: CSize      := 0
        i:   CSize      := 0
        while i < count, i += 1 {
            d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
            c_memcpy(d, sp, sn)
            off += sn
        }
        return result
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1)
    if buf == null { _tl_fatal_(c"String.repeat: out of memory") }
    off: CSize := 0
    i:   CSize := 0
    while i < count, i += 1 {
        d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
        c_memcpy(d, sp, sn)
        off += sn
    }
    String(big = _Big(len = total, buf = buf))
}

repeat(s, count) {
    repeat(Alloc.context.default, s, count)
}

replace(alloc, s, old, new_s) {
    sn := len(s)
    on := len(old)
    if on == 0 { _tl_fatal_(c"String.replace: empty search string") }
    if on > sn { return copy(alloc, s) }
    nn := len(new_s)
    sp := _buf(s.&)
    op := _buf(old.&)

    occ := count(s, old)

    if occ == 0 { return copy(alloc, s) }

    total: CSize := sn - (occ * on) + (occ * nn)
    if total == 0 { return empty() }
    np := _buf (new_s.&)

    // Build result into heap buffer, then decide SSO
    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1)
    if buf == null { _tl_fatal_(c"String.replace: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < sn {
        if j <= sn - on {
            candidate: Ptr[CChar] := Unsafe.pointer_add(sp, j)
            if c_memcmp(candidate, op, on) == 0 {
                if nn > 0 {
                    d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
                    c_memcpy(d, np, nn)
                }
                off += nn
                j += on
            } else {
                buf.[off] = sp.[j]
                off += 1
                j += 1
            }
        } else {
            buf.[off] = sp.[j]
            off += 1
            j += 1
        }
    }

    if total <= _STR_MAX_SMALL {
        result := _init_small(buf, total)
        alloc->free(alloc, buf)
        return result
    }
    String(big = _Big(len = total, buf = buf))
}

replace(s, old, new_s) {
    replace(Alloc.context.default, s, old, new_s)
}

strip_prefix(alloc, s, prefix) {
    if !starts_with(s, prefix) { return copy(alloc, s) }
    pn        := len(prefix)
    sn        := len(s)
    remaining := sn - pn
    if remaining == 0 { return empty() }
    sp              := _buf(s.&)
    src: Ptr[CChar] := Unsafe.pointer_add(sp, pn)
    from_bytes(alloc, src, remaining)
}

strip_prefix(s, prefix) {
    strip_prefix(Alloc.context.default, s, prefix)
}

strip_suffix(alloc, s, suffix) {
    if !ends_with(s, suffix) { return copy(alloc, s) }
    xn        := len(suffix)
    sn        := len(s)
    remaining := sn - xn
    if remaining == 0 { return empty() }
    sp := _buf(s.&)
    from_bytes(alloc, sp, remaining)
}

strip_suffix(s, suffix) {
    strip_suffix(Alloc.context.default, s, suffix)
}


// -- Split and join --

split(alloc, s, delimiter) {
    dn := len(delimiter)
    if dn == 0 { _tl_fatal_(c"String.split: empty delimiter") }
    sn     := len(s)
    result := Array.empty[String]()

    if sn == 0 {
        e := empty()
        result.push(alloc, e)
        return result
    }

    if dn > sn {
        c := copy(alloc, s)
        result.push(alloc, c)
        return result
    }

    sp           := _buf(s.&)
    dp           := _buf(delimiter.&)
    start: CSize := 0
    i:     CSize := 0
    while i <= sn - dn, i += 1 {
        candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        if c_memcmp(candidate, dp, dn) == 0 {
            src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
            part            := from_bytes(alloc, src, i - start)
            result.push(alloc, part)
            start = i + dn
            i += dn - 1
        }
    }

    // Push remaining segment
    src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
    part            := from_bytes (alloc, src, sn - start)

    result.push(alloc, part)
    result
}

split(s, delimiter) {
    split(Alloc.context.default, s, delimiter)
}

join(alloc, sep, arr) {
    if arr.size == 0 { return empty() }
    if arr.size == 1 { return copy(alloc, arr.get(0)) }

    sn := len (sep)
    sp := _buf(sep.&)

    // Calculate total length
    total: CSize := 0
    i:     CSize := 0
    while i < arr.size, i += 1 {
        total += len(arr.get(i))
    }
    n: CSize := arr.size - 1
    total += (sn * n)

    if total == 0 { return empty() }

    if total <= _STR_MAX_SMALL {
        result          := String(ss = _Small(buf = void, tag_len = _make_tag_len(total)))
        dst: Ptr[CChar] := result.ss.buf
        off: CSize      := 0
        j:   CSize      := 0
        while j < arr.size, j += 1 {
            elem := arr.get(j)
            en   := len(elem)
            if en > 0 {
                ep            := _buf(elem.&)
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, ep, en)
            }
            off += en
            if j < arr.size - 1 && sn > 0 {
                d: Ptr[CChar] := Unsafe.pointer_add(dst, off)
                c_memcpy(d, sp, sn)
                off += sn
            }
        }
        return result
    }

    buf: Ptr[CChar] := alloc->malloc(alloc, total + 1)
    if buf == null { _tl_fatal_(c"String.join: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < arr.size, j += 1 {
        elem := arr.get(j)
        en   := len(elem)
        if en > 0 {
            ep            := _buf(elem.&)
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, ep, en)
        }
        off += en
        if j < arr.size - 1 && sn > 0 {
            d: Ptr[CChar] := Unsafe.pointer_add(buf, off)
            c_memcpy(d, sp, sn)
            off += sn
        }
    }
    String(big = _Big(len = total, buf = buf))
}

join(sep, arr) {
    join(Alloc.context.default, sep, arr)
}


// -- Memory --


free(self, alloc) {
    if !_is_small(self.*) {
        alloc->free(alloc, self->big.buf)
    }
    self.* = empty()
}


free(self) {
    free(self, Alloc.context.default)
}


// -- Iterator --

Iter: {
    buf:   Ptr[CChar],
    size:  CSize,
    index: CSize,
}

iter_init  (s: Ptr[String])  -> Iter
iter_ptr   (iter: Ptr[Iter]) -> Ptr[CChar]
iter_value (iter: Ptr[Iter]) -> CChar
iter_cond  (iter: Ptr[Iter]) -> Bool
iter_update(iter: Ptr[Iter]) -> Void
iter_deinit(iter: Ptr[Iter]) -> Void

iter_init  (s)    { Iter(buf = _buf(s), size = len(s.*), index = 0) }
iter_value (iter) { iter->buf.[iter->index]                         }
iter_ptr   (iter) { iter->buf.[iter->index].&                       }
iter_cond  (iter) { iter->index < iter->size                        }
iter_update(iter) { iter->index += 1                                }
iter_deinit(iter) {                                                 }
