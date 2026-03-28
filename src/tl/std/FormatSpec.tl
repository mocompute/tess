// FormatSpec — format specifier for f-string formatting.
// Parsed at compile time from {expr:spec} syntax.
// Fields match the Python-inspired grammar: [[fill]align][sign][#][0][width][.precision][type]
#module FormatSpec
#import <String.tl>
#import <Alloc.tl>

FormatSpec: {
    fill:              CChar,
    align:             CChar,
    sign:              CChar,
    alt:               Bool,
    zero_pad:          Bool,
    width:             CInt,
    precision:         CInt,
    type_char:         CChar,
    has_type_specific: Bool,
}

// Synopsis
apply_layout(s: String, spec: FormatSpec) -> String

#ifc
#include <string.h>

// Pad a string to a given width with fill/align.
// Returns the number of bytes written to out (not null-terminated).
// out must have at least width bytes.
static size_t tl_format_apply_layout(
    char *out, size_t out_cap,
    const char *src, size_t src_len,
    char fill, char align, int width)
{
    if (fill == 0) fill = ' ';
    if (width <= 0 || src_len >= (size_t)width) {
        if (src_len > out_cap) src_len = out_cap;
        memcpy(out, src, src_len);
        return src_len;
    }

    size_t w       = (size_t)width;
    size_t padding = w - src_len;
    size_t left_pad, right_pad;

    switch (align) {
    case '<': left_pad = 0;            right_pad = padding;            break;
    case '>': left_pad = padding;      right_pad = 0;                 break;
    case '^': left_pad = padding / 2;  right_pad = padding - left_pad; break;
    default:  left_pad = padding;      right_pad = 0;                 break; // default right-align
    }

    size_t pos = 0;
    for (size_t i = 0; i < left_pad && pos < out_cap; i++) out[pos++] = fill;
    for (size_t i = 0; i < src_len  && pos < out_cap; i++) out[pos++] = src[i];
    for (size_t i = 0; i < right_pad && pos < out_cap; i++) out[pos++] = fill;
    return pos;
}
#endc

c_tl_format_apply_layout(
                         out:  Ptr[CChar], out_cap: CSize,
                         src:  Ptr[Const[CChar]], src_len: CSize,
                         fill: CChar, align: CChar, width: CInt) -> CSize

// Implementation
apply_layout(s, spec) {
    w := spec.width
    if w <= 0 { return s }
    wz: CSize := w
    if s.len() >= wz { return s }

    // Cap to avoid excessive allocation
    if w > 4096 { w = 4096 }

    sptr := s.cstr()

    // Stack buffer for common case; heap for large widths
    if w <= 256 {
        buf: CArray[CChar, 256] := void
        ptr: Ptr[CChar]         := buf
        written                 := c_tl_format_apply_layout(ptr, 256, sptr, s.len(), spec.fill, spec.align, w)
        return String.from_bytes(ptr, written)
    }

    cap: CSize      := w
    ptr: Ptr[CChar] := Alloc.transient_alloc(cap)
    written         := c_tl_format_apply_layout(ptr, cap, sptr, s.len(), spec.fill, spec.align, w)
    result          := String.from_bytes(ptr, written)
    Alloc.transient_free(ptr)
    result
}
