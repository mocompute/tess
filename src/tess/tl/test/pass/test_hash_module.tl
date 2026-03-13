// Test the Hash module's FNV-1a functions.

#module main
#import <Str.tl>
#import <Hash.tl>
#import <string.tl>

main() -> CInt {
    error: CInt := 0

    // Hash.fnv1a on a raw buffer matches hash for the same content.
    s := Str.from_cstr("hello")
    h_str  := hash(s)
    p      := Str.cstr(s.&)
    h_hash := Hash.fnv1a(p, Str.len(s))
    error += if h_str == h_hash { 0 } else { 1 }

    // Non-zero sanity check.
    error += if h_hash != 0zu { 0 } else { 1 }

    // Different input produces different hash.
    s2     := Str.from_cstr("world")
    h2_str := hash(s2)
    h2_raw := Hash.fnv1a(Str.cstr(s2.&), Str.len(s2))
    error += if h2_str == h2_raw { 0 } else { 1 }
    error += if h_hash != h2_raw { 0 } else { 1 }

    // Empty buffer matches hash(Str.empty()).
    e       := Str.empty()
    h_empty := hash(e)
    null_p: Ptr[CChar] := ""
    h_raw_empty := Hash.fnv1a(null_p, 0zu)
    error += if h_empty == h_raw_empty { 0 } else { 1 }

    // Hash.fnv1a_cstring matches hash for the same content.
    h_cstr := Hash.fnv1a_cstring("hello")
    error += if h_cstr == h_str { 0 } else { 1 }

    error
}
