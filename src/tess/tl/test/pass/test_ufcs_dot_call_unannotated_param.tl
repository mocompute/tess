// UFCS dot-call on an unannotated function parameter.
// `content.char_at(i)` desugars to `String.char_at(content, i)` once
// specialization concretizes the parameter type.

#module main
#import <String.tl>

_count(content) {
    i:  CSize := 0
    ch: Some  := content.char_at(i) else { return 1 }
    0
}

main() {
    _count("hello")
}
