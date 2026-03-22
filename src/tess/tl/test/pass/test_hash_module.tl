// Test the Hash module's FNV-1a functions.

#module main
#import <String.tl>
#import <Hash.tl>
#import <cstring.tl>

main() -> CInt {
    error: CInt := 0

    // Hash.fnv1a on a raw buffer matches hash for the same content.
    s      := String.from_cstr(c"hello")
    h_str  := s.hash()
    p      := String.cstr(s.&)
    h_hash := Hash.fnv1a(p, String.len(s))
    error += if h_str == h_hash { 0 } else { 1 }

    // Non-zero sanity check.
    error += if h_hash != 0zu { 0 } else { 1 }

    // Different input produces different hash.
    s2     := String.from_cstr(c"world")
    h2_str := s2.hash()
    h2_raw := Hash.fnv1a(String.cstr(s2.&), String.len(s2))
    error += if h2_str == h2_raw { 0 } else { 1 }
    error += if h_hash != h2_raw { 0 } else { 1 }

    // Empty buffer matches hash(String.empty()).
    e                  := String.empty()
    h_empty            := e.hash()
    null_p: Ptr[CChar] := c""
    h_raw_empty        := Hash.fnv1a(null_p, 0zu)
    error += if h_empty == h_raw_empty { 0 } else { 1 }

    // Hash.fnv1a_cstring matches hash for the same content.
    h_cstr := Hash.fnv1a_cstring(c"hello")
    error += if h_cstr == h_str { 0 } else { 1 }

    error
}
