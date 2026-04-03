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

// Note: String type is defined in <builtin.tl> so it is always available without requiring an import of
// String.tl

// _Big: {
//     len: CSize,
//     buf: Ptr[CChar],
// }

// _Small: {
//     buf:     CArray[CChar, 15],
//     tag_len: CUnsignedChar,
// }

// String: { | big: _Big
//           | ss: _Small }


// -- Synopsis --

// Construction
empty      ()                                -> String
from_bytes (cs: Ptr[Const[CChar]], n: CSize) -> String
rom_cstr   (cs: Ptr[Const[CChar]])           -> String
from_int   (n: Int)                          -> String
from_float (f: Float)                        -> String
from_byte  (b: Byte)                         -> String
from_bool  (b: Bool)                         -> String

// Explicit allocator construction
from_bytes (alloc: Ptr[Allocator], cs: Ptr[Const[CChar]], n: CSize) -> String
from_cstr  (alloc: Ptr[Allocator], cs: Ptr[Const[CChar]])           -> String
from_int   (alloc: Ptr[Allocator], n: Int)                          -> String
from_float (alloc: Ptr[Allocator], f: Float)                        -> String
// from_byte and from_bool don't require allocators since they're small


// Immutable methods
(self: Ptr[Const[String]]): {
    // Construction
    copy  ()                         -> String
    copy  (start: CSize, end: CSize) -> String
    slice (start: CSize, end: CSize) -> Slice[CChar]

    // Queries
    byte_at      (index: CSize)               -> Option[Byte]
    char_at      (index: CSize)               -> Option[CChar]
    contains     (needle: Ptr[Const[String]]) -> Bool
    contains_char(b: CChar)                   -> Bool
    ends_with    (suffix: Ptr[Const[String]]) -> Bool
    eq_cstr      (cs: CString)                -> Bool
    hash         ()                           -> CSize
    is_empty     ()                           -> Bool
    len          ()                           -> CSize
    starts_with  (prefix: Ptr[Const[String]]) -> Bool

    // Search
    count             (needle: Ptr[Const[String]]) -> CSize
    count_byte        (b: CChar)                   -> CSize
    index_of          (needle: Ptr[Const[String]]) -> Option[CSize]
    index_of_char     (b: CChar)                   -> Option[CSize]
    index_of_char_from(b: CChar, start: CSize)     -> Option[CSize]
    last_index_of     (needle: Ptr[Const[String]]) -> Option[CSize]
    last_index_of_char(b: CChar)                   -> Option[CSize]

    // Concatenation (immutable)
    cat   (b: Ptr[Const[String]])                                               -> String
    cat_3 (b: Ptr[Const[String]], c: Ptr[Const[String]])                        -> String
    cat_4 (b: Ptr[Const[String]], c: Ptr[Const[String]], d: Ptr[Const[String]]) -> String

    // Transformation
    repeat      (count: CSize)                                       -> String
    replace     (old: Ptr[Const[String]], new_s: Ptr[Const[String]]) -> String
    replace_byte(find: CChar, replace: CChar)                        -> String
    strip_prefix(prefix: Ptr[Const[String]])                         -> String
    strip_suffix(suffix: Ptr[Const[String]])                         -> String
    to_lower    ()                                                   -> String
    to_upper    ()                                                   -> String
    trim_left   ()                                                   -> String
    trim_right  ()                                                   -> String
    trim        ()                                                   -> String

    split (delimiter: Ptr[Const[String]])                        -> Array[String]
    split (alloc: Ptr[Allocator], delimiter: Ptr[Const[String]]) -> Array[String]

    // Explicit allocators
    cat         (alloc: Ptr[Allocator], other: Ptr[Const[String]])                              -> String
    cat_3       (alloc: Ptr[Allocator], other1: Ptr[Const[String]], other2: Ptr[Const[String]]) -> String
    copy        (alloc: Ptr[Allocator])                                                         -> String
    copy        (alloc: Ptr[Allocator], start: CSize, end: CSize)                               -> String
    repeat      (alloc: Ptr[Allocator], count: CSize)                                           -> String
    replace     (alloc: Ptr[Allocator], old: Ptr[Const[String]], new_s: Ptr[Const[String]])     -> String
    replace_byte(alloc: Ptr[Allocator], find: CChar, replace: CChar)                            -> String
    strip_prefix(alloc: Ptr[Allocator], prefix: Ptr[Const[String]])                             -> String
    strip_suffix(alloc: Ptr[Allocator], suffix: Ptr[Const[String]])                             -> String
    to_upper    (alloc: Ptr[Allocator])                                                         -> String
    to_lower    (alloc: Ptr[Allocator])                                                         -> String
    trim_left   (alloc: Ptr[Allocator])                                                         -> String
    trim_right  (alloc: Ptr[Allocator])                                                         -> String
    trim        (alloc: Ptr[Allocator])                                                         -> String
}

// Mutating methods
(self: Ptr[String]): {
    // Mutate to zero-terminated C string
    cstr () -> CString

    // Mutable transformation.
    replace_byte_in_place(find: CChar, replace: CChar) -> Void
    to_upper_in_place    ()                            -> Void
    to_lower_in_place    ()                            -> Void


    // Mutable concatenation
    push (other: Ptr[Const[String]])                        -> Void
    push (alloc: Ptr[Allocator], other: Ptr[Const[String]]) -> Void

    // Memory.
    free( alloc: Ptr[Allocator]) -> Void
    free()                       -> Void

}

// Immutable binary operations
(self: Ptr[Const[String]], other: Ptr[Const[String]]): {
    eq  () -> Bool
    cmp () -> CInt
}

// Free functions

join (sep: Ptr[Const[String]], arr: Array[String])                        -> String
join (alloc: Ptr[Allocator], sep: Ptr[Const[String]], arr: Array[String]) -> String

// -- Implementation --

_cat_impl(alloc: Ptr[Allocator], bufs: Ptr[Ptr[CChar]], lens: Ptr[CSize], count: CSize) -> String

(s: Ptr[Const[String]]): {
    _is_small () { (s->ss.tag_len & _STR_SMALL_TAG) == _STR_SMALL_TAG }
    _small_len() { tl: CSize := s->ss.tag_len, (tl & 0xF0) >> 4       }

    _buf () -> Ptr[CChar] {             // Note: casts away const
        return if _is_small(s) { s->ss.buf } else { s->big.buf }
    }
}


_make_tag_len(n) {
    tag:    CSize := _STR_SMALL_TAG
    r:      CSize := ((n & 0x0F) << 4) | tag
    result: Byte  := r
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
    buf: Ptr[CChar] := alloc.alloc(n + 1)
    if buf == null { _tl_fatal_(c"String: out of memory") }
    if n > 0 { c_memcpy(buf, src, n) }
    String(big = _Big(len = n, buf = buf))
}


// -- Construction --

empty() {
    String(ss = _Small(buf = void, tag_len = _make_tag_len(0)))
}

from_bytes(alloc, cs, n) {
    if n <= _STR_MAX_SMALL {
        _init_small(cs, n)
    }
    else {
        _init_big(alloc, cs, n)
    }
}

from_bytes(cs, n) {
    from_bytes(Alloc.context.default, cs, n)
}

from_cstr(alloc, cs) {
    n := c_strlen(cs)
    from_bytes (alloc, cs, n)
}

from_cstr(cs) {
    from_cstr(Alloc.context.default, cs)
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


(s: Ptr[Const[String]]): {
    copy() {
        s.copy(Alloc.context.default)
    }

    copy(start, end) {
        s.copy(Alloc.context.default, start, end)
    }


    slice(start, end) {
        n := len(s)
        if start > end || end > n { _tl_fatal_(c"String.slice: index out of bounds") }
        slen := end - start
        if slen == 0 { return Slice[CChar](data: Ptr[CChar] = null, size = 0) }
        p               := _buf(s)
        src: Ptr[CChar] := Unsafe.pointer_add(p, start)
        Slice(data = src, size = slen)
    }


    byte_at(index) {
        n := len(s)
        if index >= n { return None }
        p       := _buf(s)
        b: Byte := p.[index]            // cast CChar -> Byte
        Some(b)
    }

    char_at(index) {
        n := len(s)
        if index >= n { return None }
        p := _buf(s)
        Some(p.[index])
    }

    contains(needle) {
        s: Some := index_of(s, needle) else { false }
        true
    }

    contains_char(b: CChar) {
        n        := len(s)
        sp       := _buf(s)
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := sp.[i]
            if ch == b { return true }
        }
        false
    }

    ends_with(suffix) {
        sn := len(s)
        xn := len(suffix)
        if xn > sn { return false }
        if xn == 0 { return true }
        sp               := _buf(s)
        tail: Ptr[CChar] := Unsafe.pointer_add(sp, sn - xn)
        xp               := _buf(suffix)
        c_memcmp(tail, xp, xn) == 0
    }

    eq_cstr(cs: CString) {
        sn := len(s)
        cn := c_strlen(cs)
        if sn != cn { return false }
        if sn == 0 { return true }
        sp := _buf(s)
        c_memcmp(sp, cs, sn) == 0
    }

    hash() {
        Hash.fnv1a(s._buf(), s.len())
    }

    is_empty() {
        len(s) == 0
    }

    len() {
        if _is_small(s) { _small_len(s) } else { s->big.len }
    }

    starts_with(prefix) {
        sn := len(s)
        pn := len(prefix)
        if pn > sn { return false }
        if pn == 0 { return true }
        sp := _buf(s)
        pp := _buf(prefix)
        c_memcmp(sp, pp, pn) == 0
    }

    count(needle) {
        sn := len(s)
        nn := len(needle)
        if nn == 0 { _tl_fatal_(c"String.count: empty needle") }
        if nn > sn { return 0 }
        sp       := _buf(s)
        np       := _buf(needle)
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

    count_byte(b) {
        n        := len(s)
        sp       := _buf(s)
        c: CSize := 0
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := sp.[i]
            if ch == b { c += 1 }
        }
        c
    }

    index_of(needle) {
        sn       := len(s)
        nn       := len(needle)
        i: CSize := 0
        if nn == 0 { return Some(i) }
        if nn > sn { return None }
        sp := _buf(s)
        np := _buf(needle)
        while i <= sn - nn, i += 1 {
            candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
            if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
        }
        None
    }

    index_of_char(b) {
        n        := len(s)
        sp       := _buf(s)
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := sp.[i]
            if ch == b { return Some(i) }
        }
        None
    }

    index_of_char_from(b, start) {
        n := len(s)
        if start >= n { return None }
        sp       := _buf(s)
        i: CSize := start
        while i < n, i += 1 {
            ch: CChar := sp.[i]
            if ch == b { return Some(i) }
        }
        None
    }

    last_index_of(needle) {
        sn := len(s)
        nn := len(needle)
        if nn == 0 { return Some(sn) }
        if nn > sn { return None }
        sp       := _buf(s)
        np       := _buf(needle)
        i: CSize := sn - nn + 1
        while i > 0 {
            i -= 1
            candidate: Ptr[CChar] := Unsafe.pointer_add(sp, i)
            if c_memcmp(candidate, np, nn) == 0 { return Some(i) }
        }
        None
    }

    last_index_of_char(b) {
        n := len(s)
        if n == 0 { return None }
        sp       := _buf(s)
        i: CSize := n
        while i > 0 {
            i -= 1
            ch: CChar := sp.[i]
            if ch == b { return Some(i) }
        }
        None
    }

    cat(b) {
        s.cat(Alloc.context.default, b)
    }

    cat_3(b, c) {
        s.cat_3(Alloc.context.default, b, c)
    }


    replace_byte(find, replace) {
        s.replace_byte(Alloc.context.default, find, replace)
    }

    to_lower() {
        s.to_lower(Alloc.context.default)
    }

    to_upper() {
        s.to_upper(Alloc.context.default)
    }


    trim_left() {
        s.trim_left(Alloc.context.default)
    }

    trim_right() {
        s.trim_right(Alloc.context.default)
    }

    trim() {
        s.trim(Alloc.context.default)
    }

    repeat(count) {
        s.repeat(Alloc.context.default, count)
    }

    replace(old, new_s) {
        s.replace(Alloc.context.default, old, new_s)
    }

    strip_prefix(prefix) {
        s.strip_prefix(Alloc.context.default, prefix)
    }

    strip_suffix(suffix) {
        s.strip_suffix(Alloc.context.default, suffix)
    }

    split(delimiter) {
        s.split(Alloc.context.default, delimiter)
    }

    // With allocators

    cat(alloc, b) {
        bufs: CArray[Ptr[CChar], 2] := void
        lens: CArray[CSize, 2]      := void
        bufs.[0] = _buf(s), lens.[0] = len(s)
        bufs.[1] = _buf(b), lens.[1] = len(b)

        // no implicit pointer decomposition
        bp: Ptr[Ptr[CChar]] := bufs
        lp: Ptr[CSize]      := lens

        _cat_impl(alloc, bp, lp, 2)

    }

    cat_3(alloc, b, c) {
        bufs: CArray[Ptr[CChar], 3] := void
        lens: CArray[CSize, 3]      := void
        bufs.[0] = _buf(s), lens.[0] = len(s)
        bufs.[1] = _buf(b), lens.[1] = len(b)
        bufs.[2] = _buf(c), lens.[2] = len(c)

        // no implicit pointer decomposition
        bp: Ptr[Ptr[CChar]] := bufs
        lp: Ptr[CSize]      := lens

        _cat_impl(alloc, bp, lp, 3)
    }

    copy(alloc) {
        n := len (s)
        p := _buf (s)
        from_bytes(alloc, p, n)
    }

    copy(alloc, start, end) {
        n := len(s)
        if start > end || end > n { _tl_fatal_(c"String.copy: index out of bounds") }
        slen := end - start
        if slen == 0 { return empty() }
        sp              := _buf(s)
        src: Ptr[CChar] := Unsafe.pointer_add(sp, start)
        from_bytes(alloc, src, slen)
    }

    replace_byte(alloc, find, replace) {
        result := s.copy(alloc)
        replace_byte_in_place(result.&, find, replace)
        result
    }

    to_lower(alloc) {
        result := s.copy(alloc)
        to_lower_in_place(result.&)
        result
    }

    to_upper(alloc) {
        result := s.copy(alloc)
        to_upper_in_place(result.&)
        result
    }

    trim_left(alloc) {
        n        := len(s)
        sp       := _buf(s)
        i: CSize := 0
        while i < n {
            if !_is_whitespace(sp.[i]) { break }
            i += 1
        }
        if i == n { return empty() }
        src: Ptr[CChar] := Unsafe.pointer_add(sp, i)
        from_bytes(alloc, src, n - i)
    }

    trim_right(alloc) {
        n := len(s)
        if n == 0 { return empty() }
        sp       := _buf(s)
        i: CSize := n
        while i > 0 {
            if !_is_whitespace(sp.[i - 1]) { break }
            i -= 1
        }
        if i == 0 { return empty() }
        from_bytes(alloc, sp, i)
    }

    trim(alloc) {
        n := len(s)
        if n == 0 { return empty() }
        sp           := _buf(s)
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

    repeat(alloc, count) {
        if count == 0 { return empty() }
        sn := len(s)
        if sn == 0 { return empty() }
        total: CSize := sn * count
        sp           := _buf(s)

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

        buf: Ptr[CChar] := alloc.alloc(total + 1)
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

    replace(alloc, old, new_s) {
        sn := len(s)
        on := len(old)
        if on == 0 { _tl_fatal_(c"String.replace: empty search string") }
        if on > sn { return s.copy(alloc) }
        nn := len(new_s)
        sp := _buf(s)
        op := _buf(old)

        occ := s.count(old)

        if occ == 0 { return s.copy(alloc) }

        total: CSize := sn - (occ * on) + (occ * nn)
        if total == 0 { return empty() }
        np := _buf(new_s)

        // Build result into heap buffer, then decide SSO
        buf: Ptr[CChar] := alloc.alloc(total + 1)
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
            alloc.dealloc(buf)
            return result
        }
        String(big = _Big(len = total, buf = buf))
    }

    strip_prefix(alloc, prefix) {
        if !s.starts_with(prefix) { return s.copy(alloc) }
        pn        := len(prefix)
        sn        := len(s)
        remaining := sn - pn
        if remaining == 0 { return empty() }
        sp              := _buf(s)
        src: Ptr[CChar] := Unsafe.pointer_add(sp, pn)
        from_bytes(alloc, src, remaining)
    }

    strip_suffix(alloc, suffix) {
        if !s.ends_with(suffix) { return s.copy(alloc) }
        xn        := len(suffix)
        sn        := len(s)
        remaining := sn - xn
        if remaining == 0 { return empty() }
        sp := _buf(s)
        from_bytes(alloc, sp, remaining)
    }

    split(alloc, delimiter) {
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
            c := s.copy(alloc)
            result.push(alloc, c)
            return result
        }

        sp           := _buf(s)
        dp           := _buf(delimiter)
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


}

(self: Ptr[String]): {
    cstr() {
        n := len(self)
        p := _buf(self)
        if p.[n] != '\0' { p.[n] = '\0' }
        p
    }

    push(alloc, other) {
        result := self.cat(alloc, other)
        free(self, alloc)
        self.* = result
    }


    push(other) {
        self.push(Alloc.context.default, other)
    }

    replace_byte_in_place(find, replace) {
        n        := len(self)
        rp       := _buf(self)
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := rp.[i]
            if ch == find {
                rp.[i] = replace
            }
        }
    }

    to_upper_in_place() {
        n        := len(self)
        rp       := _buf(self)
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := rp.[i]
            if ch >= 'a' && ch <= 'z' { rp.[i] = ch - ' ' }
        }
    }

    to_lower_in_place() {
        n        := len(self)
        rp       := _buf(self)
        i: CSize := 0
        while i < n, i += 1 {
            ch: CChar := rp.[i]
            if ch >= 'A' && ch <= 'Z' { rp.[i] = ch + ' ' }
        }
    }


    free(alloc) {
        if !_is_small(self) {
            alloc.dealloc(self->big.buf)
        }
        self.* = empty()
    }


    free() {
        self.free(Alloc.context.default)
    }
}


(self: Ptr[Const[String]], other: Ptr[Const[String]]): {
    eq() {
        0 == self.cmp(other)
    }

    cmp() {
        la      := len(self)
        lb      := len(other)
        pa      := _buf(self)
        pb      := _buf(other)
        min_len := if la < lb { la } else { lb }
        if min_len > 0 {
            r := c_memcmp(pa, pb, min_len)
            if r != 0 { return r }
        }
        if la < lb { - 1 } else if la > lb { 1 } else { 0 }
    }

}


_cat_impl(alloc, bufs: Ptr[Ptr[CChar]], lens: Ptr[CSize], count: CSize) {
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

    buf: Ptr[CChar] := alloc.alloc(total + 1)
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


join(alloc, sep, arr) {
    if arr.size == 0 { return empty() }
    e0 := arr.get(0)
    if arr.size == 1 { return e0.copy(alloc) }

    sn := sep.len()
    sp := sep._buf()

    // Calculate total length
    total: CSize := 0
    i:     CSize := 0
    while i < arr.size, i += 1 {
        total += arr.get(i).len()
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
            en   := elem.len()
            if en > 0 {
                ep            := elem._buf()
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

    buf: Ptr[CChar] := alloc.alloc(total + 1)
    if buf == null { _tl_fatal_(c"String.join: out of memory") }
    off: CSize := 0
    j:   CSize := 0
    while j < arr.size, j += 1 {
        elem := arr.get(j)
        en   := elem.len()
        if en > 0 {
            ep            := elem._buf()
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


// -- Iterator --

Iter: {
    buf:   Ptr[CChar],
    size:  CSize,
    index: CSize,
}

iter_init (s: Ptr[String]) { Iter(buf = _buf(s), size = len(s), index = 0) }

(iter: Ptr[Iter]): {

    iter_value () -> CChar      { iter->buf.[iter->index]   }
    iter_ptr   () -> Ptr[CChar] { iter->buf.[iter->index].& }
    iter_cond  () -> Bool       { iter->index < iter->size  }
    iter_update() -> Void       { iter->index += 1          }
    iter_deinit() -> Void       {                           }

}
