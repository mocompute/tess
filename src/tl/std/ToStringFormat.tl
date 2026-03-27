// ToStringFormat — trait for types that support format specifiers.
// A type satisfying ToStringFormat must provide to_string_format(a: T, spec: FormatSpec) -> String.
// Types may implement ToStringFormat without implementing it for all specifier combinations;
// unsupported specifiers produce implementation-defined output.
//
// Note: the C helpers below share the size-dispatch preamble pattern with ToString.tl but
// extend it with format spec handling (sign, alt, zero-pad, width, precision, type_char).

#import <String.tl>
#import <FormatSpec.tl>

#module ToStringFormat

ToStringFormat[T]: { to_string_format(a: T, spec: FormatSpec) -> String }

// -- C helpers --

#ifc
#include <stdio.h>
#include <string.h>

// Format a signed integer with full format spec support.
// Handles: sign, alt form (#), zero-pad, type_char (d/x/X/o/b), width (for zero-pad only).
static size_t tl_signed_int_format(
    char *buf, size_t bufsz,
    const void *p, size_t sz,
    char sign, int alt, int zero_pad, int width, char type_char)
{
    long long v;
    switch (sz) {
        case 1:  { signed char x;  memcpy(&x, p, 1); v = x; break; }
        case 2:  { short x;        memcpy(&x, p, 2); v = x; break; }
        case 4:  { int x;          memcpy(&x, p, 4); v = x; break; }
        default: { memcpy(&v, p, sizeof(v)); break; }
    }

    // Build a printf format string dynamically
    char fmt[32];
    int fi = 0;
    fmt[fi++] = '%';

    // Sign
    if (sign == '+')      fmt[fi++] = '+';
    else if (sign == ' ') fmt[fi++] = ' ';

    // Alt form
    if (alt) fmt[fi++] = '#';

    // Zero-pad with width
    if (zero_pad && width > 0) {
        fi += snprintf(fmt + fi, sizeof(fmt) - fi, "0%d", width);
    }

    // Conversion
    if (type_char == 'x')       { fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llx"); }
    else if (type_char == 'X')  { fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llX"); }
    else if (type_char == 'o')  { fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llo"); }
    else if (type_char == 'b') {
        // Binary: C doesn't have %b widely, do it manually
        fmt[0] = '\0'; // cancel the format string
        unsigned long long uv = (unsigned long long)v;
        if (v < 0) uv = (unsigned long long)v; // two's complement

        char tmp[72];
        int ti = 0;

        // Sign prefix for negative
        if (v < 0)        { tmp[ti++] = '-'; uv = -(unsigned long long)v; }
        else if (sign == '+') tmp[ti++] = '+';
        else if (sign == ' ') tmp[ti++] = ' ';

        if (alt) { tmp[ti++] = '0'; tmp[ti++] = 'b'; }

        // Convert to binary digits
        if (uv == 0) {
            tmp[ti++] = '0';
        } else {
            char digits[64];
            int di = 0;
            unsigned long long t = uv;
            while (t > 0) { digits[di++] = '0' + (t & 1); t >>= 1; }
            for (int i = di - 1; i >= 0; i--) tmp[ti++] = digits[i];
        }
        tmp[ti] = '\0';

        // Zero-pad
        if (zero_pad && width > ti) {
            // Insert zeros after sign/prefix, before digits
            int prefix_len = 0;
            if (v < 0 || sign == '+' || sign == ' ') prefix_len++;
            if (alt) prefix_len += 2;
            int need = width - ti;
            // Shift digits right
            memmove(tmp + prefix_len + need, tmp + prefix_len, ti - prefix_len + 1);
            memset(tmp + prefix_len, '0', need);
            ti += need;
        }

        size_t len = (size_t)ti;
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, tmp, len);
        buf[len] = '\0';
        return len;
    }
    else { fi += snprintf(fmt + fi, sizeof(fmt) - fi, "lld"); }
    fmt[fi] = '\0';

    if (fmt[0] == '\0') return strlen(buf); // binary path already wrote buf

    // Handle octal alt form prefix: C's # gives 0, we want 0o
    if (alt && type_char == 'o') {
        // Format without alt first, then prepend 0o manually
        char tmp_fmt[32];
        int tfi = 0;
        tmp_fmt[tfi++] = '%';
        if (sign == '+')      tmp_fmt[tfi++] = '+';
        else if (sign == ' ') tmp_fmt[tfi++] = ' ';
        if (zero_pad && width > 2) {
            tfi += snprintf(tmp_fmt + tfi, sizeof(tmp_fmt) - tfi, "0%d", width - 2);
        }
        tfi += snprintf(tmp_fmt + tfi, sizeof(tmp_fmt) - tfi, "llo");
        tmp_fmt[tfi] = '\0';

        char tmp[72];
        int r = snprintf(tmp, sizeof(tmp), tmp_fmt, v);
        if (r < 0) return 0;

        // Determine sign prefix length
        int prefix = 0;
        if (tmp[0] == '-' || tmp[0] == '+' || tmp[0] == ' ') prefix = 1;

        // Insert "0o" after sign
        size_t slen = (size_t)r;
        if (slen + 2 >= bufsz) return 0;
        memcpy(buf, tmp, prefix);
        buf[prefix] = '0';
        buf[prefix + 1] = 'o';
        memcpy(buf + prefix + 2, tmp + prefix, slen - prefix + 1);
        return slen + 2;
    }

    int r = snprintf(buf, bufsz, fmt, v);
    return r < 0 ? 0 : (size_t)r;
}

// Format an unsigned integer.
static size_t tl_unsigned_int_format(
    char *buf, size_t bufsz,
    const void *p, size_t sz,
    char sign, int alt, int zero_pad, int width, char type_char)
{
    unsigned long long v = 0;
    memcpy(&v, p, sz < sizeof(v) ? sz : sizeof(v));

    char fmt[32];
    int fi = 0;
    fmt[fi++] = '%';
    if (sign == '+')      fmt[fi++] = '+';
    else if (sign == ' ') fmt[fi++] = ' ';
    if (alt && type_char != 'o' && type_char != 'b') fmt[fi++] = '#';
    if (zero_pad && width > 0) fi += snprintf(fmt + fi, sizeof(fmt) - fi, "0%d", width);

    if (type_char == 'x')       fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llx");
    else if (type_char == 'X')  fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llX");
    else if (type_char == 'o') {
        fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llo");
        fmt[fi] = '\0';

        if (alt) {
            char tmp[72];
            int r = snprintf(tmp, sizeof(tmp), fmt + 1 - 1, v); // use fmt without alt
            // Rebuild without C's # — prepend 0o
            char tmp2[72];
            fi = 0;
            tmp2[fi++] = '%';
            if (sign == '+')      tmp2[fi++] = '+';
            else if (sign == ' ') tmp2[fi++] = ' ';
            if (zero_pad && width > 2) fi += snprintf(tmp2 + fi, sizeof(tmp2) - fi, "0%d", width - 2);
            fi += snprintf(tmp2 + fi, sizeof(tmp2) - fi, "llo");
            tmp2[fi] = '\0';

            r = snprintf(tmp, sizeof(tmp), tmp2, v);
            if (r < 0) return 0;
            size_t slen = (size_t)r;
            if (slen + 2 >= bufsz) return 0;
            buf[0] = '0'; buf[1] = 'o';
            memcpy(buf + 2, tmp, slen + 1);
            return slen + 2;
        }
    }
    else if (type_char == 'b') {
        // Binary conversion
        char tmp[72];
        int ti = 0;
        if (alt) { tmp[ti++] = '0'; tmp[ti++] = 'b'; }
        if (v == 0) {
            tmp[ti++] = '0';
        } else {
            char digits[64]; int di = 0;
            unsigned long long t = v;
            while (t > 0) { digits[di++] = '0' + (t & 1); t >>= 1; }
            for (int i = di - 1; i >= 0; i--) tmp[ti++] = digits[i];
        }
        tmp[ti] = '\0';
        if (zero_pad && width > ti) {
            int prefix = alt ? 2 : 0;
            int need = width - ti;
            memmove(tmp + prefix + need, tmp + prefix, ti - prefix + 1);
            memset(tmp + prefix, '0', need);
            ti += need;
        }
        size_t len = (size_t)ti;
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, tmp, len);
        buf[len] = '\0';
        return len;
    }
    else { fi += snprintf(fmt + fi, sizeof(fmt) - fi, "llu"); }
    fmt[fi] = '\0';

    int r = snprintf(buf, bufsz, fmt, v);
    return r < 0 ? 0 : (size_t)r;
}

// Format a float.
static size_t tl_float_format(
    char *buf, size_t bufsz,
    const void *p, size_t sz,
    char sign, int precision, char type_char)
{
    double v;
    if (sz == sizeof(float)) { float f; memcpy(&f, p, sizeof(f)); v = (double)f; }
    else if (sz > sizeof(double)) { long double ld; memcpy(&ld, p, sz); v = (double)ld; }
    else { memcpy(&v, p, sizeof(v)); }

    char fmt[32];
    int fi = 0;
    fmt[fi++] = '%';
    if (sign == '+')      fmt[fi++] = '+';
    else if (sign == ' ') fmt[fi++] = ' ';
    if (precision >= 0) fi += snprintf(fmt + fi, sizeof(fmt) - fi, ".%d", precision);

    if      (type_char == 'e') fmt[fi++] = 'e';
    else if (type_char == 'E') fmt[fi++] = 'E';
    else if (type_char == 'f') fmt[fi++] = 'f';
    else                       fmt[fi++] = 'g'; // default
    fmt[fi] = '\0';

    int r = snprintf(buf, bufsz, fmt, v);
    return r < 0 ? 0 : (size_t)r;
}
#endc

c_tl_signed_int_format(buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize,
                        sign: CChar, alt: Bool, zero_pad: Bool, width: CInt,
                        type_char: CChar) -> CSize

c_tl_unsigned_int_format(buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize,
                          sign: CChar, alt: Bool, zero_pad: Bool, width: CInt,
                          type_char: CChar) -> CSize

c_tl_float_format(buf: Ptr[CChar], bufsz: CSize, p: Ptr[any], sz: CSize,
                   sign: CChar, precision: CInt, type_char: CChar) -> CSize

// -- Implementations --

#module Int

to_string_format[T](n: T, spec: FormatSpec) -> String {
    buf: CArray[CChar, 128] := void
    ptr: Ptr[CChar]         := buf
    written := c_tl_signed_int_format(ptr, 128, n.&, sizeof(n),
                   spec.sign, spec.alt, spec.zero_pad, spec.width, spec.type_char)
    String.from_bytes(ptr, written)
}

#module UInt

to_string_format[T](n: T, spec: FormatSpec) -> String {
    buf: CArray[CChar, 128] := void
    ptr: Ptr[CChar]         := buf
    written := c_tl_unsigned_int_format(ptr, 128, n.&, sizeof(n),
                   spec.sign, spec.alt, spec.zero_pad, spec.width, spec.type_char)
    String.from_bytes(ptr, written)
}

#module Float

to_string_format[T](f: T, spec: FormatSpec) -> String {
    buf: CArray[CChar, 128] := void
    ptr: Ptr[CChar]         := buf
    written := c_tl_float_format(ptr, 128, f.&, sizeof(f),
                   spec.sign, spec.precision, spec.type_char)
    String.from_bytes(ptr, written)
}
