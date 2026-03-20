// ToString — trait for types that can be converted to a String representation.
//
// A type satisfying ToString must provide a unary to_string function returning String.
// Used as a variadic bound: print(args: ...ToString) applies to_string to each arg.

#import <String.tl>
#import <Slice.tl>

#module ToString

ToString[T]: { to_string(a: T) -> String }

// -- C helpers for generic to_string implementations --
// These use the Ptr[any] + sizeof pattern (same as Hash.fnv1a) to handle all
// members of a type family through a single generic function.

#ifc
#include <stdio.h>
#include <string.h>

static size_t tl_signed_int_snprintf(char *buf, size_t bufsz, const void *p, size_t sz) {
    long long v;
    switch (sz) {
        case 1: { signed char x;  memcpy(&x, p, 1); v = x; break; }
        case 2:  { short x;        memcpy(&x, p, 2); v = x; break; }
        case 4:  { int x;          memcpy(&x, p, 4); v = x; break; }
        default: { memcpy(&v, p, sizeof(v)); break; }
    }
    int r = snprintf(buf, bufsz, "%lld", v);
    return r < 0 ? 0 : (size_t)r;
}

static size_t tl_unsigned_int_snprintf(char *buf, size_t bufsz, const void *p, size_t sz) {
    unsigned long long v = 0;
    memcpy(&v, p, sz < sizeof(v) ? sz : sizeof(v));
    int r = snprintf(buf, bufsz, "%llu", v);
    return r < 0 ? 0 : (size_t)r;
}

static size_t tl_float_snprintf(char *buf, size_t bufsz, const void *p, size_t sz) {
    double v;
    if (sz == sizeof(float)) { float f; memcpy(&f, p, sizeof(f)); v = (double)f; }
    else if (sz > sizeof(double)) { long double ld; memcpy(&ld, p, sz); v = (double)ld; }
    else { memcpy(&v, p, sizeof(v)); }
    int r = snprintf(buf, bufsz, "%g", v);
    return r < 0 ? 0 : (size_t)r;
}
#endc

c_tl_signed_int_snprintf  (buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize) -> CSize
c_tl_unsigned_int_snprintf(buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize) -> CSize
c_tl_float_snprintf       (buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize) -> CSize


// -- Implementations for builtin type families --

#module Int

to_string[T](n: T) -> String {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_signed_int_snprintf(ptr, 32, n.&, sizeof(n))
    String.from_bytes(ptr, written)
}

#module UInt

to_string[T](n: T) -> String {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_unsigned_int_snprintf(ptr, 32, n.&, sizeof(n))
    String.from_bytes(ptr, written)
}

#module Float

to_string[T](f: T) -> String {
    buf: CArray[CChar, 32] := void
    ptr: Ptr[CChar]        := buf
    written                := c_tl_float_snprintf(ptr, 32, f.&, sizeof(f))
    String.from_bytes(ptr, written)
}

#module Bool
to_string(b: Bool) -> String { String.from_bool(b) }

#module CString
to_string(s: CString) -> String { String.from_cstr(s) }

#module String
to_string(s: String) -> String { s }


// -- Print functions --

#module Print

_print_all(args: Slice[String]) -> Void

_print_all(args) {
    i: CSize := 0
    while i < args.size, i += 1 {
        s := args.v.[i]
        c_printf("%s", s.&.cstr())
    }
    void
}

print(args: ...ToString) -> Void { _print_all(args) }

println(args: ...ToString) -> Void {
    _print_all(args)
    c_printf("\n")
    void
}
