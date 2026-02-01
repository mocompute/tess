// -- Str prelude --

#module_prelude Str

_Big: {
    len: CSize,
    buf: Ptr(CChar),
}

_Small: {
    buf:     CArray(CChar, 15),
    tag_len: CUnsignedChar,
}

Str: { | big: _Big
       | small: _Small }

from_literal(s: Ptr(CChar)) -> Str
